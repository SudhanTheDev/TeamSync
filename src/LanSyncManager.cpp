#include "LanSyncManager.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkInterface>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpServer>
#include <QTcpSocket>
#include <QtEndian>

#include <algorithm>

namespace teamsync {
namespace {

constexpr quint32 maxFrameSize = 256U * 1024U * 1024U;

QString native(const std::filesystem::path& path) {
    return QString::fromStdWString(path.wstring());
}

QString normalizedRelative(QString path) {
    path.replace('\\', '/');
    while (path.startsWith("./")) path.remove(0, 2);
    return path;
}

} // namespace

LanSyncManager::LanSyncManager(std::filesystem::path workspaceRoot)
    : root_(std::filesystem::absolute(std::move(workspaceRoot))) {}

LanSyncManager::~LanSyncManager() {
    for (auto* socket : incoming_.keys()) socket->disconnectFromHost();
    delete server_;
}

bool LanSyncManager::startHost(const quint16 port, QString& error, const QString& requestedCode) {
    if (mode_ != Mode::Local) {
        error = "A connection mode is already active.";
        return false;
    }
    auto* server = new QTcpServer;
    if (!server->listen(QHostAddress::AnyIPv4, port)) {
        error = "Could not host TeamSync on port " + QString::number(port) + ": " + server->errorString();
        delete server;
        return false;
    }
    server_ = server;
    port_ = server_->serverPort();
    pairingCode_ = requestedCode.size() == 6 ? requestedCode
        : QString::number(QRandomGenerator::global()->bounded(100000, 1000000));
    mode_ = Mode::Host;
    QObject::connect(server_, &QTcpServer::newConnection, server_, [this] { acceptConnections(); });
    return true;
}

bool LanSyncManager::joinHost(const QString& address, const quint16 port,
                              const QString& code, QString& error) {
    if (mode_ != Mode::Local) {
        error = "A connection mode is already active.";
        return false;
    }
    hostAddress_ = address.trimmed();
    port_ = port;
    pairingCode_ = code.trimmed();
    QJsonObject response;
    if (!request({{"action", "join"}, {"code", pairingCode_}}, response, error)) return false;
    if (!response.value("ok").toBool()) {
        error = response.value("error").toString("The host rejected the connection.");
        return false;
    }
    if (!applySnapshot(response.value("snapshot").toObject(), error)) return false;
    revision_ = response.value("revision").toVariant().toULongLong();
    lastSync_ = QDateTime::currentDateTime();
    mode_ = Mode::Client;
    return true;
}

bool LanSyncManager::pull(bool& changed, QString& error) {
    changed = false;
    if (mode_ != Mode::Client) return true;
    QJsonObject response;
    if (!request({{"action", "pull"}, {"code", pairingCode_},
                  {"revision", QString::number(revision_)}}, response, error)) return false;
    if (!response.value("ok").toBool()) {
        error = response.value("error").toString("The host rejected the sync request.");
        return false;
    }
    const auto remoteRevision = response.value("revision").toVariant().toULongLong();
    if (response.value("changed").toBool()) {
        if (!applySnapshot(response.value("snapshot").toObject(), error)) return false;
        changed = true;
    }
    revision_ = remoteRevision;
    lastSync_ = QDateTime::currentDateTime();
    return true;
}

bool LanSyncManager::push(QString& error) {
    if (mode_ != Mode::Client) return true;
    QJsonObject response;
    if (!request({{"action", "push"}, {"code", pairingCode_},
                  {"revision", QString::number(revision_)}, {"snapshot", createSnapshot()}},
                 response, error)) return false;
    if (!response.value("ok").toBool()) {
        error = response.value("error").toString("The host rejected the workspace update.");
        if (response.contains("snapshot")) {
            QString ignored;
            applySnapshot(response.value("snapshot").toObject(), ignored);
            revision_ = response.value("revision").toVariant().toULongLong();
        }
        return false;
    }
    revision_ = response.value("revision").toVariant().toULongLong();
    lastSync_ = QDateTime::currentDateTime();
    return true;
}

void LanSyncManager::noteLocalChange() {
    if (mode_ == Mode::Host) ++revision_;
}

void LanSyncManager::setRemoteCommitHandler(std::function<void()> handler) {
    remoteCommitHandler_ = std::move(handler);
}

QStringList LanSyncManager::localAddresses() const {
    QStringList result;
    for (const auto& address : QNetworkInterface::allAddresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) {
            result << address.toString();
        }
    }
    result.removeDuplicates();
    return result;
}

QStringList LanSyncManager::activePeerAddresses() const {
    QStringList result;
    const auto cutoff = QDateTime::currentDateTime().addSecs(-8);
    for (auto iterator = peerLastSeen_.cbegin(); iterator != peerLastSeen_.cend(); ++iterator) {
        if (iterator.value() >= cutoff) result << iterator.key();
    }
    result.sort();
    return result;
}

