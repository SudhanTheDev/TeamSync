#include "TeamSyncService.h"

#include "Exceptions.h"
#include "Utils.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <sstream>

namespace teamsync {
namespace {

std::string safeAttachmentName(const std::string& name) {
    std::string result;
    result.reserve(name.size());
    for (const unsigned char c : name) {
        if (std::isalnum(c) || c == '.' || c == '-' || c == '_') result.push_back(static_cast<char>(c));
        else result.push_back('_');
    }
    if (result.empty() || result == "." || result == "..") result = "attachment";
    return result;
}

} // namespace

TeamSyncService::TeamSyncService(const std::filesystem::path& applicationRoot)
    : fileManager_(applicationRoot), authentication_(users_) {
    loadAll();
}

void TeamSyncService::loadAll() {
    users_ = fileManager_.loadUsers();
    teams_ = fileManager_.loadTeams();
    projects_ = fileManager_.loadProjects();
    tasks_ = fileManager_.loadTasks();
    taskAttachments_ = fileManager_.loadTaskAttachments();
    messages_ = fileManager_.loadMessages();
    sharedFiles_ = fileManager_.loadSharedFiles();
    notifications_ = fileManager_.loadNotifications();
    activities_ = fileManager_.loadActivities();
    validateLoadedData();
    refreshAllProjectStatuses();
}

void TeamSyncService::validateLoadedData() {
    auto removeDuplicateValues = [](auto& records) {
        std::set<int> ids;
        records.erase(std::remove_if(records.begin(), records.end(), [&ids](const auto& record) {
            return !ids.insert(record.id()).second;
        }), records.end());
    };
    auto removeDuplicatePointers = [](auto& records) {
        std::set<int> ids;
        records.erase(std::remove_if(records.begin(), records.end(), [&ids](const auto& record) {
            return !record || !ids.insert(record->id()).second;
        }), records.end());
    };
    removeDuplicatePointers(users_);
    removeDuplicateValues(teams_);
    removeDuplicateValues(projects_);
    removeDuplicatePointers(tasks_);
    removeDuplicateValues(taskAttachments_);
    removeDuplicateValues(messages_);
    removeDuplicateValues(sharedFiles_);
    removeDuplicateValues(notifications_);
    removeDuplicateValues(activities_);
    userIndex_.clear();
    for (const auto& value : users_) userIndex_.emplace(value->id(), value.get());
}

User& TeamSyncService::registerUser(const std::string& fullName, const std::string& username,
                                    const std::string& password, const std::string& confirmation,
                                    const std::string& email, const UserRole role,
                                    const std::vector<std::string>& skills,
                                    const std::vector<std::string>& securityAnswers) {
    User& created = authentication_.registerUser(fullName, username, password, confirmation,
                                                 email, role, skills, securityAnswers);
    userIndex_[created.id()] = &created;
    log(created.id(), 0, "User registered");
    fileManager_.saveUsers(users_);
    fileManager_.saveActivities(activities_);
    return created;
}

User& TeamSyncService::login(const std::string& username, const std::string& password) {
    User& result = authentication_.login(username, password);
    log(result.id(), 0, "Login");
    fileManager_.saveActivities(activities_);
    return result;
}

void TeamSyncService::logout(const int userId) {
    user(userId);
    log(userId, 0, "Logout");
    fileManager_.saveActivities(activities_);
}

void TeamSyncService::editProfile(const int userId, const std::string& fullName,
                                  const std::string& email,
                                  const std::vector<std::string>& skills) {
    auto& value = user(userId);
    authentication_.editProfile(value, fullName, email, skills);
    log(userId, 0, "Profile updated");
    fileManager_.saveUsers(users_);
    fileManager_.saveActivities(activities_);
}

void TeamSyncService::changePassword(const int userId, const std::string& oldPassword,
                                     const std::string& newPassword,
                                     const std::string& confirmation) {
    auto& value = user(userId);
    authentication_.changePassword(value, oldPassword, newPassword, confirmation);
    log(userId, 0, "Password changed");
    fileManager_.saveUsers(users_);
    fileManager_.saveActivities(activities_);
}

void TeamSyncService::resetPassword(const std::string& username,
                                    const std::vector<std::string>& securityAnswers,
                                    const std::string& newPassword,
                                    const std::string& confirmation) {
    auto* value = findUserByUsername(username);
    if (!value) throw AuthenticationError("Username was not found.");
    authentication_.resetPassword(*value, securityAnswers, newPassword, confirmation);
    log(value->id(), 0, "Password recovered using security questions");
    fileManager_.saveUsers(users_);
    fileManager_.saveActivities(activities_);
}

void TeamSyncService::setSecurityAnswers(const int userId, const std::string& currentPassword,
                                         const std::vector<std::string>& securityAnswers) {
    auto& value = user(userId);
    authentication_.setSecurityAnswers(value, currentPassword, securityAnswers);
    log(userId, 0, "Security questions configured");
    fileManager_.saveUsers(users_);
    fileManager_.saveActivities(activities_);
}

User& TeamSyncService::user(const int id) {
    const auto found = userIndex_.find(id);
    if (found == userIndex_.end()) throw NotFoundError("User ID " + std::to_string(id) + " was not found.");
    return *found->second;
}

const User& TeamSyncService::user(const int id) const {
    const auto found = userIndex_.find(id);
    if (found == userIndex_.end()) throw NotFoundError("User ID " + std::to_string(id) + " was not found.");
    return *found->second;
}

User* TeamSyncService::findUserByUsername(const std::string& username) {
    const auto normalized = util::lower(util::trim(username));
    const auto found = std::find_if(users_.begin(), users_.end(), [&normalized](const auto& value) {
        return util::lower(value->username()) == normalized;
    });
    return found == users_.end() ? nullptr : found->get();
}

std::vector<const User*> TeamSyncService::searchUsers(const std::string& query) const {
    std::vector<const User*> result;
    for (const auto& value : users_) {
        if (util::containsIgnoreCase(value->fullName(), query) ||
            util::containsIgnoreCase(value->username(), query)) result.push_back(value.get());
    }
    return result;
}

Team& TeamSyncService::createTeam(const int creatorId, const std::string& name,
                                  const std::string& description) {
    user(creatorId);
    const int id = Team::generateId();
    teams_.emplace_back(id, name, description, creatorId, std::vector<int>{creatorId},
                        std::vector<int>{creatorId}, Date::today().toString(),
                        util::generateJoinCode(id, name));
    log(creatorId, id, "Team created: " + teams_.back().name());
    fileManager_.saveTeams(teams_);
    fileManager_.saveActivities(activities_);
    return teams_.back();
}

Team& TeamSyncService::joinTeam(const int userId, const std::string& idOrCode) {
    user(userId);
    Team* found = nullptr;
    try {
        std::size_t used = 0;
        const int id = std::stoi(util::trim(idOrCode), &used);
        if (used == util::trim(idOrCode).size()) found = &team(id);
    } catch (...) {
        // Input can also be a join code.
    }
    if (!found) {
        const auto normalized = util::lower(util::trim(idOrCode));
        const auto position = std::find_if(teams_.begin(), teams_.end(), [&normalized](const Team& value) {
            return util::lower(value.joinCode()) == normalized;
        });
        if (position == teams_.end()) throw NotFoundError("No team matches that ID or join code.");
        found = &*position;
    }
    if (found->status() != TeamStatus::Active) throw ValidationError("Archived teams cannot accept members.");
    found->addMember(userId);
    addNotification(userId, "You joined team " + found->name() + '.');
    log(userId, found->id(), "Joined team");
    fileManager_.saveTeams(teams_);
    fileManager_.saveNotifications(notifications_);
    fileManager_.saveActivities(activities_);
    return *found;
}

void TeamSyncService::leaveTeam(const int userId, const int teamId) {
    auto& value = team(teamId);
    value.removeMember(userId);
    log(userId, teamId, "Left team");
    fileManager_.saveTeams(teams_);
    fileManager_.saveActivities(activities_);
}

void TeamSyncService::removeMember(const int actingUserId, const int teamId, const int memberId) {
    requireTeamAdmin(actingUserId, teamId);
    if (actingUserId == memberId) throw ValidationError("Use the leave-team option to remove yourself.");
    auto& value = team(teamId);
    value.removeMember(memberId);
    addNotification(memberId, "You were removed from team " + value.name() + '.');
    log(actingUserId, teamId, "Removed member " + std::to_string(memberId));
    fileManager_.saveTeams(teams_);
    fileManager_.saveNotifications(notifications_);
    fileManager_.saveActivities(activities_);
}

void TeamSyncService::promoteMember(const int actingUserId, const int teamId, const int memberId) {
    requireTeamAdmin(actingUserId, teamId);
    auto& value = team(teamId);
    value.addAdmin(memberId);
    addNotification(memberId, "You are now an admin of team " + value.name() + '.');
    log(actingUserId, teamId, "Promoted member " + std::to_string(memberId) + " to team admin");
    fileManager_.saveTeams(teams_);
    fileManager_.saveNotifications(notifications_);
    fileManager_.saveActivities(activities_);
}

Team& TeamSyncService::team(const int id) {
    const auto found = std::find_if(teams_.begin(), teams_.end(),
                                    [id](const Team& value) { return value.id() == id; });
    if (found == teams_.end()) throw NotFoundError("Team ID " + std::to_string(id) + " was not found.");
    return *found;
}

const Team& TeamSyncService::team(const int id) const {
    const auto found = std::find_if(teams_.begin(), teams_.end(),
                                    [id](const Team& value) { return value.id() == id; });
    if (found == teams_.end()) throw NotFoundError("Team ID " + std::to_string(id) + " was not found.");
    return *found;
}

std::vector<Team*> TeamSyncService::teamsForUser(const int userId) {
    std::vector<Team*> result;
    for (auto& value : teams_) if (value.hasMember(userId)) result.push_back(&value);
    return result;
}

std::vector<const Team*> TeamSyncService::teamsForUser(const int userId) const {
    std::vector<const Team*> result;
    for (const auto& value : teams_) if (value.hasMember(userId)) result.push_back(&value);
    return result;
}

std::vector<const Team*> TeamSyncService::searchTeams(const std::string& query) const {
    std::vector<const Team*> result;
    for (const auto& value : teams_) if (util::containsIgnoreCase(value.name(), query) ||
        util::containsIgnoreCase(value.description(), query)) result.push_back(&value);
    return result;
}

Project& TeamSyncService::createProject(const int creatorId, const int teamId,
                                        const std::string& title, const std::string& description,
                                        const Date& startDate, const Date& dueDate) {
    requireTeamAdmin(creatorId, teamId);
    projects_.emplace_back(Project::generateId(), teamId, title, description, startDate, dueDate,
                           ProjectStatus::NotStarted, creatorId);
    log(creatorId, teamId, "Project created: " + projects_.back().title());
    fileManager_.saveProjects(projects_);
    fileManager_.saveActivities(activities_);
    return projects_.back();
}

void TeamSyncService::editProject(const int actingUserId, const int projectId,
                                  const std::string& title, const std::string& description,
                                  const Date& startDate, const Date& dueDate) {
    auto& value = project(projectId);
    requireTeamAdmin(actingUserId, value.teamId());
    value.setTitle(title);
    value.setDescription(description);
    value.setDates(startDate, dueDate);
    log(actingUserId, value.teamId(), "Project updated: " + value.title());
    fileManager_.saveProjects(projects_);
    fileManager_.saveActivities(activities_);
}

void TeamSyncService::archiveProject(const int actingUserId, const int projectId) {
    auto& value = project(projectId);
    requireTeamAdmin(actingUserId, value.teamId());
    value.setStatus(ProjectStatus::Archived);
    log(actingUserId, value.teamId(), "Project archived: " + value.title());
    fileManager_.saveProjects(projects_);
    fileManager_.saveActivities(activities_);
}

Project& TeamSyncService::project(const int id) {
    const auto found = std::find_if(projects_.begin(), projects_.end(),
                                    [id](const Project& value) { return value.id() == id; });
    if (found == projects_.end()) throw NotFoundError("Project ID " + std::to_string(id) + " was not found.");
    return *found;
}

const Project& TeamSyncService::project(const int id) const {
    const auto found = std::find_if(projects_.begin(), projects_.end(),
                                    [id](const Project& value) { return value.id() == id; });
    if (found == projects_.end()) throw NotFoundError("Project ID " + std::to_string(id) + " was not found.");
    return *found;
}

std::vector<Project*> TeamSyncService::projectsForTeam(const int teamId) {
    std::vector<Project*> result;
    for (auto& value : projects_) if (value.teamId() == teamId) result.push_back(&value);
    return result;
}

std::vector<const Project*> TeamSyncService::projectsForUser(const int userId) const {
    std::vector<const Project*> result;
    for (const auto& value : projects_) if (team(value.teamId()).hasMember(userId)) result.push_back(&value);
    return result;
}

std::vector<const Project*> TeamSyncService::searchProjects(const int userId,
                                                            const std::string& query) const {
    std::vector<const Project*> result;
    for (const auto* value : projectsForUser(userId)) if (util::containsIgnoreCase(value->title(), query) ||
        util::containsIgnoreCase(value->description(), query)) result.push_back(value);
    return result;
}

Task& TeamSyncService::createTask(const int creatorId, const int projectId, const TaskType type,
                                  const std::string& title, const std::string& description,
                                  const int assignedUserId, const TaskPriority priority,
                                  const Date& dueDate, const std::string& requiredSkill) {
    auto& parent = project(projectId);
    requireTeamAdmin(creatorId, parent.teamId());
    requireTeamMembership(assignedUserId, parent.teamId());
    const int id = Task::generateId();
    tasks_.push_back(makeTask(type, id, projectId, parent.teamId(), title, description, creatorId,
                              assignedUserId, priority, TaskStatus::Pending, Date::today(), dueDate,
                              std::nullopt, requiredSkill));
    parent.addTask(id);
    refreshProjectStatus(projectId);
    addNotification(assignedUserId, "You were assigned task #" + std::to_string(id) + ": " + title);
    log(creatorId, parent.teamId(), "Task created and assigned: " + title);
    fileManager_.saveTasks(tasks_);
    fileManager_.saveProjects(projects_);
    fileManager_.saveNotifications(notifications_);
    fileManager_.saveActivities(activities_);
    return *tasks_.back();
}

void TeamSyncService::editTask(const int actingUserId, const int taskId, const std::string& title,
                               const std::string& description, const TaskPriority priority,
                               const Date& dueDate, const std::string& requiredSkill) {
    auto& value = searchTask(taskId);
    requireTeamAdmin(actingUserId, value.teamId());
    value.setTitle(title);
    value.setDescription(description);
    value.setPriority(priority);
    value.setDueDate(dueDate);
    value.setRequiredSkill(requiredSkill);
    log(actingUserId, value.teamId(), "Task edited: " + value.title());
    fileManager_.saveTasks(tasks_);
    fileManager_.saveActivities(activities_);
}

void TeamSyncService::deleteTask(const int actingUserId, const int taskId) {
    const auto found = std::find_if(tasks_.begin(), tasks_.end(),
                                    [taskId](const auto& value) { return value->id() == taskId; });
    if (found == tasks_.end()) throw NotFoundError("Task ID " + std::to_string(taskId) + " was not found.");
    requireTeamAdmin(actingUserId, (*found)->teamId());
    const int projectId = (*found)->projectId();
    const int teamId = (*found)->teamId();
    const std::string title = (*found)->title();
    project(projectId).removeTask(taskId);
    taskAttachments_.erase(std::remove_if(taskAttachments_.begin(), taskAttachments_.end(),
        [taskId](const TaskAttachment& value) { return value.taskId() == taskId; }), taskAttachments_.end());
    {
        std::error_code ignored;
        std::filesystem::remove_all(fileManager_.root() / "task_attachments" /
                                    std::to_string(taskId), ignored);
    }
    tasks_.erase(found);
    refreshProjectStatus(projectId);
    log(actingUserId, teamId, "Task deleted: " + title);
    fileManager_.saveTasks(tasks_);
    fileManager_.saveTaskAttachments(taskAttachments_);
    fileManager_.saveProjects(projects_);
    fileManager_.saveActivities(activities_);
}

void TeamSyncService::assignTask(const int actingUserId, const int taskId, const int memberId) {
    assignTask(actingUserId, taskId, memberId, {});
}

void TeamSyncService::assignTask(const int actingUserId, const int taskId, const int memberId,
                                 const std::string& note) {
    auto& value = searchTask(taskId);
    requireTeamAdmin(actingUserId, value.teamId());
    requireTeamMembership(memberId, value.teamId());
    value.assignTo(memberId);
    if (!util::trim(note).empty()) value.addComment("Assignment note: " + note);
    addNotification(memberId, "You were assigned task #" + std::to_string(taskId) + ": " + value.title());
    log(actingUserId, value.teamId(), "Task assigned to member " + std::to_string(memberId));
    fileManager_.saveTasks(tasks_);
    fileManager_.saveNotifications(notifications_);
    fileManager_.saveActivities(activities_);
}

void TeamSyncService::startTask(const int actingUserId, const int taskId) {
    auto& value = searchTask(taskId);
    if (!canUpdateTask(actingUserId, value)) throw AuthorizationError("Only members of this team may update this task.");
    value.start();
    refreshProjectStatus(value.projectId());
    log(actingUserId, value.teamId(), "Task started: " + value.title());
    fileManager_.saveTasks(tasks_);
    fileManager_.saveProjects(projects_);
    fileManager_.saveActivities(activities_);
}

void TeamSyncService::markTaskComplete(const int actingUserId, const int taskId,
                                       const std::string& completionNote,
                                       const std::vector<std::filesystem::path>& files) {
    auto& value = searchTask(taskId);
    if (!canUpdateTask(actingUserId, value)) throw AuthorizationError("Only members of this team may complete this task.");

    constexpr std::uintmax_t maxAttachmentSize = 100ULL * 1024ULL * 1024ULL;
    constexpr std::uintmax_t maxAttachmentBatchSize = 150ULL * 1024ULL * 1024ULL;
    struct PreparedFile { std::filesystem::path source; std::string name; std::uintmax_t size; };
    std::vector<PreparedFile> prepared;
    std::uintmax_t totalAttachmentSize = 0;
    for (const auto& input : files) {
        std::error_code error;
        const auto source = std::filesystem::absolute(input, error);
        if (error || !std::filesystem::is_regular_file(source, error) || error) {
            throw ValidationError("A selected completion file does not exist or is not a regular file: " + input.string());
        }
        const auto size = std::filesystem::file_size(source, error);
        if (error) throw ValidationError("A selected completion file could not be read: " + source.string());
        if (size > maxAttachmentSize) throw ValidationError("Completion files must be 100 MB or smaller: " + source.filename().string());
        totalAttachmentSize += size;
        if (totalAttachmentSize > maxAttachmentBatchSize) {
            throw ValidationError("Completion files must total 150 MB or less for reliable LAN synchronization.");
        }
        prepared.push_back({source, source.filename().string(), size});
    }

    std::vector<std::filesystem::path> copied;
    std::vector<int> addedIds;
    try {
        for (const auto& item : prepared) {
            const int id = TaskAttachment::generateId();
            const auto relative = std::filesystem::path("task_attachments") /
                                  std::to_string(taskId) /
                                  (std::to_string(id) + "_" + safeAttachmentName(item.name));
            const auto target = fileManager_.root() / relative;
            std::error_code error;
            std::filesystem::create_directories(target.parent_path(), error);
            if (error) throw PersistenceError("Cannot create the task attachment folder: " + error.message());
            std::filesystem::copy_file(item.source, target,
                                       std::filesystem::copy_options::overwrite_existing, error);
            if (error) throw PersistenceError("Cannot copy completion file: " + error.message());
            copied.push_back(target);
            addedIds.push_back(id);
            taskAttachments_.emplace_back(id, taskId, value.teamId(), actingUserId, item.name,
                                          relative, completionNote, util::timestampNow(), item.size);
        }
    } catch (...) {
        for (const auto& target : copied) {
            std::error_code ignored;
            std::filesystem::remove(target, ignored);
        }
        taskAttachments_.erase(std::remove_if(taskAttachments_.begin(), taskAttachments_.end(),
            [&addedIds](const TaskAttachment& attachment) {
                return std::find(addedIds.begin(), addedIds.end(), attachment.id()) != addedIds.end();
            }), taskAttachments_.end());
        throw;
    }

    value.markCompleted(completionNote);
    refreshProjectStatus(value.projectId());
    log(actingUserId, value.teamId(), "Task completed: " + value.title());
    addNotification(value.createdBy(), "Task completed: " + value.title());
    fileManager_.saveTasks(tasks_);
    fileManager_.saveTaskAttachments(taskAttachments_);
    fileManager_.saveProjects(projects_);
    fileManager_.saveNotifications(notifications_);
    fileManager_.saveActivities(activities_);
}

void TeamSyncService::reopenTask(const int actingUserId, const int taskId) {
    auto& value = searchTask(taskId);
    if (!canUpdateTask(actingUserId, value)) throw AuthorizationError("Only members of this team may reopen this task.");
    value.reopen();
    refreshProjectStatus(value.projectId());
    log(actingUserId, value.teamId(), "Task reopened: " + value.title());
    fileManager_.saveTasks(tasks_);
    fileManager_.saveProjects(projects_);
    fileManager_.saveActivities(activities_);
}

void TeamSyncService::addTaskComment(const int actingUserId, const int taskId,
                                     const std::string& comment) {
    auto& value = searchTask(taskId);
    requireTeamMembership(actingUserId, value.teamId());
    value.addComment(user(actingUserId).fullName() + ": " + comment);
    log(actingUserId, value.teamId(), "Commented on task: " + value.title());
    fileManager_.saveTasks(tasks_);
    fileManager_.saveActivities(activities_);
}

Task& TeamSyncService::searchTask(const int taskId) {
    const auto found = std::find_if(tasks_.begin(), tasks_.end(),
                                    [taskId](const auto& value) { return value->id() == taskId; });
    if (found == tasks_.end()) throw NotFoundError("Task ID " + std::to_string(taskId) + " was not found.");
    return **found;
}

Task* TeamSyncService::searchTask(const std::string& title) {
    const auto found = std::find_if(tasks_.begin(), tasks_.end(), [&title](const auto& value) {
        return util::containsIgnoreCase(value->title(), title);
    });
    return found == tasks_.end() ? nullptr : found->get();
}

const Task& TeamSyncService::task(const int id) const {
    const auto found = std::find_if(tasks_.begin(), tasks_.end(),
                                    [id](const auto& value) { return value->id() == id; });
    if (found == tasks_.end()) throw NotFoundError("Task ID " + std::to_string(id) + " was not found.");
    return **found;
}

std::vector<Task*> TeamSyncService::tasksForUser(const int userId) {
    std::vector<Task*> result;
    for (auto& value : tasks_) if (value->assignedUserId() == userId) result.push_back(value.get());
    return result;
}

std::vector<const Task*> TeamSyncService::tasksForUser(const int userId) const {
    std::vector<const Task*> result;
    for (const auto& value : tasks_) if (value->assignedUserId() == userId) result.push_back(value.get());
    return result;
}

std::vector<const Task*> TeamSyncService::tasksForTeam(const int teamId) const {
    std::vector<const Task*> result;
    for (const auto& value : tasks_) if (value->teamId() == teamId) result.push_back(value.get());
    return result;
}

std::vector<const Task*> TeamSyncService::searchTasks(const int userId,
                                                      const std::string& query) const {
    std::vector<const Task*> result;
    for (const auto& value : tasks_) {
        if (team(value->teamId()).hasMember(userId) &&
            (util::containsIgnoreCase(value->title(), query) ||
             util::containsIgnoreCase(value->description(), query))) result.push_back(value.get());
    }
    return result;
}

std::vector<MemberRecommendation> TeamSyncService::recommendMembers(const int taskId) const {
    const auto& target = task(taskId);
    const auto& parent = team(target.teamId());
    std::vector<MemberRecommendation> result;
    for (const int memberId : parent.memberIds()) {
        const auto& member = user(memberId);
        int active = 0;
        int completed = 0;
        int total = 0;
        for (const auto& value : tasks_) if (value->assignedUserId() == memberId) {
            ++total;
            if (value->status() == TaskStatus::Completed) ++completed;
            else ++active;
        }
        int skillMatch = target.requiredSkill().empty() ? 70 : 0;
        for (const auto& skill : member.skills()) {
            if (util::lower(skill) == util::lower(target.requiredSkill())) skillMatch = 100;
            else if (skillMatch < 100 && (util::containsIgnoreCase(skill, target.requiredSkill()) ||
                     util::containsIgnoreCase(target.requiredSkill(), skill))) skillMatch = 70;
        }
        const double rate = total == 0 ? 0.5 : static_cast<double>(completed) / total;
        const int workloadPoints = std::max(0, 25 - active * 5);
        const int experiencePoints = std::min(10, completed * 2);
        const int priorityFit = static_cast<int>(target.priority()) >= 3 && active <= 2 ? 5 : 2;
        const int score = std::clamp(static_cast<int>(std::lround(skillMatch * 0.40 +
            workloadPoints + rate * 20.0 + experiencePoints + priorityFit)), 0, 100);
        result.push_back({memberId, member.fullName(), skillMatch, active, completed, rate * 100.0, score});
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.score != right.score) return left.score > right.score;
        return left.activeTasks < right.activeTasks;
    });
    return result;
}

