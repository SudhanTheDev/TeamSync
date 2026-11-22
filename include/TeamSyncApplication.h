#pragma once

#include "Exceptions.h"
#include "Input.h"
#include "ConsoleLanSync.h"
#include "TeamSyncService.h"

#include <filesystem>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <thread>
#include <vector>

namespace teamsync {

class TeamSyncApplication {
public:
    TeamSyncApplication(const std::filesystem::path& applicationRoot, std::istream& in,
                        std::ostream& out);
    ~TeamSyncApplication();
    int run();

private:
    std::filesystem::path localRoot_;
    std::filesystem::path activeRoot_;
    std::unique_ptr<TeamSyncService> service_;
    Input input_;
    std::istream& in_;
    std::ostream& out_;
    std::unique_ptr<ConsoleLanSync> lan_;
    User* currentUser_ = nullptr;
    std::string sessionPassword_;
    std::string savedUsername_;
    std::string savedPassword_;
    bool guestSession_ = false;
    bool rememberLogin_ = true;
    bool exiting_ = false;
    bool syncInProgress_ = false;
    std::atomic_bool remoteWorkspaceChanged_{false};
    std::mutex networkMutex_;
    std::mutex idleStateMutex_;
    std::condition_variable idleCondition_;
    bool idleStopping_ = false;
    bool idleBusy_ = false;
    std::thread idleThread_;
    std::thread::id mainThreadId_ = std::this_thread::get_id();

    void showBanner() const;
    void registerAccount();
    void recoverPassword();
    bool login();
    void guestLogin();
    void rememberedLogin();
    void saveRememberedLogin();
    void clearRememberedLogin();
    void rememberSettings();
    void reloadAuthenticated(const std::string& username, const std::string& password);
    void prepareLocalMutation();
    void switchToLocal();
    void recoveryMenu();
    void taskBoardMenu();
    void searchMenu();
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
    void connectionMenu();
    void syncNetwork(bool localAction = false);
    void configureTransport(ConsoleLanSync& transport);
    void startIdleSync();
    void stopIdleSync();
    void tutorialAndCredits() const;

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
    decltype(auto) localMutation(Function&& action) {
        using Result = decltype(action());
        if constexpr (std::is_void_v<Result>) {
            lan_->executeLocalMutation([&] { prepareLocalMutation(); action(); lan_->noteLocalChange(); });
        } else if constexpr (std::is_reference_v<Result>) {
            std::remove_reference_t<Result>* result = nullptr;
            lan_->executeLocalMutation([&] { prepareLocalMutation(); result = &action(); lan_->noteLocalChange(); });
            return *result;
        } else {
            std::optional<Result> result;
            lan_->executeLocalMutation([&] { prepareLocalMutation(); result = action(); lan_->noteLocalChange(); });
            return Result(std::move(*result));
        }
    }
    template <typename Function>
    void safely(Function&& action) {
        try {
            const bool wasSignedIn = currentUser_ != nullptr;
            if (!syncInProgress_) syncNetwork();
            if (wasSignedIn && !currentUser_) throw AuthenticationError("Please sign in again before continuing.");
            action();
            if (!syncInProgress_) syncNetwork(true);
        } catch (const NavigationRequest& navigation) {
            if (navigation.target() == NavigationTarget::Dashboard) throw;
            out_ << "\nReturned to the previous menu.\n";
        } catch (const std::exception& error) {
            out_ << "\nError: " << error.what() << "\n";
        }
    }
};

} // namespace teamsync