QString LanSyncManager::statusText() const {
    if (mode_ == Mode::Host) {
        const auto addresses = localAddresses();
        return "Hosting on " + (addresses.isEmpty() ? QString("this computer") : addresses.join(" or ")) +
               ':' + QString::number(port_) + "  •  Pairing code " + pairingCode_;
    }
    if (mode_ == Mode::Client) return "Connected to " + hostAddress_ + ':' + QString::number(port_);
    return "Local workspace (not shared on the network)";
}

QString LanSyncManager::lastSyncText() const {
    if (mode_ == Mode::Local) return "Not connected";
    if (mode_ == Mode::Host) return "Hosting revision " + QString::number(revision_);
    return lastSync_.isValid() ? lastSync_.toString("yyyy-MM-dd HH:mm:ss") : "Waiting for first sync";
}

QByteArray LanSyncManager::workspaceFingerprint() const {
    const auto snapshot = createSnapshot();
    return QCryptographicHash::hash(QJsonDocument(snapshot).toJson(QJsonDocument::Compact),
                                    QCryptographicHash::Sha256);
}

void LanSyncManager::acceptConnections() {
    while (server_ && server_->hasPendingConnections()) {
        auto* socket = server_->nextPendingConnection();
        incoming_.insert(socket, {});
        QObject::connect(socket, &QTcpSocket::readyRead, socket,
                         [this, socket] { readServerRequest(socket); });
        QObject::connect(socket, &QTcpSocket::disconnected, socket, [this, socket] {
            incoming_.remove(socket);
            socket->deleteLater();
        });
    }
}

void LanSyncManager::readServerRequest(QTcpSocket* socket) {
    incoming_[socket].append(socket->readAll());
    QJsonObject message;
    QString error;
    if (!takeFrame(incoming_[socket], message, error)) {
        if (!error.isEmpty()) {
            socket->write(frame({{"ok", false}, {"error", error}}));
            socket->disconnectFromHost();
        }
        return;
    }
    if (message.value("code").toString() == pairingCode_) {
        peerLastSeen_[socket->peerAddress().toString()] = QDateTime::currentDateTime();
    }
    socket->write(frame(handleRequest(message)));
    socket->flush();
    socket->disconnectFromHost();
}

QJsonObject LanSyncManager::handleRequest(const QJsonObject& requestObject) {
    if (requestObject.value("code").toString() != pairingCode_) {
        return {{"ok", false}, {"error", "Incorrect pairing code."}};
    }
    const auto action = requestObject.value("action").toString();
    if (action == "join") {
        return {{"ok", true}, {"revision", QString::number(revision_)},
                {"snapshot", createSnapshot()}};
    }
    if (action == "pull") {
        const auto clientRevision = requestObject.value("revision").toVariant().toULongLong();
        const bool changed = clientRevision != revision_;
        QJsonObject response{{"ok", true}, {"changed", changed},
                             {"revision", QString::number(revision_)}};
        if (changed) response.insert("snapshot", createSnapshot());
        return response;
    }
    if (action == "push") {
        const auto clientRevision = requestObject.value("revision").toVariant().toULongLong();
        if (clientRevision != revision_) {
            return {{"ok", false}, {"error", "The workspace changed on the host. Your view was refreshed; please try the action again."},
                    {"revision", QString::number(revision_)}, {"snapshot", createSnapshot()}};
        }
        QString error;
        if (!applySnapshot(requestObject.value("snapshot").toObject(), error)) {
            return {{"ok", false}, {"error", error}};
        }
        ++revision_;
        if (remoteCommitHandler_) remoteCommitHandler_();
        return {{"ok", true}, {"revision", QString::number(revision_)}};
    }
    return {{"ok", false}, {"error", "Unknown TeamSync network request."}};
}

QJsonObject LanSyncManager::createSnapshot() const {
    QJsonObject files;
    const auto root = native(root_);
    const QString dataRoot = QDir(root).filePath("data");
    QDir dataDirectory(dataRoot);
    for (const auto& name : dataDirectory.entryList({"*.dat"}, QDir::Files, QDir::Name)) {
        QFile file(dataDirectory.filePath(name));
        if (file.open(QIODevice::ReadOnly)) files.insert("data/" + name, QString::fromLatin1(file.readAll().toBase64()));
    }
    const auto addTree = [&files, &root](const QString& folderName) {
        const QString treeRoot = QDir(root).filePath(folderName);
        QDirIterator iterator(treeRoot, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            const QString absolute = iterator.next();
            QFile file(absolute);
            if (!file.open(QIODevice::ReadOnly)) continue;
            const QString relative = folderName + '/' +
                QDir(treeRoot).relativeFilePath(absolute).replace('\\', '/');
            files.insert(relative, QString::fromLatin1(file.readAll().toBase64()));
        }
    };
    addTree("task_attachments");
    addTree("shared_files_storage");
    return {{"files", files}};
}

