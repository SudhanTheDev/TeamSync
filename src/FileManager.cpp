#include "FileManager.h"

#include "Exceptions.h"
#include "Utils.h"

#include <fstream>
#include <sstream>
#include <system_error>

namespace teamsync {
namespace {

std::string field(const std::string& value) { return util::encodeField(value); }

std::vector<std::string> decodedList(const std::string& value) {
    std::vector<std::string> result;
    if (value.empty()) return result;
    for (const auto& item : util::split(value, ';')) result.push_back(util::decodeField(item));
    return result;
}

std::string record(const std::vector<std::string>& fields) {
    std::ostringstream out;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i) out << '|';
        out << fields[i];
    }
    return out.str();
}

} // namespace

FileManager::FileManager(std::filesystem::path applicationRoot)
    : root_(std::filesystem::absolute(std::move(applicationRoot))),
      dataDirectory_(root_ / "data"), reportsDirectory_(root_ / "reports") {
    std::error_code error;
    std::filesystem::create_directories(dataDirectory_, error);
    if (error) throw PersistenceError("Cannot create data directory: " + error.message());
    std::filesystem::create_directories(reportsDirectory_, error);
    if (error) throw PersistenceError("Cannot create reports directory: " + error.message());
}

std::vector<std::string> FileManager::readLines(const std::string& fileName) {
    const auto path = dataDirectory_ / fileName;
    if (!std::filesystem::exists(path)) return {};
    std::ifstream input(path);
    if (!input) throw PersistenceError("Cannot read data file: " + path.string());
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
    }
    if (!input.eof()) throw PersistenceError("Read failure in data file: " + path.string());
    return lines;
}

void FileManager::writeLinesSafely(const std::string& fileName,
                                   const std::vector<std::string>& lines) const {
    const auto target = dataDirectory_ / fileName;
    auto temporary = target;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) throw PersistenceError("Cannot write temporary file: " + temporary.string());
        for (const auto& line : lines) output << line << '\n';
        output.flush();
        if (!output) throw PersistenceError("Write failure in: " + temporary.string());
    }
    std::error_code error;
#ifdef _WIN32
    auto backup = target;
    backup += ".bak";
    std::filesystem::remove(backup, error);
    error.clear();
    const bool hadOriginal = std::filesystem::exists(target);
    if (hadOriginal) {
        std::filesystem::rename(target, backup, error);
        if (error) {
            std::filesystem::remove(temporary);
            throw PersistenceError("Cannot prepare safe replacement for " + target.string() +
                                   ": " + error.message());
        }
    }
    std::filesystem::rename(temporary, target, error);
    if (error && hadOriginal) {
        std::error_code restoreError;
        std::filesystem::rename(backup, target, restoreError);
    }
    if (!error) std::filesystem::remove(backup);
#else
    std::filesystem::rename(temporary, target, error);
#endif
    if (error) {
        std::filesystem::remove(temporary);
        throw PersistenceError("Cannot replace " + target.string() + ": " + error.message());
    }
}

void FileManager::recordCorruption(const std::string& fileName, const std::size_t line,
                                   const std::string& reason) {
    warnings_.push_back(fileName + " line " + std::to_string(line) + " skipped: " + reason);
}

std::vector<std::unique_ptr<User>> FileManager::loadUsers() {
    std::vector<std::unique_ptr<User>> result;
    const auto lines = readLines("users.dat");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        try {
            const auto f = util::split(lines[i], '|');
            if (f.size() != 8 && f.size() != 9) throw ValidationError("expected 8 or 9 fields");
            const auto securityHashes = f.size() == 9 ? decodedList(f[8]) : std::vector<std::string>{};
            result.push_back(makeUser(userRoleFromString(util::decodeField(f[1])), std::stoi(f[0]),
                util::decodeField(f[2]), util::decodeField(f[3]), util::decodeField(f[4]),
                util::decodeField(f[5]), decodedList(f[6]), util::decodeField(f[7]),
                securityHashes));
        } catch (const std::exception& error) { recordCorruption("users.dat", i + 1, error.what()); }
    }
    return result;
}

std::vector<Team> FileManager::loadTeams() {
    std::vector<Team> result;
    const auto lines = readLines("teams.dat");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        try {
            const auto f = util::split(lines[i], '|');
            if (f.size() != 9) throw ValidationError("expected 9 fields");
            result.emplace_back(std::stoi(f[0]), util::decodeField(f[1]), util::decodeField(f[2]),
                std::stoi(f[3]), util::parseIntList(f[4]), util::parseIntList(f[5]),
                util::decodeField(f[6]), util::decodeField(f[7]),
                teamStatusFromString(util::decodeField(f[8])));
        } catch (const std::exception& error) { recordCorruption("teams.dat", i + 1, error.what()); }
    }
    return result;
}