std::vector<const TaskAttachment*> TeamSyncService::attachmentsForTask(const int userId,
                                                                       const int taskId) const {
    const auto& target = task(taskId);
    requireTeamMembership(userId, target.teamId());
    std::vector<const TaskAttachment*> result;
    for (const auto& value : taskAttachments_) if (value.taskId() == taskId) result.push_back(&value);
    return result;
}

std::filesystem::path TeamSyncService::attachmentPath(const int userId, const int attachmentId) const {
    const auto found = std::find_if(taskAttachments_.begin(), taskAttachments_.end(),
        [attachmentId](const TaskAttachment& value) { return value.id() == attachmentId; });
    if (found == taskAttachments_.end()) throw NotFoundError("Task attachment was not found.");
    requireTeamMembership(userId, found->teamId());
    return fileManager_.root() / found->relativePath();
}

Message& TeamSyncService::sendMessage(const int senderId, const int teamId,
                                      const std::string& text) {
    requireTeamMembership(senderId, teamId);
    messages_.emplace_back(Message::generateId(), teamId, senderId, text, util::timestampNow());
    log(senderId, teamId, "Message sent");
    fileManager_.saveMessages(messages_);
    fileManager_.saveActivities(activities_);
    return messages_.back();
}

void TeamSyncService::deleteMessage(const int requestingUserId, const int messageId) {
    const auto found = std::find_if(messages_.begin(), messages_.end(),
                                    [messageId](const Message& value) { return value.id() == messageId; });
    if (found == messages_.end()) throw NotFoundError("Message was not found.");
    if (found->senderId() != requestingUserId) throw AuthorizationError("You can delete only your own messages.");
    const int teamId = found->teamId();
    messages_.erase(found);
    log(requestingUserId, teamId, "Deleted own message");
    fileManager_.saveMessages(messages_);
    fileManager_.saveActivities(activities_);
}

