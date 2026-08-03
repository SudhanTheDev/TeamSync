#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <string>

namespace teamsync {

class SharedFile {
public:
    SharedFile() = default;
    SharedFile(int id, int teamId, int uploaderId, std::string fileName,
               std::filesystem::path localPath, std::string description,
               std::string dateShared, std::uintmax_t recordedSize);

    int id() const { return id_; }
    int teamId() const { return teamId_; }
    int uploaderId() const { return uploaderId_; }
    const std::string& fileName() const { return fileName_; }
    const std::filesystem::path& localPath() const { return localPath_; }
    const std::string& description() const { return description_; }
    const std::string& dateShared() const { return dateShared_; }
    std::uintmax_t recordedSize() const { return recordedSize_; }

    bool exists() const;
    std::uintmax_t currentSize() const;
    bool isExecutable() const;
    bool openWithDefaultApplication() const;
    static int generateId();
    static void observeId(int id);
    friend std::ostream& operator<<(std::ostream& out, const SharedFile& file);

private:
    int id_ = 0;
    int teamId_ = 0;
    int uploaderId_ = 0;
    std::string fileName_;
    std::filesystem::path localPath_;
    std::string description_;
    std::string dateShared_;
    std::uintmax_t recordedSize_ = 0;
    static int nextId_;
};

} // namespace teamsync
