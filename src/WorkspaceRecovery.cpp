#include "WorkspaceRecovery.h"

#include "Exceptions.h"
#include "Utils.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace teamsync {
namespace {
namespace fs = std::filesystem;
constexpr std::uint64_t maxSnapshotBytes = 64ULL * 1024 * 1024 * 1024;
constexpr std::size_t maxSnapshotFiles = 100000;
struct Entry { fs::path relative; std::uint64_t bytes; std::uint64_t hash; };

#ifdef _WIN32
// Recovery is an optimistic, read-only observer of a live workspace. Normal
// iostream/copy_file readers can deny DELETE sharing on Windows and prevent the
// application's atomic rename saves. These handles explicitly permit all three
// share modes; the before/after hashes still reject a concurrently changed copy.
class SharedReadHandle {
public:
    explicit SharedReadHandle(const fs::path& path)
        : handle_(CreateFileW(path.c_str(), GENERIC_READ,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr)) {
        if (handle_ == INVALID_HANDLE_VALUE) {
            const auto nativeError = GetLastError();
            throw PersistenceError("Cannot read recovery source: " + path.u8string() +
                                   " (Windows error " + std::to_string(nativeError) + ")");
        }
    }
    ~SharedReadHandle() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
    SharedReadHandle(const SharedReadHandle&) = delete;
    SharedReadHandle& operator=(const SharedReadHandle&) = delete;
    DWORD read(char* destination, const DWORD capacity) {
        DWORD count = 0;
        if (!ReadFile(handle_, destination, capacity, &count, nullptr))
            throw PersistenceError("Recovery source could not be read completely (Windows error " +
                                   std::to_string(GetLastError()) + ").");
        return count;
    }
private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};
#endif

std::string timestamp(const bool id = false) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw = std::chrono::system_clock::to_time_t(now);
    std::tm value{};
#ifdef _WIN32
    localtime_s(&value, &raw);
#else
    localtime_r(&raw, &value);
#endif
    std::ostringstream text;
    text << std::put_time(&value, id ? "%Y%m%d-%H%M%S" : "%Y-%m-%d %H:%M:%S");
    if (id) {
        static std::atomic<unsigned long> serial{0};
        text << '-' << std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count()
             << '-' << ++serial;
    }
    return text.str();
}

bool safeId(const std::string& value) {
    return value.size() >= 15 && value.size() < 96 &&
        std::all_of(value.begin(), value.end(), [](unsigned char c) { return (c >= '0' && c <= '9') || c == '-'; });
}

void rejectLinks(const fs::path& value) {
    fs::path checked;
    for (const auto& part : fs::absolute(value).lexically_normal()) {
        checked /= part;
        std::error_code error;
        const auto status = fs::symlink_status(checked, error);
        if (!error && fs::is_symlink(status)) throw PersistenceError("Recovery refuses symbolic-link paths: " + checked.string());
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(checked.c_str());
        if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw PersistenceError("Recovery refuses junction or reparse-point paths: " + checked.string());
#endif
    }
}

bool managed(const fs::path& relative) {
    if (relative.empty() || relative.is_absolute()) return false;
    for (const auto& part : relative) {
        const auto text = part.generic_string();
        if (part == ".." || part == "." || text.find(':') != std::string::npos ||
            std::any_of(text.begin(), text.end(), [](unsigned char c) { return c < 32; })) return false;
    }
    const auto first = relative.begin()->string();
    if (first == "data") return std::distance(relative.begin(), relative.end()) == 2 && relative.extension() == ".dat";
    return (first == "task_attachments" || first == "shared_files_storage") &&
        std::distance(relative.begin(), relative.end()) >= 2;
}

std::uint64_t fileHash(const fs::path& path) {
    rejectLinks(path);
#ifdef _WIN32
    SharedReadHandle input(path);
#else
    std::ifstream input(path, std::ios::binary);
    if (!input) throw PersistenceError("Cannot read recovery source: " + path.string());
#endif
    std::uint64_t hash = 1469598103934665603ULL;
    char buffer[65536];
#ifdef _WIN32
    std::uint64_t total = 0;
    while (const DWORD count = input.read(buffer, sizeof buffer)) {
        if (count > maxSnapshotBytes - total) throw PersistenceError("Recovery source exceeds the safe size limit.");
        total += count;
        for (DWORD i = 0; i < count; ++i) { hash ^= static_cast<unsigned char>(buffer[i]); hash *= 1099511628211ULL; }
    }
#else
    while (input) {
        input.read(buffer, sizeof buffer);
        for (std::streamsize i = 0; i < input.gcount(); ++i) { hash ^= static_cast<unsigned char>(buffer[i]); hash *= 1099511628211ULL; }
    }
    if (!input.eof()) throw PersistenceError("Recovery source could not be read completely.");
#endif
    return hash;
}

