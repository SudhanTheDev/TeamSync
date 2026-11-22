#include "LanSyncManager.h"

#include <QHostAddress>
#include <QNetworkInterface>
#include <algorithm>

namespace teamsync {
namespace {
std::string utf8(const QString& value) { const auto bytes = value.toUtf8(); return {bytes.constData(), static_cast<std::size_t>(bytes.size())}; }
QString text(const std::string& value) { return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())); }
}

// Both interfaces intentionally use the same transport, recovery state and
// versioned wire protocol. This adapter only translates presentation types.
LanSyncManager::LanSyncManager(std::filesystem::path workspaceRoot)
    : root_(std::filesystem::absolute(std::move(workspaceRoot))), engine_(std::make_unique<ConsoleLanSync>(root_)) {}
LanSyncManager::~LanSyncManager() = default;

bool LanSyncManager::startHost(quint16 portValue, QString& error, const QString& requestedCode) {
    if (!requestedCode.isEmpty() && (requestedCode.size() != 6 ||
        std::any_of(requestedCode.cbegin(), requestedCode.cend(), [](QChar c) { return c < '0' || c > '9'; }))) {
        error = "The pairing code must contain exactly six digits."; return false;
    }
    std::string code, problem;
    const bool ok = engine_->host(portValue, code, problem, utf8(requestedCode));
    error = text(problem);
    if (ok) { mode_ = Mode::Host; pairingCode_ = text(code); }
    return ok;
}
bool LanSyncManager::joinHost(const QString& address, quint16 portValue, const QString& code, QString& error) {
    const auto pairing = code.trimmed();
    if (pairing.size() != 6 || std::any_of(pairing.cbegin(), pairing.cend(), [](QChar c) { return c < '0' || c > '9'; })) {
        error = "Enter the host's six-digit pairing code."; return false;
    }
    std::string problem;
    const bool ok = engine_->join(utf8(address.trimmed()), portValue, utf8(code.trimmed()), problem);
    error = text(problem);
    if (ok) { mode_ = Mode::Client; hostAddress_ = address.trimmed(); pairingCode_ = code.trimmed(); }
    return ok;
}
bool LanSyncManager::pull(bool& changed, QString& error) {
    std::string problem; const bool ok = engine_->pull(changed, problem); error = text(problem); return ok;
}
bool LanSyncManager::push(QString& error) {
    std::string problem; const bool ok = engine_->push(problem); error = text(problem); return ok;
}
bool LanSyncManager::sync(bool& changed, QString& error) {
    std::string problem; const bool ok = engine_->sync(changed, problem); error = text(problem); return ok;
}
bool LanSyncManager::adoptHostAfterPreservingLocal(bool& changed, QString& error) {
    std::string problem; const bool ok = engine_->adoptHostAfterPreservingLocal(changed, problem); error = text(problem); return ok;
}
void LanSyncManager::noteLocalChange() { engine_->noteLocalChange(); }
void LanSyncManager::setRemoteCommitHandler(std::function<void()> handler) { engine_->setRemoteChangeHandler(std::move(handler)); }
void LanSyncManager::executeLocalMutation(const std::function<void()>& action) { engine_->executeLocalMutation(action); }
bool LanSyncManager::hasPendingChanges() const { return engine_->hasPendingChanges(); }
ConsoleLanSync::TransferProgress LanSyncManager::progress() const { return engine_->progress(); }
void LanSyncManager::cancelTransfer() { engine_->cancelTransfer(); }
quint16 LanSyncManager::port() const { return engine_->port(); }
quint64 LanSyncManager::revision() const { return engine_->revision(); }
QStringList LanSyncManager::localAddresses() const {
    QStringList addresses;
    for (const auto& address : QNetworkInterface::allAddresses())
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) addresses << address.toString();
    addresses.removeDuplicates(); return addresses;
}
QStringList LanSyncManager::activePeerAddresses() const {
    QStringList peers; for (const auto& value : engine_->peerAddresses()) peers << text(value); return peers;
}
QString LanSyncManager::statusText() const { return text(engine_->status()); }
QString LanSyncManager::lastSyncText() const {
    if (mode_ == Mode::Local) return "Not connected";
    if (mode_ == Mode::Host) return "Hosting revision " + QString::number(revision());
    const auto value = text(engine_->lastSyncText());
    return value.isEmpty() ? "Waiting for first synchronization" : value;
}
QByteArray LanSyncManager::workspaceFingerprint() const { return QByteArray::number(engine_->fingerprint(), 16); }
QList<LanSyncManager::NearbyWorkspace> LanSyncManager::discoverNearby(int timeoutMs) {
    QList<NearbyWorkspace> result;
    for (const auto& value : ConsoleLanSync::discoverNearby(timeoutMs)) result.append({text(value.name), text(value.address), value.port});
    return result;
}
} // namespace teamsync