std::vector<Project> FileManager::loadProjects() {
    std::vector<Project> result;
    const auto lines = readLines("projects.dat");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        try {
            const auto f = util::split(lines[i], '|');
            if (f.size() != 9) throw ValidationError("expected 9 fields");
            result.emplace_back(std::stoi(f[0]), std::stoi(f[1]), util::decodeField(f[2]),
                util::decodeField(f[3]), Date::fromString(f[4]), Date::fromString(f[5]),
                projectStatusFromString(util::decodeField(f[6])), std::stoi(f[7]),
                util::parseIntList(f[8]));
        } catch (const std::exception& error) { recordCorruption("projects.dat", i + 1, error.what()); }
    }
    return result;
}

std::vector<std::unique_ptr<Task>> FileManager::loadTasks() {
    std::vector<std::unique_ptr<Task>> result;
    const auto lines = readLines("tasks.dat");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        try {
            const auto f = util::split(lines[i], '|');
            const bool v2 = f.size() == 17 && f[0] == "v2";
            if (!v2 && f.size() != 16) throw ValidationError("expected a legacy or v2 task record");
            const std::size_t offset = v2 ? 1 : 0;
            std::optional<Date> completed;
            if (!f[12 + offset].empty()) completed = Date::fromString(f[12 + offset]);
            auto status = taskStatusFromString(util::decodeField(f[9 + offset]));
            if (!v2) {
                const int oldProgress = std::stoi(f[13]);
                if (oldProgress >= 100) status = TaskStatus::Completed;
                else if (oldProgress > 0 || status == TaskStatus::InProgress || status == TaskStatus::Overdue)
                    status = TaskStatus::InProgress;
                else status = TaskStatus::Pending;
            }
            result.push_back(makeTask(taskTypeFromString(util::decodeField(f[offset])), std::stoi(f[1 + offset]),
                std::stoi(f[2 + offset]), std::stoi(f[3 + offset]), util::decodeField(f[4 + offset]),
                util::decodeField(f[5 + offset]), std::stoi(f[6 + offset]), std::stoi(f[7 + offset]),
                taskPriorityFromString(util::decodeField(f[8 + offset])), status,
                Date::fromString(f[10 + offset]), Date::fromString(f[11 + offset]), completed,
                util::decodeField(f[14]), decodedList(f[15]),
                v2 ? util::decodeField(f[16]) : std::string{}));
        } catch (const std::exception& error) { recordCorruption("tasks.dat", i + 1, error.what()); }
    }
    return result;
}

std::vector<TaskAttachment> FileManager::loadTaskAttachments() {
    std::vector<TaskAttachment> result;
    const auto lines = readLines("task_attachments.dat");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        try {
            const auto f = util::split(lines[i], '|');
            if (f.size() != 9) throw ValidationError("expected 9 fields");
            result.emplace_back(std::stoi(f[0]), std::stoi(f[1]), std::stoi(f[2]),
                std::stoi(f[3]), util::decodeField(f[4]),
                std::filesystem::path(util::decodeField(f[5])), util::decodeField(f[6]),
                util::decodeField(f[7]), std::stoull(f[8]));
        } catch (const std::exception& error) {
            recordCorruption("task_attachments.dat", i + 1, error.what());
        }
    }
    return result;
}

std::vector<Message> FileManager::loadMessages() {
    std::vector<Message> result;
    const auto lines = readLines("messages.dat");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        try {
            const auto f = util::split(lines[i], '|');
            if (f.size() != 5) throw ValidationError("expected 5 fields");
            result.emplace_back(std::stoi(f[0]), std::stoi(f[1]), std::stoi(f[2]),
                                util::decodeField(f[3]), util::decodeField(f[4]));
        } catch (const std::exception& error) { recordCorruption("messages.dat", i + 1, error.what()); }
    }
    return result;
}

std::vector<SharedFile> FileManager::loadSharedFiles() {
    std::vector<SharedFile> result;
    const auto lines = readLines("shared_files.dat");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        try {
            const auto f = util::split(lines[i], '|');
            if (f.size() != 8) throw ValidationError("expected 8 fields");
            auto storedPath = std::filesystem::path(util::decodeField(f[4]));
            if (storedPath.is_relative()) storedPath = root_ / storedPath;
            result.emplace_back(std::stoi(f[0]), std::stoi(f[1]), std::stoi(f[2]),
                util::decodeField(f[3]), storedPath,
                util::decodeField(f[5]), util::decodeField(f[6]), std::stoull(f[7]));
        } catch (const std::exception& error) { recordCorruption("shared_files.dat", i + 1, error.what()); }
    }
    return result;
}

std::vector<Notification> FileManager::loadNotifications() {
    std::vector<Notification> result;
    const auto lines = readLines("notifications.dat");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        try {
            const auto f = util::split(lines[i], '|');
            if (f.size() != 5) throw ValidationError("expected 5 fields");
            result.emplace_back(std::stoi(f[0]), std::stoi(f[1]), util::decodeField(f[2]),
                                util::decodeField(f[3]), f[4] == "1");
        } catch (const std::exception& error) { recordCorruption("notifications.dat", i + 1, error.what()); }
    }
    return result;
}