std::vector<Entry> inventory(const fs::path& root) {
    rejectLinks(root);
    std::vector<Entry> result;
    std::uint64_t total = 0;
    auto add = [&](const fs::path& path) {
        rejectLinks(path);
        if (!fs::is_regular_file(path)) throw PersistenceError("Recovery only supports regular files.");
        if (!managed(path.lexically_relative(root))) throw PersistenceError("Unsafe managed file path.");
        const auto size = fs::file_size(path);
        if (size > maxSnapshotBytes - total || result.size() >= maxSnapshotFiles)
            throw PersistenceError("Recovery snapshot exceeds its safe 64 GiB / 100,000-file limit.");
        total += size;
        result.push_back({path.lexically_relative(root), size, fileHash(path)});
    };
    if (fs::exists(root / "data")) {
        rejectLinks(root / "data");
        for (const auto& entry : fs::directory_iterator(root / "data")) {
            if (entry.path().extension() == ".dat") add(entry.path());
        }
    }
    for (const auto& folder : {"task_attachments", "shared_files_storage"}) {
        const auto directory = root / folder;
        if (!fs::exists(directory)) continue;
        rejectLinks(directory);
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            rejectLinks(entry.path());
            if (!entry.is_directory()) add(entry.path());
        }
    }
    std::sort(result.begin(), result.end(), [](const Entry& a, const Entry& b) { return a.relative.generic_string() < b.relative.generic_string(); });
    return result;
}

std::string digest(const std::vector<Entry>& entries) {
    std::ostringstream result;
    for (const auto& entry : entries) result << entry.relative.generic_string() << ':' << entry.bytes << ':' << entry.hash << '\n';
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char c : result.str()) { hash ^= c; hash *= 1099511628211ULL; }
    return std::to_string(hash);
}

void copyChecked(const fs::path& source, const fs::path& destination, const Entry& entry) {
    rejectLinks(source); rejectLinks(destination);
    fs::create_directories(destination.parent_path());
    if (fs::exists(destination)) throw PersistenceError("Recovery staging destination already exists.");
#ifdef _WIN32
    SharedReadHandle input(source);
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) throw PersistenceError("Cannot create recovery staging file: " + destination.u8string());
    char buffer[65536];
    std::uint64_t copied = 0;
    while (const DWORD count = input.read(buffer, sizeof buffer)) {
        if (count > entry.bytes - copied)
            throw PersistenceError("The workspace changed while copying recovery history. Retry after saving finishes.");
        output.write(buffer, static_cast<std::streamsize>(count));
        if (!output) throw PersistenceError("Cannot write recovery staging file. Check free disk space.");
        copied += count;
    }
    output.close();
    if (!output || copied != entry.bytes)
        throw PersistenceError("Recovery source changed or its staging copy could not be completed.");
#else
    fs::copy_file(source, destination, fs::copy_options::none);
#endif
    if (fs::file_size(destination) != entry.bytes || fileHash(destination) != entry.hash ||
        fs::file_size(source) != entry.bytes || fileHash(source) != entry.hash)
        throw PersistenceError("The workspace changed while creating recovery history. Retry when saving has finished.");
}

void writeManifest(const fs::path& directory, const BackupInfo& info, const std::vector<Entry>& entries) {
    std::ofstream output(directory / "manifest.txt", std::ios::binary | std::ios::trunc);
    output << "TeamSync-Recovery-v1\n" << util::encodeField(info.reason) << '\n' << info.createdAt << '\n'
           << info.automatic << '\n' << digest(entries) << '\n';
    for (const auto& entry : entries) output << entry.bytes << '\t' << entry.hash << '\t'
        << util::encodeField(entry.relative.generic_string()) << '\n';
    output.close();
    if (!output) throw PersistenceError("Could not save recovery manifest.");
}