std::vector<Message> TeamSyncService::messagesForTeam(const int userId, const int teamId,
                                                      const std::string& query) const {
    requireTeamMembership(userId, teamId);
    std::vector<Message> result;
    for (const auto& value : messages_) if (value.teamId() == teamId &&
        (query.empty() || util::containsIgnoreCase(value.text(), query))) result.push_back(value);
    return result;
}

SharedFile& TeamSyncService::shareFile(const int uploaderId, const int teamId,
                                      const std::filesystem::path& path,
                                      const std::string& displayName,
                                      const std::string& description) {
    requireTeamMembership(uploaderId, teamId);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        throw ValidationError("The selected local file does not exist or is not a regular file.");
    }
    const auto absolutePath = std::filesystem::absolute(path, error);
    if (error) throw ValidationError("The file path could not be resolved.");
    const auto size = std::filesystem::file_size(absolutePath, error);
    if (error) throw ValidationError("The file size could not be read.");
    if (size > 150ULL * 1024ULL * 1024ULL) {
        throw ValidationError("Shared files must be 150 MB or smaller for reliable LAN synchronization.");
    }
    const int id = SharedFile::generateId();
    const auto relative = std::filesystem::path("shared_files_storage") /
                          (std::to_string(id) + "_" + safeAttachmentName(absolutePath.filename().string()));
    const auto storedPath = fileManager_.root() / relative;
    std::filesystem::create_directories(storedPath.parent_path(), error);
    if (error) throw PersistenceError("Cannot create the shared-file storage folder: " + error.message());
    std::filesystem::copy_file(absolutePath, storedPath,
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error) throw PersistenceError("Cannot copy the shared file into TeamSync: " + error.message());
    sharedFiles_.emplace_back(id, teamId, uploaderId,
        util::trim(displayName).empty() ? absolutePath.filename().string() : displayName,
        storedPath, description, util::timestampNow(), size);
    for (const int memberId : team(teamId).memberIds()) if (memberId != uploaderId) {
        addNotification(memberId, "A new file was shared with team " + team(teamId).name() + ": " +
                        sharedFiles_.back().fileName());
    }
    log(uploaderId, teamId, "File shared: " + sharedFiles_.back().fileName());
    fileManager_.saveSharedFiles(sharedFiles_);
    fileManager_.saveNotifications(notifications_);
    fileManager_.saveActivities(activities_);
    return sharedFiles_.back();
}

