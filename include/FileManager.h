#pragma once

#include "ActivityLog.h"
#include "Message.h"
#include "Notification.h"
#include "Project.h"
#include "SharedFile.h"
#include "Task.h"
#include "TaskAttachment.h"
#include "Team.h"
#include "User.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace teamsync {

class FileManager {
public:
    explicit FileManager(std::filesystem::path applicationRoot = ".");

    const std::filesystem::path& root() const { return root_; }
    const std::filesystem::path& dataDirectory() const { return dataDirectory_; }
    const std::filesystem::path& reportsDirectory() const { return reportsDirectory_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    void clearWarnings() { warnings_.clear(); }

    std::vector<std::unique_ptr<User>> loadUsers();
    std::vector<Team> loadTeams();
    std::vector<Project> loadProjects();
    std::vector<std::unique_ptr<Task>> loadTasks();
    std::vector<TaskAttachment> loadTaskAttachments();
    std::vector<Message> loadMessages();
    std::vector<SharedFile> loadSharedFiles();
    std::vector<Notification> loadNotifications();
    std::vector<ActivityLog> loadActivities();

    void saveUsers(const std::vector<std::unique_ptr<User>>& records) const;
    void saveTeams(const std::vector<Team>& records) const;
    void saveProjects(const std::vector<Project>& records) const;
    void saveTasks(const std::vector<std::unique_ptr<Task>>& records) const;
    void saveTaskAttachments(const std::vector<TaskAttachment>& records) const;
    void saveMessages(const std::vector<Message>& records) const;
    void saveSharedFiles(const std::vector<SharedFile>& records) const;
    void saveNotifications(const std::vector<Notification>& records) const;
    void saveActivities(const std::vector<ActivityLog>& records) const;

private:
    std::filesystem::path root_;
    std::filesystem::path dataDirectory_;
    std::filesystem::path reportsDirectory_;
    std::vector<std::string> warnings_;

    std::vector<std::string> readLines(const std::string& fileName);
    void writeLinesSafely(const std::string& fileName,
                          const std::vector<std::string>& lines) const;
    void recordCorruption(const std::string& fileName, std::size_t line,
                          const std::string& reason);
};

} // namespace teamsync