std::vector<Entry> readManifest(const fs::path& directory, BackupInfo& info) {
    rejectLinks(directory);
    if (fs::file_size(directory / "manifest.txt") > 64 * 1024 * 1024)
        throw PersistenceError("Recovery manifest exceeds the safe size limit.");
    std::ifstream input(directory / "manifest.txt", std::ios::binary);
    std::string header, automatic, expectedDigest;
    if (!std::getline(input, header) || header != "TeamSync-Recovery-v1" ||
        !std::getline(input, info.reason) || !std::getline(input, info.createdAt) ||
        !std::getline(input, automatic) || !std::getline(input, expectedDigest))
        throw PersistenceError("This recovery snapshot has an invalid manifest.");
    info.reason = util::decodeField(info.reason);
    info.automatic = automatic == "1";
    std::vector<Entry> entries;
    std::set<std::string> names;
    std::string line;
    while (std::getline(input, line)) {
        std::istringstream row(line);
        Entry entry;
        std::string name;
        if (!(row >> entry.bytes >> entry.hash) || row.get() != '\t' || !std::getline(row, name))
            throw PersistenceError("Malformed recovery file record.");
        entry.relative = fs::u8path(util::decodeField(name));
        if (!managed(entry.relative) || !names.insert(util::lower(entry.relative.generic_string())).second ||
            entry.bytes > maxSnapshotBytes - info.bytes || entries.size() >= maxSnapshotFiles)
            throw PersistenceError("Unsafe or oversized recovery file record.");
        info.bytes += entry.bytes;
        entries.push_back(entry);
    }
    if (!input.eof() || digest(entries) != expectedDigest) throw PersistenceError("Recovery manifest integrity check failed.");
    info.fileCount = entries.size();
    return entries;
}

// Only removes an exact, generated recovery-history child, never the workspace.
void removeOwnedTree(const fs::path& recoveryRoot, const fs::path& target) {
    rejectLinks(recoveryRoot); rejectLinks(target);
    const auto relative = target.lexically_relative(recoveryRoot);
    if (relative.empty() || relative.is_absolute() || std::distance(relative.begin(), relative.end()) != 2 ||
        !safeId(target.filename().string())) throw PersistenceError("Refusing an unsafe recovery cleanup target.");
    const auto category = relative.begin()->string();
    if (category != "backups" && category != "staging") throw PersistenceError("Invalid recovery cleanup category.");
    if (!fs::exists(target)) return;
    std::vector<fs::path> directories;
    for (const auto& entry : fs::recursive_directory_iterator(target)) {
        rejectLinks(entry.path());
        if (entry.is_directory()) directories.push_back(entry.path());
        else fs::remove(entry.path());
    }
    std::sort(directories.begin(), directories.end(), [](const auto& a, const auto& b) { return a.native().size() > b.native().size(); });
    for (const auto& path : directories) fs::remove(path);
    fs::remove(target);
}
} // namespace

WorkspaceRecovery::WorkspaceRecovery(fs::path workspaceRoot)
    : root_(fs::absolute(std::move(workspaceRoot)).lexically_normal()) {
    rejectLinks(root_);
    if (root_ == root_.root_path()) throw ValidationError("A workspace folder is required for recovery.");
}

std::vector<BackupInfo> WorkspaceRecovery::list(const std::string& category) const {
    std::vector<BackupInfo> result;
    const auto directory = root_ / ".teamsync-recovery" / category;
    rejectLinks(directory);
    if (!fs::exists(directory)) return result;
    for (const auto& item : fs::directory_iterator(directory)) {
        if (!item.is_directory() || item.is_symlink() || !safeId(item.path().filename().string())) continue;
        try {
            BackupInfo info;
            info.id = item.path().filename().string();
            readManifest(item.path(), info);
            result.push_back(std::move(info));
        } catch (const std::exception&) { /* Incomplete/corrupt history is never offered for restore. */ }
    }
    std::sort(result.begin(), result.end(), [](const BackupInfo& a, const BackupInfo& b) { return a.id > b.id; });
    return result;
}

std::vector<BackupInfo> WorkspaceRecovery::listBackups() const { return list("backups"); }
std::vector<BackupInfo> WorkspaceRecovery::listConflicts() const { return list("conflicts"); }
std::string WorkspaceRecovery::createBackup(const std::string& reason, const bool automatic) { return createSnapshot("backups", reason, automatic); }
std::string WorkspaceRecovery::saveConflict(const std::string& reason) { return createSnapshot("conflicts", reason, false); }