void TeamSyncService::removeSharedFile(const int requestingUserId, const int fileId) {
    const auto found = std::find_if(sharedFiles_.begin(), sharedFiles_.end(),
                                    [fileId](const SharedFile& value) { return value.id() == fileId; });
    if (found == sharedFiles_.end()) throw NotFoundError("Shared file record was not found.");
    if (found->uploaderId() != requestingUserId) throw AuthorizationError("You can remove only your own shared-file records.");
    const int teamId = found->teamId();
    const std::string name = found->fileName();
    const auto storedRoot = (fileManager_.root() / "shared_files_storage").lexically_normal();
    const auto storedPath = found->localPath().lexically_normal();
    const auto relative = storedPath.lexically_relative(storedRoot);
    if (!relative.empty() && *relative.begin() != "..") {
        std::error_code ignored;
        std::filesystem::remove(storedPath, ignored);
    }
    sharedFiles_.erase(found);
    log(requestingUserId, teamId, "Shared file record removed: " + name);
    fileManager_.saveSharedFiles(sharedFiles_);
    fileManager_.saveActivities(activities_);
}

SharedFile& TeamSyncService::sharedFile(const int id) {
    const auto found = std::find_if(sharedFiles_.begin(), sharedFiles_.end(),
                                    [id](const SharedFile& value) { return value.id() == id; });
    if (found == sharedFiles_.end()) throw NotFoundError("Shared file record was not found.");
    return *found;
}