std::vector<ActivityLog> FileManager::loadActivities() {
    std::vector<ActivityLog> result;
    const auto lines = readLines("activity_logs.dat");
    for (std::size_t i = 0; i < lines.size(); ++i) {
        try {
            const auto f = util::split(lines[i], '|');
            if (f.size() != 5) throw ValidationError("expected 5 fields");
            result.emplace_back(std::stoi(f[0]), std::stoi(f[1]), std::stoi(f[2]),
                                util::decodeField(f[3]), util::decodeField(f[4]));
        } catch (const std::exception& error) { recordCorruption("activity_logs.dat", i + 1, error.what()); }
    }
    return result;
}

void FileManager::saveUsers(const std::vector<std::unique_ptr<User>>& records) const {
    std::vector<std::string> lines;
    for (const auto& value : records) lines.push_back(record({std::to_string(value->id()),
        field(toString(value->role())), field(value->fullName()), field(value->username()),
        field(value->passwordHash()), field(value->email()), util::join(value->skills(), ';'),
        field(value->dateCreated()), util::join(value->securityAnswerHashes(), ';')}));
    writeLinesSafely("users.dat", lines);
}

void FileManager::saveTeams(const std::vector<Team>& records) const {
    std::vector<std::string> lines;
    for (const auto& value : records) lines.push_back(record({std::to_string(value.id()),
        field(value.name()), field(value.description()), std::to_string(value.creatorId()),
        util::joinInts(value.memberIds()), util::joinInts(value.adminIds()), field(value.dateCreated()),
        field(value.joinCode()), field(toString(value.status()))}));
    writeLinesSafely("teams.dat", lines);
}

void FileManager::saveProjects(const std::vector<Project>& records) const {
    std::vector<std::string> lines;
    for (const auto& value : records) lines.push_back(record({std::to_string(value.id()),
        std::to_string(value.teamId()), field(value.title()), field(value.description()),
        value.startDate().toString(), value.dueDate().toString(), field(toString(value.status())),
        std::to_string(value.createdBy()), util::joinInts(value.taskIds())}));
    writeLinesSafely("projects.dat", lines);
}

void FileManager::saveTasks(const std::vector<std::unique_ptr<Task>>& records) const {
    std::vector<std::string> lines;
    for (const auto& value : records) lines.push_back(record({"v2", field(toString(value->type())),
        std::to_string(value->id()), std::to_string(value->projectId()),
        std::to_string(value->teamId()), field(value->title()), field(value->description()),
        std::to_string(value->createdBy()), std::to_string(value->assignedUserId()),
        field(toString(value->priority())), field(toString(value->status())),
        value->creationDate().toString(), value->dueDate().toString(),
        value->completionDate() ? value->completionDate()->toString() : "",
        field(value->requiredSkill()), util::join(value->comments(), ';'),
        field(value->completionNote())}));
    writeLinesSafely("tasks.dat", lines);
}

void FileManager::saveTaskAttachments(const std::vector<TaskAttachment>& records) const {
    std::vector<std::string> lines;
    for (const auto& value : records) lines.push_back(record({std::to_string(value.id()),
        std::to_string(value.taskId()), std::to_string(value.teamId()),
        std::to_string(value.uploaderId()), field(value.fileName()),
        field(value.relativePath().generic_string()), field(value.description()),
        field(value.dateAdded()), std::to_string(value.size())}));
    writeLinesSafely("task_attachments.dat", lines);
}

void FileManager::saveMessages(const std::vector<Message>& records) const {
    std::vector<std::string> lines;
    for (const auto& value : records) lines.push_back(record({std::to_string(value.id()),
        std::to_string(value.teamId()), std::to_string(value.senderId()), field(value.text()),
        field(value.dateTime())}));
    writeLinesSafely("messages.dat", lines);
}

void FileManager::saveSharedFiles(const std::vector<SharedFile>& records) const {
    std::vector<std::string> lines;
    for (const auto& value : records) {
        auto storedPath = value.localPath();
        const auto relative = storedPath.lexically_normal().lexically_relative(root_.lexically_normal());
        if (!relative.empty() && *relative.begin() != "..") storedPath = relative;
        lines.push_back(record({std::to_string(value.id()),
        std::to_string(value.teamId()), std::to_string(value.uploaderId()), field(value.fileName()),
        field(storedPath.generic_string()), field(value.description()), field(value.dateShared()),
        std::to_string(value.recordedSize())}));
    }
    writeLinesSafely("shared_files.dat", lines);
}

void FileManager::saveNotifications(const std::vector<Notification>& records) const {
    std::vector<std::string> lines;
    for (const auto& value : records) lines.push_back(record({std::to_string(value.id()),
        std::to_string(value.userId()), field(value.text()), field(value.dateTime()),
        value.isRead() ? "1" : "0"}));
    writeLinesSafely("notifications.dat", lines);
}

void FileManager::saveActivities(const std::vector<ActivityLog>& records) const {
    std::vector<std::string> lines;
    for (const auto& value : records) lines.push_back(record({std::to_string(value.id()),
        std::to_string(value.userId()), std::to_string(value.teamId()), field(value.action()),
        field(value.dateTime())}));
    writeLinesSafely("activity_logs.dat", lines);
}

} // namespace teamsync
