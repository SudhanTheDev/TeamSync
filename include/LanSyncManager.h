#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include "ConsoleLanSync.h"

#include <filesystem>
#include <functional>
#include <memory>

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
    bool sync(bool& changed, QString& error);
    bool adoptHostAfterPreservingLocal(bool& changed, QString& error);
    bool hasPendingChanges() const;
    ConsoleLanSync::TransferProgress progress() const;
    void cancelTransfer();
    void executeLocalMutation(const std::function<void()>& action);
    void noteLocalChange();
    void setRemoteCommitHandler(std::function<void()> handler);

    Mode mode() const { return mode_; }
    quint16 port() const;
    quint64 revision() const;
    const QString& pairingCode() const { return pairingCode_; }
    const QString& hostAddress() const { return hostAddress_; }
    QStringList localAddresses() const;
    QStringList activePeerAddresses() const;
    int activePeerCount() const { return activePeerAddresses().size(); }
    QString statusText() const;
    QString lastSyncText() const;
    QByteArray workspaceFingerprint() const;

    struct NearbyWorkspace {
        QString name;
        QString address;
        quint16 port = 45454;
    };
    static QList<NearbyWorkspace> discoverNearby(int timeoutMs = 1600);

private:
    std::filesystem::path root_;
    Mode mode_ = Mode::Local;
    std::unique_ptr<ConsoleLanSync> engine_;
    QString hostAddress_;
    QString pairingCode_;
};

} // namespace teamsync