std::vector<const SharedFile*> TeamSyncService::filesForTeam(const int userId, const int teamId,
                                                            const std::string& query) const {
    requireTeamMembership(userId, teamId);
    std::vector<const SharedFile*> result;
    for (const auto& value : sharedFiles_) if (value.teamId() == teamId &&
        (query.empty() || util::containsIgnoreCase(value.fileName(), query) ||
         util::containsIgnoreCase(value.description(), query))) result.push_back(&value);
    return result;
}

std::vector<Notification*> TeamSyncService::notificationsForUser(const int userId,
                                                                 const bool unreadOnly) {
    user(userId);
    std::vector<Notification*> result;
    for (auto& value : notifications_) if (value.userId() == userId &&
        (!unreadOnly || !value.isRead())) result.push_back(&value);
    return result;
}

void TeamSyncService::markAllNotificationsRead(const int userId) {
    for (auto* value : notificationsForUser(userId)) value->markRead();
    fileManager_.saveNotifications(notifications_);
}

std::vector<const ActivityLog*> TeamSyncService::activitiesForUser(const int userId,
                                                                  const int teamId) const {
    std::vector<const ActivityLog*> result;
    for (const auto& value : activities_) {
        bool visible = value.userId() == userId;
        if (value.teamId() != 0) {
            try { visible = team(value.teamId()).hasMember(userId); } catch (...) { visible = false; }
        }
        if (visible && (teamId == 0 || value.teamId() == teamId)) result.push_back(&value);
    }
    return result;
}

