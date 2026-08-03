#pragma once

#include <QByteArray>
#include <QHash>
#include <QDateTime>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <functional>

class QJsonObject;
class QTcpServer;
class QTcpSocket;

namespace teamsync {

class LanSyncManager {
public:
    enum class Mode { Local, Host, Client };

    explicit LanSyncManager(std::filesystem::path workspaceRoot);
    ~LanSyncManager();

    bool startHost(quint16 port, QString& error, const QString& requestedCode = {});
    bool joinHost(const QString& address, quint16 port, const QString& code, QString& error);
    bool pull(bool& changed, QString& error);
    bool push(QString& error);
    void noteLocalChange();
    void setRemoteCommitHandler(std::function<void()> handler);

    Mode mode() const { return mode_; }
    quint16 port() const { return port_; }
    quint64 revision() const { return revision_; }
    const QString& pairingCode() const { return pairingCode_; }
    const QString& hostAddress() const { return hostAddress_; }
    QStringList localAddresses() const;
    QStringList activePeerAddresses() const;
    int activePeerCount() const { return activePeerAddresses().size(); }
    QString statusText() const;
    QString lastSyncText() const;
    QByteArray workspaceFingerprint() const;

private:
    std::filesystem::path root_;
    Mode mode_ = Mode::Local;
    QTcpServer* server_ = nullptr;
    QHash<QTcpSocket*, QByteArray> incoming_;
    QString hostAddress_;
    quint16 port_ = 45454;
    QString pairingCode_;
    quint64 revision_ = 1;
    QHash<QString, QDateTime> peerLastSeen_;
    QDateTime lastSync_;
    std::function<void()> remoteCommitHandler_;

    void acceptConnections();
    void readServerRequest(QTcpSocket* socket);
    QJsonObject handleRequest(const QJsonObject& request);
    QJsonObject createSnapshot() const;
    bool applySnapshot(const QJsonObject& snapshot, QString& error) const;
    bool request(const QJsonObject& message, QJsonObject& response, QString& error) const;
    static QByteArray frame(const QJsonObject& object);
    static bool takeFrame(QByteArray& buffer, QJsonObject& object, QString& error);
    static bool safeRelativePath(const QString& path);
};

} // namespace teamsync
