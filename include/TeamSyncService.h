#pragma once

#include "Authentication.h"
#include "FileManager.h"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace teamsync {

struct MemberRecommendation {
    int userId = 0;
    std::string memberName;
    int skillMatchPercent = 0;
    int activeTasks = 0;
    int completedTasks = 0;
    double completionRate = 0.0;
    int score = 0;
};

struct ContributionStats {
    int userId = 0;
    std::string memberName;
    int assignedTasks = 0;
    int completedTasks = 0;
    int completedOnTime = 0;
    int overdueTasks = 0;
    int activityCount = 0;
    double completionRate = 0.0;
    double onTimeRate = 0.0;
    double score = 0.0;
};

class TeamSyncService {
public:
    explicit TeamSyncService(const std::filesystem::path& applicationRoot = ".");
    ~TeamSyncService() = default;

    User& registerUser(const std::string& fullName, const std::string& username,
                       const std::string& password, const std::string& confirmation,
                       const std::string& email, UserRole role,
                       const std::vector<std::string>& skills,
                       const std::vector<std::string>& securityAnswers);
    User& login(const std::string& username, const std::string& password);
    void logout(int userId);
    void editProfile(int userId, const std::string& fullName, const std::string& email,
                     const std::vector<std::string>& skills);
    void changePassword(int userId, const std::string& oldPassword,
                        const std::string& newPassword, const std::string& confirmation);
    void resetPassword(const std::string& username,
                       const std::vector<std::string>& securityAnswers,
                       const std::string& newPassword, const std::string& confirmation);
    void setSecurityAnswers(int userId, const std::string& currentPassword,
                            const std::vector<std::string>& securityAnswers);

    User& user(int id);
    const User& user(int id) const;
    User* findUserByUsername(const std::string& username);
    std::vector<const User*> searchUsers(const std::string& query) const;

    Team& createTeam(int creatorId, const std::string& name, const std::string& description);
    Team& joinTeam(int userId, const std::string& idOrCode);
    void leaveTeam(int userId, int teamId);
    void removeMember(int actingUserId, int teamId, int memberId);
    void promoteMember(int actingUserId, int teamId, int memberId);
    Team& team(int id);
    const Team& team(int id) const;
    std::vector<Team*> teamsForUser(int userId);
    std::vector<const Team*> teamsForUser(int userId) const;
    std::vector<const Team*> searchTeams(const std::string& query) const;

    Project& createProject(int creatorId, int teamId, const std::string& title,
                           const std::string& description, const Date& startDate,
                           const Date& dueDate);
    void editProject(int actingUserId, int projectId, const std::string& title,
                     const std::string& description, const Date& startDate,
                     const Date& dueDate);
    void archiveProject(int actingUserId, int projectId);
    Project& project(int id);
    const Project& project(int id) const;
    std::vector<Project*> projectsForTeam(int teamId);
    std::vector<const Project*> projectsForUser(int userId) const;
    std::vector<const Project*> searchProjects(int userId, const std::string& query) const;

    Task& createTask(int creatorId, int projectId, TaskType type, const std::string& title,
                     const std::string& description, int assignedUserId,
                     TaskPriority priority, const Date& dueDate,
                     const std::string& requiredSkill);
    void editTask(int actingUserId, int taskId, const std::string& title,
                  const std::string& description, TaskPriority priority,
                  const Date& dueDate, const std::string& requiredSkill);
    void deleteTask(int actingUserId, int taskId);
    void assignTask(int actingUserId, int taskId, int memberId);
    void assignTask(int actingUserId, int taskId, int memberId, const std::string& note);
    void startTask(int actingUserId, int taskId);
    void markTaskComplete(int actingUserId, int taskId,
                          const std::string& completionNote = {},
                          const std::vector<std::filesystem::path>& files = {});
    void reopenTask(int actingUserId, int taskId);
    void addTaskComment(int actingUserId, int taskId, const std::string& comment);
    Task& searchTask(int taskId);
    Task* searchTask(const std::string& title);
    const Task& task(int id) const;
    std::vector<Task*> tasksForUser(int userId);
    std::vector<const Task*> tasksForUser(int userId) const;
    std::vector<const Task*> tasksForTeam(int teamId) const;
    std::vector<const Task*> searchTasks(int userId, const std::string& query) const;
    std::vector<MemberRecommendation> recommendMembers(int taskId) const;
    std::vector<const TaskAttachment*> attachmentsForTask(int userId, int taskId) const;
    std::filesystem::path attachmentPath(int userId, int attachmentId) const;

    Message& sendMessage(int senderId, int teamId, const std::string& text);
    void deleteMessage(int requestingUserId, int messageId);
    std::vector<Message> messagesForTeam(int userId, int teamId,
                                         const std::string& query = {}) const;

    SharedFile& shareFile(int uploaderId, int teamId, const std::filesystem::path& path,
                          const std::string& displayName, const std::string& description);
    void removeSharedFile(int requestingUserId, int fileId);
    SharedFile& sharedFile(int id);
    std::vector<const SharedFile*> filesForTeam(int userId, int teamId,
                                                const std::string& query = {}) const;

    std::vector<Notification*> notificationsForUser(int userId, bool unreadOnly = false);
    void markAllNotificationsRead(int userId);
    std::vector<const ActivityLog*> activitiesForUser(int userId, int teamId = 0) const;
    std::vector<ContributionStats> contributionsForTeam(int requestingUserId, int teamId) const;
    void recordReportGenerated(int userId, int teamId, const std::string& reportName);

    void saveAll() const;
    void reload();
    const FileManager& fileManager() const { return fileManager_; }

    const std::vector<std::unique_ptr<User>>& users() const { return users_; }
    const std::vector<Team>& teams() const { return teams_; }
    const std::vector<Project>& projects() const { return projects_; }
    const std::vector<std::unique_ptr<Task>>& tasks() const { return tasks_; }
    const std::vector<TaskAttachment>& taskAttachments() const { return taskAttachments_; }
    const std::vector<Message>& messages() const { return messages_; }
    const std::vector<SharedFile>& sharedFiles() const { return sharedFiles_; }
    const std::vector<Notification>& notifications() const { return notifications_; }
    const std::vector<ActivityLog>& activities() const { return activities_; }

private:
    FileManager fileManager_;
    std::vector<std::unique_ptr<User>> users_;
    std::vector<Team> teams_;
    std::vector<Project> projects_;
    std::vector<std::unique_ptr<Task>> tasks_;
    std::vector<TaskAttachment> taskAttachments_;
    std::vector<Message> messages_;
    std::vector<SharedFile> sharedFiles_;
    std::vector<Notification> notifications_;
    std::vector<ActivityLog> activities_;
    std::unordered_map<int, User*> userIndex_;
    Authentication authentication_;

    void loadAll();
    void validateLoadedData();
    void requireTeamMembership(int userId, int teamId) const;
    void requireTeamAdmin(int userId, int teamId) const;
    bool canUpdateTask(int userId, const Task& task) const;
    void refreshProjectStatus(int projectId);
    void refreshAllProjectStatuses();
    void addNotification(int userId, const std::string& text);
    void log(int userId, int teamId, const std::string& action);
};

} // namespace teamsync