std::vector<ContributionStats> TeamSyncService::contributionsForTeam(const int requestingUserId,
                                                                    const int teamId) const {
    requireTeamMembership(requestingUserId, teamId);
    const auto& parent = team(teamId);
    std::vector<ContributionStats> result;
    for (const int memberId : parent.memberIds()) {
        ContributionStats stats;
        stats.userId = memberId;
        stats.memberName = user(memberId).fullName();
        for (const auto& value : tasks_) if (value->teamId() == teamId &&
                                             value->assignedUserId() == memberId) {
            ++stats.assignedTasks;
            if (value->status() == TaskStatus::Completed) {
                ++stats.completedTasks;
                if (value->completionDate() && *value->completionDate() <= value->dueDate()) {
                    ++stats.completedOnTime;
                }
            } else if (value->status() == TaskStatus::Overdue) {
                ++stats.overdueTasks;
            }
        }
        stats.activityCount = static_cast<int>(std::count_if(activities_.begin(), activities_.end(),
            [memberId, teamId](const ActivityLog& value) {
                return value.userId() == memberId && value.teamId() == teamId;
            }));
        stats.completionRate = stats.assignedTasks == 0 ? 0.0 :
            100.0 * stats.completedTasks / stats.assignedTasks;
        stats.onTimeRate = stats.completedTasks == 0 ? 0.0 :
            100.0 * stats.completedOnTime / stats.completedTasks;
        stats.score = std::clamp(stats.completionRate * 0.50 + stats.onTimeRate * 0.25 +
            std::min(20.0, stats.activityCount * 2.0) - stats.overdueTasks * 5.0 + 5.0, 0.0, 100.0);
        result.push_back(stats);
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.score > right.score;
    });
    return result;
}