bool LanSyncManager::applySnapshot(const QJsonObject& snapshot, QString& error) const {
    const auto files = snapshot.value("files").toObject();
    QHash<QString, QByteArray> decoded;
    for (auto iterator = files.begin(); iterator != files.end(); ++iterator) {
        const auto relative = normalizedRelative(iterator.key());
        if (!safeRelativePath(relative)) {
            error = "The host sent an unsafe workspace path.";
            return false;
        }
        decoded.insert(relative, QByteArray::fromBase64(iterator.value().toString().toLatin1()));
    }

    const QString root = native(root_);
    QDir().mkpath(QDir(root).filePath("data"));
    const QString attachments = QDir(root).filePath("task_attachments");
    const QString sharedStorage = QDir(root).filePath("shared_files_storage");
    QDir().mkpath(attachments);
    QDir().mkpath(sharedStorage);

    QDir dataDirectory(QDir(root).filePath("data"));
    for (auto iterator = decoded.cbegin(); iterator != decoded.cend(); ++iterator) {
        const QString target = QDir(root).filePath(iterator.key());
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {
            error = "Could not create a synchronized workspace folder.";
            return false;
        }
        QSaveFile output(target);
        if (!output.open(QIODevice::WriteOnly) || output.write(iterator.value()) != iterator.value().size() || !output.commit()) {
            error = "Could not safely write synchronized file: " + iterator.key();
            return false;
        }
    }
    for (const auto& existing : dataDirectory.entryList({"*.dat"}, QDir::Files)) {
        if (!decoded.contains("data/" + existing)) dataDirectory.remove(existing);
    }
    QDirIterator existingAttachments(attachments, QDir::Files, QDirIterator::Subdirectories);
    while (existingAttachments.hasNext()) {
        const QString absolute = existingAttachments.next();
        const QString relative = "task_attachments/" +
            QDir(attachments).relativeFilePath(absolute).replace('\\', '/');
        if (!decoded.contains(relative)) QFile::remove(absolute);
    }
    QDirIterator existingSharedFiles(sharedStorage, QDir::Files, QDirIterator::Subdirectories);
    while (existingSharedFiles.hasNext()) {
        const QString absolute = existingSharedFiles.next();
        const QString relative = "shared_files_storage/" +
            QDir(sharedStorage).relativeFilePath(absolute).replace('\\', '/');
        if (!decoded.contains(relative)) QFile::remove(absolute);
    }
    return true;
}

bool LanSyncManager::request(const QJsonObject& message, QJsonObject& response, QString& error) const {
    QTcpSocket socket;
    socket.connectToHost(hostAddress_, port_);
    if (!socket.waitForConnected(5000)) {
        error = "Could not connect to " + hostAddress_ + ':' + QString::number(port_) + ". " + socket.errorString();
        return false;
    }
    const auto packet = frame(message);
    if (socket.write(packet) != packet.size() || !socket.waitForBytesWritten(5000)) {
        error = "Could not send the TeamSync network request.";
        return false;
    }
    QByteArray buffer;
    while (socket.state() != QAbstractSocket::UnconnectedState || socket.bytesAvailable() > 0) {
        if (socket.bytesAvailable() == 0 && !socket.waitForReadyRead(10000)) {
            if (socket.state() == QAbstractSocket::UnconnectedState) break;
            error = "The TeamSync host did not respond in time.";
            return false;
        }
        buffer.append(socket.readAll());
        QString frameError;
        if (takeFrame(buffer, response, frameError)) return true;
        if (!frameError.isEmpty()) { error = frameError; return false; }
    }
    error = "The TeamSync host closed the connection before sending a complete response.";
    return false;
}

QByteArray LanSyncManager::frame(const QJsonObject& object) {
    const auto payload = QJsonDocument(object).toJson(QJsonDocument::Compact);
    QByteArray result(4, Qt::Uninitialized);
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()), result.data());
    result.append(payload);
    return result;
}

bool LanSyncManager::takeFrame(QByteArray& buffer, QJsonObject& object, QString& error) {
    if (buffer.size() < 4) return false;
    const auto length = qFromBigEndian<quint32>(buffer.constData());
    if (length > maxFrameSize) {
        error = "The TeamSync network message is too large.";
        return false;
    }
    if (buffer.size() < static_cast<int>(length + 4)) return false;
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(buffer.mid(4, length), &parseError);
    buffer.remove(0, static_cast<int>(length + 4));
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = "The TeamSync network message is invalid.";
        return false;
    }
    object = document.object();
    return true;
}

bool LanSyncManager::safeRelativePath(const QString& input) {
    const auto path = normalizedRelative(input);
    if (path.isEmpty() || path.startsWith('/') || path.contains(':')) return false;
    const auto parts = path.split('/', Qt::SkipEmptyParts);
    if (parts.contains("..")) return false;
    if (path.startsWith("data/")) return parts.size() == 2 && path.endsWith(".dat");
    if (path.startsWith("task_attachments/")) return parts.size() >= 3;
    return path.startsWith("shared_files_storage/") && parts.size() >= 2;
}

} // namespace teamsync
