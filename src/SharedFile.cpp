#include "SharedFile.h"

#include "Exceptions.h"
#include "Utils.h"

#include <algorithm>
#include <cctype>
#include <ostream>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace teamsync {

int SharedFile::nextId_ = 30001;

SharedFile::SharedFile(const int id, const int teamId, const int uploaderId,
                       std::string fileName, std::filesystem::path localPath,
                       std::string description, std::string dateShared,
                       const std::uintmax_t recordedSize)
    : id_(id), teamId_(teamId), uploaderId_(uploaderId), fileName_(util::trim(fileName)),
      localPath_(std::move(localPath)), description_(std::move(description)),
      dateShared_(std::move(dateShared)), recordedSize_(recordedSize) {
    if (fileName_.empty()) throw ValidationError("Shared file name cannot be empty.");
    if (localPath_.empty()) throw ValidationError("Shared file path cannot be empty.");
    observeId(id_);
}

bool SharedFile::exists() const {
    std::error_code error;
    return std::filesystem::is_regular_file(localPath_, error) && !error;
}

std::uintmax_t SharedFile::currentSize() const {
    std::error_code error;
    const auto size = std::filesystem::file_size(localPath_, error);
    return error ? 0 : size;
}

bool SharedFile::isExecutable() const {
    std::string extension = localPath_.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return extension == ".exe" || extension == ".com" || extension == ".bat" ||
           extension == ".cmd" || extension == ".msi" || extension == ".ps1" ||
           extension == ".scr";
}

bool SharedFile::openWithDefaultApplication() const {
    if (!exists()) return false;
#ifdef _WIN32
    const auto result = ShellExecuteW(nullptr, L"open", localPath_.wstring().c_str(), nullptr,
                                      nullptr, SW_SHOWNORMAL);
    return reinterpret_cast<std::intptr_t>(result) > 32;
#else
    // Kept deliberately conservative: avoid constructing a shell command from a file path.
    return false;
#endif
}

int SharedFile::generateId() { return nextId_++; }
void SharedFile::observeId(const int id) { nextId_ = std::max(nextId_, id + 1); }

std::ostream& operator<<(std::ostream& out, const SharedFile& file) {
    return out << '[' << file.id_ << "] " << file.fileName_ << " | "
               << file.localPath_.string() << " | " << file.recordedSize_ << " bytes | "
               << (file.exists() ? "Available" : "Missing");
}

} // namespace teamsync