void TeamSyncService::recordReportGenerated(const int userId, const int teamId,
                                            const std::string& reportName) {
    requireTeamMembership(userId, teamId);
    log(userId, teamId, "Report generated: " + reportName);
    fileManager_.saveActivities(activities_);
}

void TeamSyncService::saveAll() const {
    fileManager_.saveUsers(users_);
    fileManager_.saveTeams(teams_);
    fileManager_.saveProjects(projects_);
    fileManager_.saveTasks(tasks_);
    fileManager_.saveTaskAttachments(taskAttachments_);
    fileManager_.saveMessages(messages_);
    fileManager_.saveSharedFiles(sharedFiles_);
    fileManager_.saveNotifications(notifications_);
    fileManager_.saveActivities(activities_);
}

void TeamSyncService::reload() {
    fileManager_.clearWarnings();
    loadAll();
}

void TeamSyncService::requireTeamMembership(const int userId, const int teamId) const {
    user(userId);
    if (!team(teamId).hasMember(userId)) throw AuthorizationError("You do not belong to this team.");
}

void TeamSyncService::requireTeamAdmin(const int userId, const int teamId) const {
    requireTeamMembership(userId, teamId);
    if (!team(teamId).isAdmin(userId)) throw AuthorizationError("Only a team admin can perform this action.");
}

