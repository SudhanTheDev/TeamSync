#pragma once

#include "TeamSyncService.h"

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include <filesystem>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

class QComboBox;
class QCheckBox;
class QDialog;
class QEvent;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTableView;
class QStandardItemModel;
class QVBoxLayout;
class QToolButton;
class QTextEdit;
class QWidget;
class QResizeEvent;

namespace teamsync {

class LanSyncManager;
namespace ui { class TaskBoardWidget; }

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
        SettingsPage,
        PageCount
    };

    std::filesystem::path localRoot_;
    std::filesystem::path activeRoot_;
    std::unique_ptr<TeamSyncService> service_;
    std::unique_ptr<LanSyncManager> lan_;
    LanLaunchOptions networkOptions_;
    QString networkStartupError_;
    bool networkOnline_ = true;
    bool guestSession_ = false;
    bool networkBusy_ = false;
    bool operationInProgress_ = false;
    bool popupAnimations_ = true;
    QString sessionPassword_;
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
    QPushButton* connectionBadge_ = nullptr;
    QPushButton* pageHeaderAction_ = nullptr;
    QLineEdit* globalSearch_ = nullptr;
    QWidget* navigationIndicator_ = nullptr;
    QToolButton* profileChevron_ = nullptr;
    QDialog* chatDialog_ = nullptr;
    QDialog* notificationsDialog_ = nullptr;
    bool darkTheme_ = true;
    std::vector<QPushButton*> navigationButtons_;

    QLineEdit* loginUsername_ = nullptr;
    QLineEdit* loginPassword_ = nullptr;
    QCheckBox* rememberLogin_ = nullptr;
    QComboBox* themeChoice_ = nullptr;

    std::vector<QLabel*> dashboardValues_;
    QLabel* dashboardStatusSummary_ = nullptr;
    QListWidget* recentMessages_ = nullptr;
    QListWidget* recentActivities_ = nullptr;
    QLabel* dashboardWelcome_ = nullptr;
    QLabel* dashboardEmpty_ = nullptr;
    QWidget* dashboardMetrics_ = nullptr;
    QWidget* dashboardWork_ = nullptr;
    QTableView* myTasksView_ = nullptr;
    QStandardItemModel* myTasksModel_ = nullptr;
    QTableView* overviewProjectsView_ = nullptr;
    QStandardItemModel* overviewProjectsModel_ = nullptr;
    QVBoxLayout* projectRows_ = nullptr;
    QVBoxLayout* activityRows_ = nullptr;
    QVBoxLayout* updateRows_ = nullptr;
    std::vector<QLabel*> dashboardTrends_;
    int selectedOverviewProject_ = 0;

    QLineEdit* teamSearch_ = nullptr;
    QTableWidget* teamsTable_ = nullptr;
    QLineEdit* projectSearch_ = nullptr;
    QTableWidget* projectsTable_ = nullptr;
    QLineEdit* taskSearch_ = nullptr;
    QComboBox* taskFilter_ = nullptr;
    QTableWidget* tasksTable_ = nullptr;
    QStringList pendingTaskFiles_;
    QStackedWidget* taskViews_ = nullptr;
    ui::TaskBoardWidget* taskBoard_ = nullptr;

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
    QWidget* buildSettingsPage();

    void attemptLogin();
    void attemptRememberedLogin();
    void finishLogin(bool rememberLogin);
    void rememberLogin(const QString& username, const QString& password);
    void clearRememberedLogin();
    void showGuestDialog();
    void showRegistrationDialog();
    void showForgotPasswordDialog();
    void showTutorialAndCreditsDialog();
    void showSecurityQuestionsDialog();
    void logout();
    void navigateTo(Page page);
    void showGlobalSearch();
    void animatePage(QWidget* page);
    void animateWindowIn();
    void animatePopup(QDialog* dialog);
    void applyTheme(bool dark);
    void toggleTheme();
    void refreshSessionUi(bool refreshPage = true);
    bool runNetworkOperation(const std::function<bool()>& action, const QString& message,
                             LanSyncManager* operationManager = nullptr);
    void showTransferCenter();
    void showRecoveryDialog();
    void shareDroppedFiles(const QStringList& files);
    bool remoteReloadPending_ = false;
    bool backgroundSync_ = false;
    std::thread backupWorker_;

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

};

} // namespace teamsync
