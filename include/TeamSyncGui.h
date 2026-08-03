#pragma once

#include "TeamSyncService.h"

#include <QMainWindow>
#include <QString>

#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

class QComboBox;
class QDialog;
class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTextEdit;
class QWidget;
class QResizeEvent;

namespace teamsync {

class LanSyncManager;

struct LanLaunchOptions {
    enum class Mode { Local, Host, Client };
    Mode mode = Mode::Local;
    QString address;
    quint16 port = 45454;
    QString pairingCode;
};

class TeamSyncGui final : public QMainWindow {
public:
    explicit TeamSyncGui(const std::filesystem::path& applicationRoot,
                         LanLaunchOptions network = {},
                         QWidget* parent = nullptr);
    ~TeamSyncGui() override;

private:
    enum Page {
        DashboardPage,
        TeamsPage,
        ProjectsPage,
        TasksPage,
        FilesPage,
        ReportsPage,
        ActivityPage,
        ConnectionPage,
        TutorialPage,
        ProfilePage,
        PageCount
    };

    std::filesystem::path localRoot_;
    std::filesystem::path activeRoot_;
    std::unique_ptr<TeamSyncService> service_;
    std::unique_ptr<LanSyncManager> lan_;
    LanLaunchOptions networkOptions_;
    QString networkStartupError_;
    bool networkOnline_ = true;
    QString lastNetworkError_;
    User* currentUser_ = nullptr;

    QStackedWidget* rootStack_ = nullptr;
    QWidget* authPage_ = nullptr;
    QWidget* shellPage_ = nullptr;
    QStackedWidget* contentStack_ = nullptr;
    QLabel* pageTitle_ = nullptr;
    QLabel* pageSubtitle_ = nullptr;
    QLabel* signedInUser_ = nullptr;
    QLabel* networkStatus_ = nullptr;
    QLabel* connectionModeLabel_ = nullptr;
    QLabel* connectionDetailLabel_ = nullptr;
    QLabel* connectionPeersLabel_ = nullptr;
    QLabel* connectionRevisionLabel_ = nullptr;
    QListWidget* connectionPeersList_ = nullptr;
    QPushButton* notificationButton_ = nullptr;
    QPushButton* floatingChatButton_ = nullptr;
    QPushButton* authThemeButton_ = nullptr;
    QPushButton* shellThemeButton_ = nullptr;
    QDialog* chatDialog_ = nullptr;
    QDialog* notificationsDialog_ = nullptr;
    bool darkTheme_ = true;
    std::vector<QPushButton*> navigationButtons_;

    QLineEdit* loginUsername_ = nullptr;
    QLineEdit* loginPassword_ = nullptr;

    std::vector<QLabel*> dashboardValues_;
    QLabel* dashboardStatusSummary_ = nullptr;
    QListWidget* recentMessages_ = nullptr;
    QListWidget* recentActivities_ = nullptr;

    QLineEdit* teamSearch_ = nullptr;
    QTableWidget* teamsTable_ = nullptr;
    QLineEdit* projectSearch_ = nullptr;
    QTableWidget* projectsTable_ = nullptr;
    QLineEdit* taskSearch_ = nullptr;
    QComboBox* taskFilter_ = nullptr;
    QTableWidget* tasksTable_ = nullptr;

    QComboBox* chatTeam_ = nullptr;
    QListWidget* chatMessages_ = nullptr;
    QLineEdit* chatInput_ = nullptr;

    QComboBox* filesTeam_ = nullptr;
    QTableWidget* filesTable_ = nullptr;
    QTableWidget* notificationsTable_ = nullptr;

    QComboBox* reportTeam_ = nullptr;
    QComboBox* reportType_ = nullptr;
    QTextEdit* reportPreview_ = nullptr;

    QComboBox* activityTeam_ = nullptr;
    QTableWidget* activityTable_ = nullptr;

    QLineEdit* profileName_ = nullptr;
    QLineEdit* profileUsername_ = nullptr;
    QLineEdit* profileEmail_ = nullptr;
    QLineEdit* profileSkills_ = nullptr;
    QLabel* profileRole_ = nullptr;

    void buildInterface();
    QWidget* buildAuthPage();
    QWidget* buildShellPage();
    QWidget* buildDashboardPage();
    QWidget* buildTeamsPage();
    QWidget* buildProjectsPage();
    QWidget* buildTasksPage();
    QWidget* buildChatPage();
    QWidget* buildFilesPage();
    QWidget* buildNotificationsPage();
    QWidget* buildReportsPage();
    QWidget* buildActivityPage();
    QWidget* buildConnectionPage();
    QWidget* buildTutorialPage();
    QWidget* buildProfilePage();

    void attemptLogin();
    void showRegistrationDialog();
    void showForgotPasswordDialog();
    void showTutorialAndCreditsDialog();
    void showSecurityQuestionsDialog();
    void logout();
    void navigateTo(Page page);
    void animatePage(QWidget* page);
    void animateWindowIn();
    void animatePopup(QDialog* dialog);
    void applyTheme(bool dark);
    void toggleTheme();

    void refreshCurrentPage();
    void refreshDashboard();
    void refreshTeams();
    void refreshProjects();
    void refreshTasks();
    void refreshTeamSelectors();
    void refreshChat();
    void refreshFiles();
    void refreshNotifications();
    void updateNotificationButton();
    void refreshActivities();
    void refreshConnection();
    void refreshProfile();

    void startHostingDialog();
    void showJoinWorkspaceDialog();
    void switchToLocalWorkspace();
    void configureLanCallbacks();
    void updateConnectionUi();
    void showConnectionToast(const QString& message, bool success = true);
    std::filesystem::path joinedCacheRoot(const QString& address, quint16 port) const;

    void showCreateTeamDialog();
    void joinTeam();
    void showTeamDetails();
    void leaveSelectedTeam();
    void showProjectDialog(bool editExisting);
    void archiveSelectedProject();
    void showTaskDialog(bool editExisting);
    void startSelectedTask();
    void completeSelectedTask();
    void reopenSelectedTask();
    void showCompletionDetails();
    void showTaskComments();
    void showRecommendations();
    void deleteSelectedTask();
    void sendChatMessage();
    void deleteSelectedMessage();
    void showChatPanel();
    void showNotificationsPanel();
    void shareFile();
    void openSelectedFile();
    void removeSelectedFile();
    void generateReport();
    void exportReport();
    void saveProfile();
    void showChangePasswordDialog();

    bool perform(const std::function<void()>& action,
                 const QString& successMessage = {});
    bool pullNetworkChanges(bool showError);
    void reloadNetworkData();
    int selectedId(QTableWidget* table) const;
    int currentUserId() const;
    void showStatus(const QString& message, bool success = true);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
};

} // namespace teamsync
