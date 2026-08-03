#include "TeamSyncApplication.h"

#include "Dashboard.h"
#include "Exceptions.h"
#include "ReportGenerator.h"
#include "Utils.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <queue>
#include <sstream>

namespace teamsync {

TeamSyncApplication::TeamSyncApplication(const std::filesystem::path& applicationRoot,
                                         std::istream& in, std::ostream& out)
    : service_(applicationRoot), input_(in, out), out_(out) {}

void TeamSyncApplication::showBanner() const {
    out_ << "\n==================================================\n"
         << "                     TEAMSYNC\n"
         << "     Local Collaboration and Task Management\n"
         << "==================================================\n"
         << "Sudhan Bhattarai & Severoos Nepali | OOP Semester 2\n";
}

int TeamSyncApplication::run() {
    showBanner();
    if (!service_.fileManager().warnings().empty()) {
        out_ << "\nData recovery warnings:\n";
        for (const auto& warning : service_.fileManager().warnings()) out_ << "  - " << warning << '\n';
    }
    while (true) {
        try {
            out_ << "\n1. Register\n2. Login\n3. Forgot password\n4. Exit\n";
            const int choice = input_.integer("Choose: ", 1, 4);
            if (choice == 4) {
                out_ << "TeamSync closed safely.\n";
                return 0;
            }
            if (choice == 1) safely([this] { registerAccount(); });
            if (choice == 2 && login()) sessionMenu();
            if (choice == 3) safely([this] { recoverPassword(); });
        } catch (const NavigationRequest&) {
            out_ << "\nYou are already at the welcome menu.\n";
        }
    }
}

void TeamSyncApplication::registerAccount() {
    out_ << "\n--- Register Account ---\n";
    const auto name = input_.line("Full name: ");
    const auto username = input_.line("Username: ");
    const auto email = input_.line("Email (optional): ", true);
    const auto skills = input_.commaSeparated("Skills, comma separated (optional): ");
    const int roleChoice = input_.integer("Role (1 Admin, 2 Team Member): ", 1, 2);
    const auto password = input_.line("Password: ");
    const auto confirmation = input_.line("Confirm password: ");
    std::vector<std::string> securityAnswers;
    out_ << "\nSecurity questions (required for password recovery):\n";
    for (const auto& question : securityQuestions()) {
        securityAnswers.push_back(input_.line(question + " "));
    }
    auto& created = service_.registerUser(name, username, password, confirmation, email,
        roleChoice == 1 ? UserRole::Admin : UserRole::TeamMember, skills, securityAnswers);
    out_ << "Account created successfully. Your user ID is " << created.id() << ".\n";
}

void TeamSyncApplication::recoverPassword() {
    out_ << "\n--- Forgot Password ---\n";
    const auto username = input_.line("Username: ");
    std::vector<std::string> securityAnswers;
    for (const auto& question : securityQuestions()) {
        securityAnswers.push_back(input_.line(question + " "));
    }
    const auto replacement = input_.line("New password: ");
    const auto confirmation = input_.line("Confirm new password: ");
    service_.resetPassword(username, securityAnswers, replacement, confirmation);
    out_ << "Password reset successfully. You can sign in now.\n";
}

bool TeamSyncApplication::login() {
    try {
        out_ << "\n--- Login ---\n";
        const auto username = input_.line("Username: ");
        const auto password = input_.line("Password: ");
        currentUser_ = &service_.login(username, password);
        input_.setDashboardAvailable(true);
        out_ << "Login successful. Welcome, " << currentUser_->fullName() << ".\n";
        return true;
    } catch (const NavigationRequest&) {
        out_ << "Returned to the welcome menu.\n";
        currentUser_ = nullptr;
        input_.setDashboardAvailable(false);
        return false;
    } catch (const std::exception& error) {
        out_ << "Login failed: " << error.what() << '\n';
        currentUser_ = nullptr;
        input_.setDashboardAvailable(false);
        return false;
    }
}

void TeamSyncApplication::sessionMenu() {
    while (currentUser_) {
        try {
            out_ << "\n================ MAIN MENU ================\n"
                 << "1. Dashboard\n2. My Profile\n3. Teams\n4. Projects\n5. Tasks\n"
                 << "6. Team Chat\n7. Shared Files\n8. Notifications\n9. Reports\n"
                 << "10. Activity Log\n11. Logout\n";
            const int choice = input_.integer("Choose: ", 1, 11);
            switch (choice) {
                case 1: safely([this] { Dashboard::display(Dashboard::calculate(service_, currentUserId()), out_); input_.pause(); }); break;
                case 2: profileMenu(); break;
                case 3: teamsMenu(); break;
                case 4: projectsMenu(); break;
                case 5: tasksMenu(); break;
                case 6: chatMenu(); break;
                case 7: filesMenu(); break;
                case 8: notificationsMenu(); break;
                case 9: reportsMenu(); break;
                case 10: activitiesMenu(); break;
                case 11:
                    safely([this] { service_.logout(currentUserId()); });
                    currentUser_ = nullptr;
                    input_.setDashboardAvailable(false);
                    out_ << "Logged out.\n";
                    break;
            }
        } catch (const NavigationRequest& navigation) {
            if (navigation.target() == NavigationTarget::Dashboard) {
                out_ << "\n--- Dashboard ---\n";
                safely([this] {
                    Dashboard::display(Dashboard::calculate(service_, currentUserId()), out_);
                });
            } else {
                out_ << "\nReturned to the dashboard.\n";
            }
        }
    }
}

int TeamSyncApplication::currentUserId() const {
    if (!currentUser_) throw AuthenticationError("No user is logged in.");
    return currentUser_->id();
}

int TeamSyncApplication::chooseTeam(const std::string& prompt) {
    listMyTeams();
    return input_.integer(prompt, 1, std::numeric_limits<int>::max());
}

void TeamSyncApplication::listMyTeams() const {
    const auto values = service_.teamsForUser(currentUserId());
    out_ << "\nMy teams:\n";
    if (values.empty()) out_ << "  None. Create a team or join with a code.\n";
    for (const auto* value : values) out_ << "  " << *value
        << (value->isAdmin(currentUserId()) ? " | You are team admin" : "") << '\n';
}

void TeamSyncApplication::displayTeamDetails(const int teamId) const {
    const auto& value = service_.team(teamId);
    if (!value.hasMember(currentUserId())) throw AuthorizationError("You do not belong to this team.");
    out_ << "\n" << value << "\nDescription: " << value.description()
         << "\nCreator: " << service_.user(value.creatorId()).fullName()
         << "\nCreated: " << value.dateCreated() << "\nMembers:\n";
    for (const int memberId : value.memberIds()) {
        const auto& member = service_.user(memberId);
        out_ << "  [" << member.id() << "] " << member.fullName() << " (" << member.username()
             << ") - " << (value.isAdmin(memberId) ? "Team Admin" : "Member") << '\n';
    }
}

void TeamSyncApplication::profileMenu() {
    bool done = false;
    while (!done) {
        out_ << "\n--- My Profile ---\n1. View profile\n2. Edit profile\n3. Change password\n4. Back\n";
        switch (input_.integer("Choose: ", 1, 4)) {
            case 1: safely([this] { currentUser_->displayProfile(); input_.pause(); }); break;
            case 2: safely([this] {
                const auto name = input_.line("Full name: ");
                const auto email = input_.line("Email (optional): ", true);
                const auto skills = input_.commaSeparated("Skills, comma separated: ");
                service_.editProfile(currentUserId(), name, email, skills);
                out_ << "Profile updated.\n";
            }); break;
            case 3: safely([this] {
                const auto current = input_.line("Current password: ");
                const auto replacement = input_.line("New password: ");
                const auto confirmation = input_.line("Confirm new password: ");
                service_.changePassword(currentUserId(), current, replacement, confirmation);
                out_ << "Password changed.\n";
            }); break;
            case 4: done = true; break;
        }
    }
}

void TeamSyncApplication::teamsMenu() {
    bool done = false;
    while (!done) {
        out_ << "\n--- Teams ---\n1. List my teams\n2. Create team\n3. Join team\n"
             << "4. View team details and members\n5. Search teams\n6. Search users\n"
             << "7. Leave team\n8. Remove member (team admin)\n"
             << "9. Promote member (team admin)\n10. Back\n";
        const int choice = input_.integer("Choose: ", 1, 10);
        if (choice == 10) { done = true; continue; }
        safely([this, choice] {
            if (choice == 1) { listMyTeams(); input_.pause(); }
            if (choice == 2) {
                const auto name = input_.line("Team name: ");
                const auto description = input_.line("Description: ", true);
                auto& value = service_.createTeam(currentUserId(), name, description);
                out_ << "Team created. ID: " << value.id() << ", join code: " << value.joinCode() << "\n";
            }
            if (choice == 3) {
                auto& value = service_.joinTeam(currentUserId(), input_.line("Team ID or join code: "));
                out_ << "Joined " << value.name() << ".\n";
            }
            if (choice == 4) { displayTeamDetails(chooseTeam()); input_.pause(); }
            if (choice == 5) {
                const auto results = service_.searchTeams(input_.line("Search text: "));
                for (const auto* value : results) out_ << "  " << *value << '\n';
                if (results.empty()) out_ << "No matching teams.\n";
                input_.pause();
            }
            if (choice == 6) {
                const auto results = service_.searchUsers(input_.line("Name or username: "));
                for (const auto* value : results) out_ << "  [" << value->id() << "] "
                    << value->fullName() << " (" << value->username() << ") | "
                    << toString(value->role()) << '\n';
                if (results.empty()) out_ << "No matching users.\n";
                input_.pause();
            }
            if (choice == 7) {
                const int id = chooseTeam();
                if (input_.yesNo("Leave this team?")) { service_.leaveTeam(currentUserId(), id); out_ << "Team left.\n"; }
            }
            if (choice == 8) {
                const int id = chooseTeam();
                displayTeamDetails(id);
                const int memberId = input_.integer("Member user ID to remove: ", 1, std::numeric_limits<int>::max());
                if (input_.yesNo("Remove this member?")) { service_.removeMember(currentUserId(), id, memberId); out_ << "Member removed.\n"; }
            }
            if (choice == 9) {
                const int id = chooseTeam();
                displayTeamDetails(id);
                service_.promoteMember(currentUserId(), id,
                    input_.integer("Member user ID to promote: ", 1, std::numeric_limits<int>::max()));
                out_ << "Member promoted.\n";
            }
        });
    }
}

void TeamSyncApplication::listAccessibleProjects() const {
    auto values = service_.projectsForUser(currentUserId());
    std::sort(values.begin(), values.end(), [](const Project* left, const Project* right) {
        return left->dueDate() < right->dueDate();
    });
    out_ << "\nAccessible projects:\n";
    if (values.empty()) out_ << "  None.\n";
    for (const auto* value : values) out_ << "  " << *value << '\n';
}

void TeamSyncApplication::projectsMenu() {
    bool done = false;
    while (!done) {
        out_ << "\n--- Projects ---\n1. List projects\n2. Create project\n3. Edit project\n"
             << "4. Archive project\n5. Search projects\n6. Back\n";
        const int choice = input_.integer("Choose: ", 1, 6);
        if (choice == 6) { done = true; continue; }
        safely([this, choice] {
            if (choice == 1) { listAccessibleProjects(); input_.pause(); }
            if (choice == 2) {
                const int teamId = chooseTeam();
                const auto title = input_.line("Project title: ");
                const auto description = input_.line("Description: ", true);
                const auto start = input_.date("Start date");
                const auto due = input_.date("Due date");
                auto& value = service_.createProject(currentUserId(), teamId, title, description,
                                                      start, due);
                out_ << "Project created with ID " << value.id() << ".\n";
            }
            if (choice == 3) {
                listAccessibleProjects();
                const int id = input_.integer("Project ID: ", 1, std::numeric_limits<int>::max());
                const auto title = input_.line("Title: ");
                const auto description = input_.line("Description: ", true);
                const auto start = input_.date("Start date");
                const auto due = input_.date("Due date");
                service_.editProject(currentUserId(), id, title, description, start, due);
                out_ << "Project updated.\n";
            }
            if (choice == 4) {
                listAccessibleProjects();
                const int id = input_.integer("Project ID: ", 1, std::numeric_limits<int>::max());
                if (input_.yesNo("Archive this project?")) { service_.archiveProject(currentUserId(), id); out_ << "Project archived.\n"; }
            }
            if (choice == 5) {
                const auto values = service_.searchProjects(currentUserId(), input_.line("Search text: "));
                for (const auto* value : values) out_ << "  " << *value << '\n';
                if (values.empty()) out_ << "No matching projects.\n";
                input_.pause();
            }
        });
    }
}

void TeamSyncApplication::displayTasks(const std::vector<const Task*>& tasks) const {
    out_ << "\n" << std::left << std::setw(8) << "ID" << std::setw(28) << "Title"
         << std::setw(16) << "Priority" << std::setw(16) << "Status"
         << std::setw(14) << "Due" << "Assignee\n"
         << std::string(104, '-') << '\n';
    for (const auto* value : tasks) {
        out_ << std::setw(8) << value->id() << std::setw(28) << value->title().substr(0, 27)
             << std::setw(16) << toString(value->priority()) << std::setw(16) << toString(value->status())
             << std::setw(14) << value->dueDate().toString()
             << service_.user(value->assignedUserId()).fullName() << '\n';
    }
    if (tasks.empty()) out_ << "No tasks found.\n";
}

void TeamSyncApplication::displayTasks(const std::vector<Task*>& tasks) const {
    std::vector<const Task*> values(tasks.begin(), tasks.end());
    displayTasks(values);
}

TaskPriority TeamSyncApplication::choosePriority() {
    return static_cast<TaskPriority>(input_.integer("Priority (1 Low, 2 Medium, 3 High, 4 Urgent): ", 1, 4));
}

TaskStatus TeamSyncApplication::chooseStatus() {
    return static_cast<TaskStatus>(input_.integer("Status (1 Pending, 2 In Progress, 3 Completed, 4 Overdue): ", 1, 4) - 1);
}

TaskType TeamSyncApplication::chooseTaskType() {
    return static_cast<TaskType>(input_.integer("Type (1 General, 2 Development, 3 Research, 4 Documentation): ", 1, 4) - 1);
}

void TeamSyncApplication::tasksMenu() {
    bool done = false;
    while (!done) {
        out_ << "\n--- Tasks ---\n1. View my assigned tasks\n2. View team tasks\n3. Create task\n"
             << "4. Edit task\n5. Assign/reassign task\n6. Start task\n7. Mark complete\n"
             << "8. Reopen task\n9. Add/view comments\n10. Search tasks\n11. Filter tasks\n"
             << "12. Sort tasks\n13. Smart member recommendation\n14. Delete task\n15. Back\n";
        const int choice = input_.integer("Choose: ", 1, 15);
        if (choice == 15) { done = true; continue; }
        safely([this, choice] {
            if (choice == 1) { displayTasks(service_.tasksForUser(currentUserId())); input_.pause(); }
            if (choice == 2) {
                const int teamId = chooseTeam();
                displayTasks(service_.tasksForTeam(teamId));
                input_.pause();
            }
            if (choice == 3) {
                listAccessibleProjects();
                const int projectId = input_.integer("Project ID: ", 1, std::numeric_limits<int>::max());
                displayTeamDetails(service_.project(projectId).teamId());
                const auto type = chooseTaskType();
                const auto title = input_.line("Task title: ");
                const auto description = input_.line("Description: ", true);
                const int assignee = input_.integer("Assign to user ID: ", 1, std::numeric_limits<int>::max());
                const auto priority = choosePriority();
                const auto due = input_.date("Due date");
                const auto skill = input_.line("Required skill (optional): ", true);
                auto& value = service_.createTask(currentUserId(), projectId, type, title,
                    description, assignee, priority, due, skill);
                out_ << "Task created with ID " << value.id() << ".\n";
            }
            if (choice == 4) {
                const int id = input_.integer("Task ID: ", 1, std::numeric_limits<int>::max());
                const auto title = input_.line("Title: ");
                const auto description = input_.line("Description: ", true);
                const auto priority = choosePriority();
                const auto due = input_.date("Due date");
                const auto skill = input_.line("Required skill (optional): ", true);
                service_.editTask(currentUserId(), id, title, description, priority, due, skill);
                out_ << "Task updated.\n";
            }
            if (choice == 5) {
                const int id = input_.integer("Task ID: ", 1, std::numeric_limits<int>::max());
                displayTeamDetails(service_.task(id).teamId());
                const int memberId = input_.integer("Member user ID: ", 1, std::numeric_limits<int>::max());
                const auto note = input_.line("Assignment note (optional): ", true);
                service_.assignTask(currentUserId(), id, memberId, note);
                out_ << "Task assigned.\n";
            }
            if (choice == 6) {
                const int id = input_.integer("Task ID: ", 1, std::numeric_limits<int>::max());
                service_.startTask(currentUserId(), id);
                out_ << "Task status changed to In Progress.\n";
            }
            if (choice == 7) {
                const int id = input_.integer("Task ID: ", 1, std::numeric_limits<int>::max());
                const auto note = input_.line("Completion note/content (optional): ", true);
                std::vector<std::filesystem::path> files;
                while (input_.yesNo("Add a completion file?")) {
                    files.emplace_back(input_.line("File path: "));
                }
                service_.markTaskComplete(currentUserId(), id, note, files);
                out_ << "Task completed.\n";
            }
            if (choice == 8) {
                service_.reopenTask(currentUserId(),
                    input_.integer("Task ID: ", 1, std::numeric_limits<int>::max()));
                out_ << "Task reopened.\n";
            }
            if (choice == 9) {
                const int id = input_.integer("Task ID: ", 1, std::numeric_limits<int>::max());
                const auto& value = service_.task(id);
                out_ << value;
                out_ << "Comments/updates:\n";
                for (const auto& comment : value.comments()) out_ << "  - " << comment << '\n';
                if (value.comments().empty()) out_ << "  None.\n";
                if (!value.completionNote().empty()) out_ << "Completion content: " << value.completionNote() << '\n';
                const auto attachments = service_.attachmentsForTask(currentUserId(), id);
                out_ << "Completion files:\n";
                for (const auto* attachment : attachments) out_ << "  - " << attachment->fileName()
                    << " (" << service_.attachmentPath(currentUserId(), attachment->id()).string() << ")\n";
                if (attachments.empty()) out_ << "  None.\n";
                if (input_.yesNo("Add a comment?")) service_.addTaskComment(currentUserId(), id, input_.line("Comment: "));
            }
            if (choice == 10) {
                displayTasks(service_.searchTasks(currentUserId(), input_.line("Search text: ")));
                input_.pause();
            }
            if (choice == 11) {
                auto values = service_.searchTasks(currentUserId(), "");
                const auto status = chooseStatus();
                values.erase(std::remove_if(values.begin(), values.end(), [status](const Task* value) {
                    return value->status() != status;
                }), values.end());
                displayTasks(values);
                input_.pause();
            }
            if (choice == 12) {
                auto values = service_.searchTasks(currentUserId(), "");
                const int sort = input_.integer("Sort by (1 Due date, 2 Priority, 3 Status): ", 1, 3);
                std::sort(values.begin(), values.end(), [sort](const Task* left, const Task* right) {
                    if (sort == 1) return left->dueDate() < right->dueDate();
                    if (sort == 2) return static_cast<int>(left->priority()) > static_cast<int>(right->priority());
                    return static_cast<int>(left->status()) < static_cast<int>(right->status());
                });
                displayTasks(values);
                input_.pause();
            }
            if (choice == 13) {
                const int id = input_.integer("Task ID: ", 1, std::numeric_limits<int>::max());
                const auto& target = service_.task(id);
                const auto values = service_.recommendMembers(id);
                out_ << "\nTask: " << target.title() << "\nRequired skill: "
                     << (target.requiredSkill().empty() ? "None" : target.requiredSkill())
                     << "\nPriority: " << toString(target.priority()) << "\n\nRecommendations:\n";
                for (const auto& value : values) out_ << "  " << value.memberName << " (#" << value.userId
                    << ") | Skill match: " << value.skillMatchPercent << "% | Active: "
                    << value.activeTasks << " | Completed: " << value.completedTasks
                    << " | Score: " << value.score << "/100\n";
                out_ << "Rule-based score: 40% skill, 25 workload, 20 completion rate, 10 experience, 5 priority fit.\n";
                input_.pause();
            }
            if (choice == 14) {
                const int id = input_.integer("Task ID: ", 1, std::numeric_limits<int>::max());
                if (input_.yesNo("Delete this task permanently?")) { service_.deleteTask(currentUserId(), id); out_ << "Task deleted.\n"; }
            }
        });
    }
}

void TeamSyncApplication::displayMessages(const std::vector<Message>& messages) const {
    if (messages.empty()) out_ << "No messages found.\n";
    for (const auto& value : messages) out_ << '[' << value.dateTime() << "] "
        << service_.user(value.senderId()).fullName() << ": " << value.text()
        << " (#" << value.id() << ")\n";
}

void TeamSyncApplication::chatMenu() {
    bool done = false;
    while (!done) {
        out_ << "\n--- Team Chat (local, refresh-based) ---\n1. View message history\n"
             << "2. Send message\n3. Search messages\n4. Delete my message\n5. Back\n";
        const int choice = input_.integer("Choose: ", 1, 5);
        if (choice == 5) { done = true; continue; }
        safely([this, choice] {
            const int teamId = chooseTeam();
            if (choice == 1) { displayMessages(service_.messagesForTeam(currentUserId(), teamId)); input_.pause(); }
            if (choice == 2) { service_.sendMessage(currentUserId(), teamId, input_.line("Message: ")); out_ << "Message saved.\n"; }
            if (choice == 3) { displayMessages(service_.messagesForTeam(currentUserId(), teamId, input_.line("Search text: "))); input_.pause(); }
            if (choice == 4) {
                displayMessages(service_.messagesForTeam(currentUserId(), teamId));
                const int id = input_.integer("Message ID: ", 1, std::numeric_limits<int>::max());
                if (input_.yesNo("Delete this message?")) { service_.deleteMessage(currentUserId(), id); out_ << "Message deleted.\n"; }
            }
        });
    }
}

void TeamSyncApplication::filesMenu() {
    bool done = false;
    while (!done) {
        out_ << "\n--- Shared Files ---\n1. View team files\n"
             << "2. Share file\n3. Search files\n4. Open file\n5. Remove my file\n6. Back\n";
        const int choice = input_.integer("Choose: ", 1, 6);
        if (choice == 6) { done = true; continue; }
        safely([this, choice] {
            const int teamId = chooseTeam();
            auto showFiles = [this](const std::vector<const SharedFile*>& values) {
                for (const auto* value : values) out_ << "  " << *value << "\n    Description: "
                    << value->description() << "\n    Shared: " << value->dateShared() << '\n';
                if (values.empty()) out_ << "No shared files found.\n";
            };
            if (choice == 1) { showFiles(service_.filesForTeam(currentUserId(), teamId)); input_.pause(); }
            if (choice == 2) {
                const auto path = std::filesystem::path(input_.line("Existing local file path: "));
                const auto name = input_.line("Display name (blank uses actual name): ", true);
                const auto description = input_.line("Description: ", true);
                auto& value = service_.shareFile(currentUserId(), teamId, path, name, description);
                out_ << "File copied into TeamSync and shared as #" << value.id() << ". The original was not changed.\n";
            }
            if (choice == 3) {
                showFiles(service_.filesForTeam(currentUserId(), teamId, input_.line("Search text: ")));
                input_.pause();
            }
            if (choice == 4) {
                showFiles(service_.filesForTeam(currentUserId(), teamId));
                auto& value = service_.sharedFile(input_.integer("File record ID: ", 1, std::numeric_limits<int>::max()));
                if (value.teamId() != teamId) throw AuthorizationError("That file belongs to another team.");
                if (!value.exists()) throw ValidationError("The file has been moved or deleted from its recorded path.");
                if (value.isExecutable()) {
                    out_ << "Warning: this appears to be an executable or script. Opening it may run code.\n";
                    if (!input_.yesNo("Open it anyway?")) return;
                } else if (!input_.yesNo("Open with the system's default application?")) return;
                out_ << (value.openWithDefaultApplication() ? "Open request sent.\n" : "The file could not be opened on this system.\n");
            }
            if (choice == 5) {
                showFiles(service_.filesForTeam(currentUserId(), teamId));
                const int id = input_.integer("File record ID: ", 1, std::numeric_limits<int>::max());
                if (input_.yesNo("Remove only this metadata record? The original file will not be deleted.")) {
                    service_.removeSharedFile(currentUserId(), id);
                    out_ << "Shared-file record removed; original file unchanged.\n";
                }
            }
        });
    }
}

void TeamSyncApplication::notificationsMenu() {
    bool done = false;
    while (!done) {
        out_ << "\n--- Notifications ---\n1. View all\n2. View unread\n3. Mark all read\n4. Back\n";
        const int choice = input_.integer("Choose: ", 1, 4);
        if (choice == 4) { done = true; continue; }
        safely([this, choice] {
            if (choice == 3) { service_.markAllNotificationsRead(currentUserId()); out_ << "Notifications marked read.\n"; return; }
            const auto values = service_.notificationsForUser(currentUserId(), choice == 2);
            std::queue<Notification*> ordered;
            for (auto* value : values) ordered.push(value);
            while (!ordered.empty()) {
                out_ << "  " << *ordered.front() << '\n';
                ordered.pop();
            }
            if (values.empty()) out_ << "No notifications.\n";
            input_.pause();
        });
    }
}

void TeamSyncApplication::reportsMenu() {
    while (true) {
        out_ << "\n--- Reports ---\n1. Task completion\n2. Pending tasks\n3. Overdue tasks\n"
             << "4. Team activity\n5. Member contribution\n6. Project status\n"
             << "7. Shared files\n8. Team member list\n9. Back\n";
        const int choice = input_.integer("Choose: ", 1, 9);
        if (choice == 9) return;
        safely([this, choice] {
            const int teamId = chooseTeam();
            const auto type = static_cast<ReportType>(choice);
            ReportGenerator generator(service_);
            out_ << '\n' << generator.generate(type, currentUserId(), teamId);
            service_.recordReportGenerated(currentUserId(), teamId, toString(type));
            if (input_.yesNo("Export this report to a text file?")) {
                const auto path = generator.exportText(type, currentUserId(), teamId,
                    input_.line("Optional file name (blank for automatic): ", true));
                out_ << "Report exported to: " << path.string() << '\n';
            }
            input_.pause();
        });
    }
}

void TeamSyncApplication::activitiesMenu() {
    safely([this] {
        const int teamId = input_.yesNo("Filter activity by one team?") ? chooseTeam() : 0;
        const auto values = service_.activitiesForUser(currentUserId(), teamId);
        out_ << "\n--- Visible Activity Log ---\n";
        for (const auto* value : values) out_ << "  " << *value << '\n';
        if (values.empty()) out_ << "No visible activities.\n";
        input_.pause();
    });
}

} // namespace teamsync
