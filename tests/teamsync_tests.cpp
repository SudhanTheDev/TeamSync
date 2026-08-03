#include "Dashboard.h"
#include "Exceptions.h"
#include "Input.h"
#include "ReportGenerator.h"
#include "TeamSyncApplication.h"
#include "TeamSyncService.h"
#include "Utils.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

template <typename Exception, typename Function>
void expectThrows(Function&& action) {
    bool thrown = false;
    try { action(); } catch (const Exception&) { thrown = true; }
    assert(thrown);
}

struct TemporaryDirectory {
    std::filesystem::path path;
    TemporaryDirectory() {
        path = std::filesystem::temp_directory_path() /
            ("teamsync_cpp_test_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

} // namespace

int main() {
    using namespace teamsync;

    assert(Date::isValid(29, 2, 2024));
    assert(!Date::isValid(29, 2, 2023));
    assert(Date::fromString("2026-08-03") == Date(3, 8, 2026));
    assert(Date(1, 1, 2026) < Date(2, 1, 2026));
    expectThrows<ValidationError>([] { Date::fromString("2026-02-30"); });
    const std::string encoded = "pipes|commas,new\nline%";
    assert(util::decodeField(util::encodeField(encoded)) == encoded);

    {
        std::istringstream inputStream("dashboard\nusable value\n");
        std::ostringstream outputStream;
        Input input(inputStream, outputStream);
        assert(input.line("Value: ") == "usable value");
        assert(outputStream.str().find("available after you log in") != std::string::npos);
    }
    {
        std::istringstream inputStream("back\n");
        std::ostringstream outputStream;
        Input input(inputStream, outputStream);
        try {
            input.line("Value: ");
            assert(false);
        } catch (const NavigationRequest& navigation) {
            assert(navigation.target() == NavigationTarget::Back);
        }
    }
    {
        std::istringstream inputStream("dashboard\n");
        std::ostringstream outputStream;
        Input input(inputStream, outputStream);
        input.setDashboardAvailable(true);
        try {
            input.integer("Choice: ", 1, 4);
            assert(false);
        } catch (const NavigationRequest& navigation) {
            assert(navigation.target() == NavigationTarget::Dashboard);
        }
    }

    TemporaryDirectory temporary;
    int adminId = 0;
    int memberId = 0;
    int teamId = 0;
    int projectId = 0;
    int taskId = 0;
    {
        TeamSyncService service(temporary.path);
        auto& admin = service.registerUser("Test Admin", "admin", "pass123", "pass123",
                                           "admin@test.local", UserRole::Admin,
                                           {"C++", "UI Design"},
                                           {"Sunrise School", "Sunny", "Kathmandu"});
        adminId = admin.id();
        auto& member = service.registerUser("Test Member", "member", "pass456", "pass456",
                                             "member@test.local", UserRole::TeamMember,
                                             {"C++", "Research"},
                                             {"Evergreen", "Mimi", "Pokhara"});
        memberId = member.id();
        auto& collaborator = service.registerUser("Test Collaborator", "collaborator",
            "pass789", "pass789", "collaborator@test.local", UserRole::TeamMember,
            {"Testing"}, {"Hill School", "Toto", "Lalitpur"});
        const int collaboratorId = collaborator.id();
        expectThrows<ValidationError>([&] {
            service.registerUser("Duplicate", "ADMIN", "abcd", "abcd", "",
                                 UserRole::TeamMember, {}, {"School", "Nick", "City"});
        });
        expectThrows<ValidationError>([&] {
            service.registerUser("No Recovery", "no_recovery", "abcd", "abcd", "",
                                 UserRole::TeamMember, {}, {});
        });
        expectThrows<AuthenticationError>([&] { service.login("admin", "wrong"); });
        assert(service.login("admin", "pass123").id() == adminId);
        expectThrows<AuthenticationError>([&] {
            service.resetPassword("member", {"Wrong", "Mimi", "Pokhara"},
                                  "recovered", "recovered");
        });
        service.resetPassword("member", {" evergreen ", "MIMI", "pokhara"},
                              "recovered", "recovered");
        expectThrows<AuthenticationError>([&] { service.login("member", "pass456"); });
        assert(service.login("member", "recovered").id() == memberId);

        auto& parent = service.createTeam(adminId, "Test Team", "Integration test");
        teamId = parent.id();
        const std::string joinCode = parent.joinCode();
        service.joinTeam(memberId, joinCode);
        service.joinTeam(collaboratorId, joinCode);
        expectThrows<ValidationError>([&] { service.joinTeam(memberId, joinCode); });
        expectThrows<AuthorizationError>([&] {
            service.createProject(memberId, teamId, "Forbidden", "", Date::today(),
                                  Date(31, 12, Date::today().year() + 1));
        });

        auto& project = service.createProject(adminId, teamId, "Test Project", "Project details",
                                               Date::today(),
                                               Date(31, 12, Date::today().year() + 1));
        projectId = project.id();
        expectThrows<AuthorizationError>([&] {
            service.createTask(memberId, projectId, TaskType::General, "Forbidden task", "",
                               memberId, TaskPriority::Low,
                               Date(31, 12, Date::today().year() + 1), "");
        });
        auto& task = service.createTask(adminId, projectId, TaskType::Development,
            "Implement feature", "Write clear C++", memberId, TaskPriority::High,
            Date(31, 12, Date::today().year() + 1), "C++");
        taskId = task.id();
        assert(service.project(projectId).status() == ProjectStatus::NotStarted);
        assert(task.calculateWeight() > 3.0);
        assert(service.searchTask(taskId) == task);
        assert(service.searchTask("feature") != nullptr);
        assert(!service.recommendMembers(taskId).empty());
        service.addTaskComment(memberId, taskId, "Work started");
        service.startTask(memberId, taskId);
        assert(service.task(taskId).status() == TaskStatus::InProgress);
        assert(service.project(projectId).status() == ProjectStatus::InProgress);
        const auto evidenceFile = temporary.path / "completion-evidence.txt";
        { std::ofstream output(evidenceFile); output << "Finished result\n"; }
        service.markTaskComplete(memberId, taskId, "Feature finished and verified", {evidenceFile});
        assert(service.task(taskId).status() == TaskStatus::Completed);
        assert(service.project(projectId).status() == ProjectStatus::Completed);
        assert(service.task(taskId).completionNote() == "Feature finished and verified");
        assert(service.attachmentsForTask(memberId, taskId).size() == 1);
        assert(std::filesystem::exists(service.attachmentPath(
            memberId, service.attachmentsForTask(memberId, taskId).front()->id())));

        auto& sharedWork = service.createTask(adminId, projectId, TaskType::General,
            "Team-wide task", "Any team member can perform status actions", adminId,
            TaskPriority::Medium, Date(31, 12, Date::today().year() + 1), "Testing");
        service.startTask(collaboratorId, sharedWork.id());
        service.markTaskComplete(collaboratorId, sharedWork.id(), "Collaborator completed the work");
        assert(service.task(sharedWork.id()).status() == TaskStatus::Completed);
        assert(service.project(projectId).status() == ProjectStatus::Completed);

        service.sendMessage(memberId, teamId, "Hello | TeamSync");
        assert(service.messagesForTeam(adminId, teamId, "hello").size() == 1);

        const auto localFile = temporary.path / "shared-note.txt";
        { std::ofstream output(localFile); output << "TeamSync test file\n"; }
        auto& shared = service.shareFile(memberId, teamId, localFile, "Test Note", "Metadata test");
        assert(shared.exists());
        assert(shared.localPath() != std::filesystem::absolute(localFile));
        assert(shared.localPath().parent_path().filename() == "shared_files_storage");

        const auto dashboard = Dashboard::calculate(service, memberId);
        assert(dashboard.joinedTeams == 1);
        assert(dashboard.completedTasks == 1);

        ReportGenerator reports(service);
        const auto text = reports.generate(ReportType::MemberContribution, adminId, teamId);
        assert(text.find("Test Member") != std::string::npos);
        const auto exported = reports.exportText(ReportType::TaskCompletion, adminId, teamId);
        assert(std::filesystem::exists(exported));
        service.recordReportGenerated(adminId, teamId, "test report");
        service.saveAll();
    }

    {
        TeamSyncService restored(temporary.path);
        assert(restored.user(adminId).username() == "admin");
        assert(restored.team(teamId).hasMember(memberId));
        assert(restored.project(projectId).title() == "Test Project");
        assert(restored.task(taskId).status() == TaskStatus::Completed);
        assert(restored.task(taskId).completionNote() == "Feature finished and verified");
        assert(restored.attachmentsForTask(memberId, taskId).size() == 1);
        assert(restored.messagesForTeam(adminId, teamId).size() == 1);
        assert(restored.filesForTeam(adminId, teamId).size() == 1);
        assert(restored.filesForTeam(adminId, teamId).front()->exists());
        assert(restored.filesForTeam(adminId, teamId).front()->localPath().parent_path().filename() ==
               "shared_files_storage");
        assert(!restored.notificationsForUser(memberId).empty());
        assert(!restored.activitiesForUser(adminId, teamId).empty());
    }

    {
        TemporaryDirectory legacyAccount;
        std::filesystem::create_directories(legacyAccount.path / "data");
        const auto legacyHash = util::hashPassword("legacyPass", "legacy_user");
        std::ofstream users(legacyAccount.path / "data" / "users.dat");
        users << "99001|Admin|Legacy User|legacy_user|" << legacyHash
              << "|legacy@test.local||2026-08-03\n";
        users.close();
        TeamSyncService legacy(legacyAccount.path);
        assert(!legacy.user(99001).securityQuestionsConfigured());
        expectThrows<AuthenticationError>([&] {
            legacy.resetPassword("legacy_user", {"School", "Nickname", "City"},
                                 "replacement", "replacement");
        });
        legacy.setSecurityAnswers(99001, "legacyPass", {"School", "Nickname", "City"});
        assert(legacy.user(99001).securityQuestionsConfigured());
        legacy.resetPassword("legacy_user", {" school ", "NICKNAME", "city"},
                             "replacement", "replacement");
        assert(legacy.login("legacy_user", "replacement").id() == 99001);
        legacy.saveAll();
        TeamSyncService migrated(legacyAccount.path);
        assert(migrated.user(99001).securityQuestionsConfigured());
    }

    { std::ofstream corrupt(temporary.path / "data" / "users.dat", std::ios::app);
      corrupt << "not|a|valid|record\n"; }
    TeamSyncService tolerant(temporary.path);
    assert(!tolerant.fileManager().warnings().empty());
    assert(tolerant.user(adminId).username() == "admin");

    {
        TemporaryDirectory legacy;
        std::filesystem::create_directories(legacy.path / "data");
        std::ofstream oldTasks(legacy.path / "data" / "tasks.dat");
        oldTasks << "General|99901|999|888|Legacy task||1|1|Medium|Pending|2026-08-03|2027-08-03||50||\n";
        oldTasks.close();
        FileManager files(legacy.path);
        auto migrated = files.loadTasks();
        assert(migrated.size() == 1);
        assert(migrated.front()->status() == TaskStatus::InProgress);
        files.saveTasks(migrated);
        std::ifstream rewritten(legacy.path / "data" / "tasks.dat");
        std::string firstLine;
        std::getline(rewritten, firstLine);
        assert(firstLine.rfind("v2|", 0) == 0);
        assert(firstLine.find("|50|") == std::string::npos);
    }

    {
        std::istringstream scriptedInput(
            "2\nadmin\npass123\n3\nback\ndashboard\n11\n4\n");
        std::ostringstream capturedOutput;
        TeamSyncApplication application(temporary.path, scriptedInput, capturedOutput);
        assert(application.run() == 0);
        const auto text = capturedOutput.str();
        assert(text.find("Returned to the dashboard") != std::string::npos);
        assert(text.find("--- Dashboard ---") != std::string::npos);
        assert(text.find("TeamSync closed safely") != std::string::npos);
    }

    std::cout << "All TeamSync tests passed.\n";
    return 0;
}
