#pragma once

#include "Exceptions.h"
#include "Input.h"
#include "TeamSyncService.h"

#include <filesystem>
#include <exception>
#include <iosfwd>
#include <string>
#include <vector>

namespace teamsync {

class TeamSyncApplication {
public:
    TeamSyncApplication(const std::filesystem::path& applicationRoot, std::istream& in,
                        std::ostream& out);
    int run();

private:
    TeamSyncService service_;
    Input input_;
    std::ostream& out_;
    User* currentUser_ = nullptr;

    void showBanner() const;
    void registerAccount();
    void recoverPassword();
    bool login();
    void sessionMenu();
    void profileMenu();
    void teamsMenu();
    void projectsMenu();
    void tasksMenu();
    void chatMenu();
    void filesMenu();
    void notificationsMenu();
    void reportsMenu();
    void activitiesMenu();

    int currentUserId() const;
    int chooseTeam(const std::string& prompt = "Team ID: ");
    void listMyTeams() const;
    void displayTeamDetails(int teamId) const;
    void listAccessibleProjects() const;
    void displayTasks(const std::vector<const Task*>& tasks) const;
    void displayTasks(const std::vector<Task*>& tasks) const;
    void displayMessages(const std::vector<Message>& messages) const;
    TaskPriority choosePriority();
    TaskStatus chooseStatus();
    TaskType chooseTaskType();
    template <typename Function>
    void safely(Function&& action) {
        try {
            action();
        } catch (const NavigationRequest& navigation) {
            if (navigation.target() == NavigationTarget::Dashboard) throw;
            out_ << "\nReturned to the previous menu.\n";
        } catch (const std::exception& error) {
            out_ << "\nError: " << error.what() << "\n";
        }
    }
};

} // namespace teamsync