bool TeamSyncService::canUpdateTask(const int userId, const Task& value) const {
    try {
        user(userId);
        return team(value.teamId()).hasMember(userId);
    } catch (...) {
        return false;
    }
}

void TeamSyncService::refreshProjectStatus(const int projectId) {
    auto& parent = project(projectId);
    if (parent.status() == ProjectStatus::Archived) return;
    int count = 0;
    int completed = 0;
    bool started = false;
    for (const auto& value : tasks_) if (value->projectId() == projectId) {
        ++count;
        if (value->status() == TaskStatus::Completed) ++completed;
        else if (value->status() == TaskStatus::InProgress || value->status() == TaskStatus::Overdue)
            started = true;
    }
    if (count > 0 && completed == count) parent.setStatus(ProjectStatus::Completed);
    else if (started || completed > 0) parent.setStatus(ProjectStatus::InProgress);
    else parent.setStatus(ProjectStatus::NotStarted);
}

void TeamSyncService::refreshAllProjectStatuses() {
    for (const auto& value : projects_) refreshProjectStatus(value.id());
}

void TeamSyncService::addNotification(const int userId, const std::string& text) {
    user(userId);
    notifications_.emplace_back(Notification::generateId(), userId, text, util::timestampNow());
}

void TeamSyncService::log(const int userId, const int teamId, const std::string& action) {
    activities_.emplace_back(ActivityLog::generateId(), userId, teamId, action, util::timestampNow());
}

} // namespace teamsync
