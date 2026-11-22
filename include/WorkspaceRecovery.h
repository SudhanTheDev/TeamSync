#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace teamsync {

struct BackupInfo {
    std::string id;
    std::string reason;
    std::string createdAt;
    std::uint64_t bytes = 0;
    std::uint64_t fileCount = 0;
    bool automatic = false;
};

// Local-only recovery history. Only data/*.dat, task_attachments and
// shared_files_storage are included. Settings, caches and secrets are excluded.
class WorkspaceRecovery {
public:
    explicit WorkspaceRecovery(std::filesystem::path workspaceRoot);
    std::string createBackup(const std::string& reason, bool automatic = false);
    std::vector<BackupInfo> listBackups() const;
    // Validates the complete snapshot before changing live files; creates a
    // permanent pre-restore backup, and rolls back a failed replacement.
    void restoreBackup(const std::string& id);
    std::string saveConflict(const std::string& reason);
    std::vector<BackupInfo> listConflicts() const;
    void restoreConflict(const std::string& id);
    const std::filesystem::path& root() const { return root_; }
    std::filesystem::path recoveryDirectory() const { return root_ / ".teamsync-recovery"; }

private:
    std::filesystem::path root_;
    std::string createSnapshot(const std::string& category, const std::string& reason,
                               bool automatic);
    std::vector<BackupInfo> list(const std::string& category) const;
    void restore(const std::string& category, const std::string& id);
};

} // namespace teamsync
