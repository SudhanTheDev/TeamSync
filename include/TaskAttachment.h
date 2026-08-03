#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace teamsync {

class TaskAttachment {
public:
    TaskAttachment() = default;
    TaskAttachment(int id, int taskId, int teamId, int uploaderId, std::string fileName,
                   std::filesystem::path relativePath, std::string description,
                   std::string dateAdded, std::uintmax_t size);

    int id() const { return id_; }
    int taskId() const { return taskId_; }
    int teamId() const { return teamId_; }
    int uploaderId() const { return uploaderId_; }
    const std::string& fileName() const { return fileName_; }
    const std::filesystem::path& relativePath() const { return relativePath_; }
    const std::string& description() const { return description_; }
    const std::string& dateAdded() const { return dateAdded_; }
    std::uintmax_t size() const { return size_; }

    static int generateId();
    static void observeId(int id);

private:
    int id_ = 0;
    int taskId_ = 0;
    int teamId_ = 0;
    int uploaderId_ = 0;
    std::string fileName_;
    std::filesystem::path relativePath_;
    std::string description_;
    std::string dateAdded_;
    std::uintmax_t size_ = 0;
    static int nextId_;
};

} // namespace teamsync
