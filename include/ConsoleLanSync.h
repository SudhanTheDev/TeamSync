#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace teamsync {

// Both desktop interfaces use this transport and its versioned LAN protocol.
class ConsoleLanSync {
public:
    struct NearbyWorkspace { std::string name, address; std::uint16_t port = 0; };
    struct TransferProgress {
        std::string phase, file;
        std::uint64_t transferred = 0, total = 0;
        double bytesPerSecond = 0;
        bool active = false, cancelled = false, resumable = false;
    };
    explicit ConsoleLanSync(std::filesystem::path workspaceRoot);
    ~ConsoleLanSync();
    ConsoleLanSync(const ConsoleLanSync&) = delete;
    ConsoleLanSync& operator=(const ConsoleLanSync&) = delete;
    bool host(std::uint16_t port, std::string& pairingCode, std::string& error,
              const std::string& requestedCode = {});
    bool join(const std::string& address, std::uint16_t port,
              const std::string& pairingCode, std::string& error);
    void disconnect();
    bool sync(bool& changed, std::string& error);
    bool pull(bool& changed, std::string& error);
    bool push(std::string& error);
    bool adoptHostAfterPreservingLocal(bool& changed, std::string& error);
    void noteLocalChange();
    void withWorkspaceLock(const std::function<void()>& action);
    void executeLocalMutation(const std::function<void()>& action);
    bool hasPendingChanges() const;
    bool isActive() const noexcept;
    bool isHost() const noexcept;
    std::uint64_t revision() const;
    std::uint16_t port() const;
    std::string hostAddress() const;
    std::string pairingCode() const;
    std::string lastSyncText() const;
    std::vector<std::string> peerAddresses() const;
    std::string status() const;
    std::string localAddresses() const;
    std::uint64_t fingerprint() const;
    static std::vector<NearbyWorkspace> discoverNearby(int timeoutMs = 1600);
    void setRemoteChangeHandler(std::function<void()> handler);
    void setProgressHandler(std::function<void(const TransferProgress&)> handler);
    TransferProgress progress() const;
    void cancelTransfer();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace teamsync