std::string WorkspaceRecovery::createSnapshot(const std::string& category, const std::string& reason, const bool automatic) {
    const auto entries = inventory(root_);
    const auto recoveryRoot = root_ / ".teamsync-recovery";
    rejectLinks(recoveryRoot);
    const auto now = timestamp();
    if (automatic) {
        for (const auto& existing : listBackups()) {
            if (!existing.automatic || existing.createdAt.substr(0, 10) != now.substr(0, 10)) continue;
            BackupInfo check;
            if (digest(readManifest(recoveryRoot / "backups" / existing.id, check)) == digest(entries)) return existing.id;
        }
    }
    BackupInfo info;
    info.id = timestamp(true); info.createdAt = now; info.reason = reason; info.automatic = automatic;
    const auto staging = recoveryRoot / "staging" / info.id;
    rejectLinks(staging);
    rejectLinks(recoveryRoot / category);
    fs::create_directories(staging / "workspace");
    try {
        for (const auto& entry : entries) copyChecked(root_ / entry.relative, staging / "workspace" / entry.relative, entry);
        if (digest(inventory(root_)) != digest(entries))
            throw PersistenceError("The workspace changed while creating recovery history. No mixed-version backup was saved; retry after saving finishes.");
        writeManifest(staging, info, entries);
        fs::create_directories(recoveryRoot / category);
        fs::rename(staging, recoveryRoot / category / info.id);
    } catch (...) {
        try { removeOwnedTree(recoveryRoot, staging); } catch (...) {}
        throw;
    }
    if (automatic) {
        std::size_t retained = 0;
        for (const auto& existing : listBackups()) {
            if (existing.automatic && ++retained > 14) {
                try { removeOwnedTree(recoveryRoot, recoveryRoot / "backups" / existing.id); } catch (...) {}
            }
        }
    }
    return info.id;
}

void WorkspaceRecovery::restoreBackup(const std::string& id) { restore("backups", id); }
void WorkspaceRecovery::restoreConflict(const std::string& id) { restore("conflicts", id); }

void WorkspaceRecovery::restore(const std::string& category, const std::string& id) {
    if (!safeId(id)) throw ValidationError("Invalid recovery snapshot ID.");
    const auto recoveryRoot = root_ / ".teamsync-recovery";
    const auto source = recoveryRoot / category / id;
    BackupInfo info;
    const auto entries = readManifest(source, info);
    const auto current = inventory(root_); // Refuse unsafe live paths before any replacement.
    const auto staging = recoveryRoot / "staging" / timestamp(true);
    rejectLinks(staging);
    fs::create_directories(staging / "incoming");
    std::vector<fs::path> movedOriginals, installed;
    try {
        for (const auto& entry : entries) copyChecked(source / "workspace" / entry.relative, staging / "incoming" / entry.relative, entry);
        createBackup("Before restoring " + id, false);
        // Per-file data moves preserve unrelated files in the data directory.
        // Attachment folders are managed in their entirety, including empty folders.
        std::set<fs::path> targets;
        for (const auto& entry : current) if (*entry.relative.begin() == "data") targets.insert(entry.relative);
        for (const auto& entry : entries) if (*entry.relative.begin() == "data") targets.insert(entry.relative);
        targets.insert("task_attachments"); targets.insert("shared_files_storage");
        for (const auto& relative : targets) {
            rejectLinks(root_ / relative);
            if (fs::exists(root_ / relative)) {
                fs::create_directories((staging / "original" / relative).parent_path());
                fs::rename(root_ / relative, staging / "original" / relative);
                movedOriginals.push_back(relative);
            }
            if (fs::exists(staging / "incoming" / relative)) {
                fs::create_directories((root_ / relative).parent_path());
                fs::rename(staging / "incoming" / relative, root_ / relative);
                installed.push_back(relative);
            }
        }
    } catch (...) {
        // Roll back by moving, not deleting, every installed file/folder.
        for (auto it = installed.rbegin(); it != installed.rend(); ++it) {
            std::error_code error;
            fs::create_directories((staging / "failed" / *it).parent_path(), error);
            fs::rename(root_ / *it, staging / "failed" / *it, error);
        }
        for (auto it = movedOriginals.rbegin(); it != movedOriginals.rend(); ++it) {
            std::error_code error;
            fs::create_directories((root_ / *it).parent_path(), error);
            fs::rename(staging / "original" / *it, root_ / *it, error);
        }
        // Staging is retained if anything failed, so no original data is lost.
        throw PersistenceError("Restore did not complete. Original files were rolled back; recovery staging is retained at " + staging.string());
    }
    // A permanent pre-restore backup now contains every original managed file.
    try { removeOwnedTree(recoveryRoot, staging); } catch (...) {}
}

} // namespace teamsync
