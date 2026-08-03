#include "TeamSyncGui.h"

#include "Dashboard.h"
#include "Exceptions.h"
#include "LanSyncManager.h"
#include "ReportGenerator.h"
#include "Utils.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDate>
#include <QDateEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontInfo>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QParallelAnimationGroup>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QScrollArea>
#include <QResizeEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTableWidget>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <sstream>

namespace teamsync {
namespace {

QString q(const std::string& value) {
    return QString::fromUtf8(value.c_str());
}

std::string s(const QString& value) {
    const auto bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

Date toTeamSyncDate(const QDate& value) {
    return Date(value.day(), value.month(), value.year());
}

QDate toQDate(const Date& value) {
    return QDate(value.year(), value.month(), value.day());
}

QPushButton* button(const QString& text, const bool primary = false) {
    auto* result = new QPushButton(text);
    result->setCursor(Qt::PointingHandCursor);
    result->setMinimumHeight(38);
    result->setProperty("primary", primary);
    return result;
}

QTableWidget* table(const QStringList& headings) {
    auto* result = new QTableWidget(0, headings.size());
    result->setHorizontalHeaderLabels(headings);
    result->horizontalHeader()->setStretchLastSection(true);
    result->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    result->setSelectionBehavior(QAbstractItemView::SelectRows);
    result->setSelectionMode(QAbstractItemView::SingleSelection);
    result->setEditTriggers(QAbstractItemView::NoEditTriggers);
    result->setAlternatingRowColors(true);
    result->verticalHeader()->setVisible(false);
    result->setShowGrid(false);
    return result;
}

QLabel* mutedLabel(const QString& text) {
    auto* result = new QLabel(text);
    result->setObjectName("mutedLabel");
    result->setWordWrap(true);
    return result;
}

QFrame* card() {
    auto* result = new QFrame;
    result->setObjectName("card");
    auto* shadow = new QGraphicsDropShadowEffect(result);
    shadow->setBlurRadius(24);
    shadow->setOffset(0, 8);
    shadow->setColor(QColor(3, 8, 25, 90));
    result->setGraphicsEffect(shadow);
    return result;
}

QIcon chatBubbleIcon() {
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::white, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(QRectF(4, 4, 20, 15), 5, 5);
    painter.drawPolyline(QPolygonF({QPointF(10, 19), QPointF(8, 24), QPointF(15, 19)}));
    return QIcon(pixmap);
}

QIcon notificationBellIcon() {
    QPixmap pixmap(26, 26);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor("#dce5f8"), 2.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawArc(QRectF(6, 5, 14, 16), 0, 180 * 16);
    painter.drawLine(QPointF(6, 13), QPointF(4, 19));
    painter.drawLine(QPointF(4, 19), QPointF(22, 19));
    painter.drawLine(QPointF(22, 19), QPointF(20, 13));
    painter.drawArc(QRectF(11, 19, 4, 4), 180 * 16, 180 * 16);
    return QIcon(pixmap);
}

QIcon materialGlyphIcon(const ushort glyph, const QColor& color) {
    QPixmap pixmap(30, 30);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(color);
    QFont font("Segoe Fluent Icons", 14);
    if (!QFontInfo(font).exactMatch()) font.setFamily("Segoe MDL2 Assets");
    painter.setFont(font);
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QString(QChar(glyph)));
    return QIcon(pixmap);
}

QString joined(const std::vector<std::string>& values) {
    QStringList result;
    for (const auto& value : values) result << q(value);
    return result.join(", ");
}

} // namespace

TeamSyncGui::TeamSyncGui(const std::filesystem::path& applicationRoot,
                         LanLaunchOptions network, QWidget* parent)
    : QMainWindow(parent), localRoot_(std::filesystem::absolute(applicationRoot)),
      activeRoot_(localRoot_), service_(std::make_unique<TeamSyncService>(localRoot_)),
      lan_(std::make_unique<LanSyncManager>(localRoot_)),
      networkOptions_(std::move(network)) {
    QSettings settings("TeamSync", "TeamSync");
    darkTheme_ = settings.value("appearance/darkTheme", true).toBool();
    setWindowTitle("TeamSync - Collaboration Workspace");
    resize(1420, 900);
    setMinimumSize(1120, 720);

    QPixmap iconPixmap(64, 64);
    iconPixmap.fill(Qt::transparent);
    QPainter painter(&iconPixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient gradient(0, 0, 64, 64);
    gradient.setColorAt(0, QColor("#7c5cff"));
    gradient.setColorAt(1, QColor("#16b8f3"));
    painter.setBrush(gradient);
    painter.setPen(Qt::NoPen);
    painter.drawRoundedRect(3, 3, 58, 58, 16, 16);
    painter.setPen(Qt::white);
    painter.setFont(QFont("Segoe UI", 18, QFont::Bold));
    painter.drawText(iconPixmap.rect(), Qt::AlignCenter, "TS");
    setWindowIcon(QIcon(iconPixmap));

    setStyleSheet(R"QSS(
        * { font-family: "Segoe UI"; font-size: 10pt; }
        QMainWindow, QWidget { background: #0a1020; color: #e9edfa; }
        QLabel { background: transparent; }
        #authPage { background: #090f1d; }
        #brandPanel {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:1,
                        stop:0 #16113a, stop:0.52 #172d57, stop:1 #087ca8);
            border-radius: 28px;
        }
        #brandTitle { font-size: 36pt; font-weight: 800; color: white; }
        #brandTagline { font-size: 16pt; font-weight: 600; color: #d8e8ff; }
        #featureChip {
            background: rgba(255,255,255,0.10); border: 1px solid rgba(255,255,255,0.14);
            border-radius: 13px; color: #eaf4ff; padding: 10px 14px;
        }
        #authCard, #card {
            background: #111a2e; border: 1px solid #243352; border-radius: 18px;
        }
        #authHeading { font-size: 24pt; font-weight: 750; color: white; }
        #shellPage, #pageArea { background: #0d1426; }
        #sidebar { background: #0b1120; border-right: 1px solid #202b43; }
        #sidebarLogo { font-size: 20pt; font-weight: 800; color: white; }
        #sidebarUser { background: #131e34; border: 1px solid #263653; border-radius: 13px; padding: 12px; }
        #pageHeader { background: #0d1426; border-bottom: 1px solid #202b43; }
        #pageTitle { font-size: 21pt; font-weight: 750; color: white; }
        #pageSubtitle, #mutedLabel { color: #94a2bd; }
        #sectionTitle { font-size: 14pt; font-weight: 700; color: #f2f5ff; }
        #metricValue { font-size: 25pt; font-weight: 800; color: white; }
        #metricLabel { color: #9da9c2; font-weight: 600; }
        #metricAccent { color: #8ee1ff; font-size: 9pt; }
        #emptyState { color: #76849f; padding: 24px; }
        QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QDateEdit, QSpinBox {
            background: #0d1528; color: #edf2ff; border: 1px solid #2a3a59;
            border-radius: 9px; padding: 8px 10px; min-height: 20px;
            selection-background-color: #6f5cf4;
        }
        QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus,
        QDateEdit:focus, QSpinBox:focus { border: 1px solid #7a68ff; }
        QComboBox::drop-down { border: none; width: 28px; }
        QComboBox QAbstractItemView { background: #131e34; color: #edf2ff; selection-background-color: #5f50cf; }
        QPushButton {
            background: #1a263e; color: #dce5f8; border: 1px solid #30415f;
            border-radius: 9px; padding: 8px 14px; font-weight: 650;
        }
        QPushButton:hover { background: #253451; border-color: #546b93; }
        QPushButton:pressed { background: #101a2e; }
        QPushButton[primary="true"] {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #7658ee,stop:1 #1e9bd7);
            border: none; color: white;
        }
        QPushButton[primary="true"]:hover { background: #6955de; }
        QPushButton#navButton {
            background: transparent; border: none; color: #96a4bd; text-align: left;
            padding: 11px 15px; border-radius: 10px; font-weight: 650;
        }
        QPushButton#navButton:hover { background: #141f35; color: white; }
        QPushButton#navButton:checked {
            background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #302866,stop:1 #183c58);
            color: white; border-left: 3px solid #826eff;
        }
        QPushButton#linkButton { background:transparent; border:none; color:#8fcfff; padding:4px; text-align:right; }
        QPushButton#linkButton:hover { color:white; text-decoration:underline; }
        QPushButton#notificationButton { min-width:48px; padding:8px 11px; }
        QPushButton#notificationButton[hasUnread="true"] { color:white; border-color:#7564ee; background:#282455; }
        QPushButton#floatingChatButton {
            border-radius:27px; min-width:54px; max-width:54px; min-height:54px; max-height:54px;
            background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #7658ee,stop:1 #168fc7);
            border:1px solid #8d7aff;
        }
        QPushButton#floatingChatButton:hover { background:#826df3; border-color:#b4a9ff; }
        QPushButton#dangerButton, QPushButton#logoutButton { color: #ffb5bc; border-color: #6e3442; background: #2c1721; }
        QTableWidget {
            background: #10192c; alternate-background-color: #121e33; border: 1px solid #263550;
            border-radius: 12px; color: #e8eefc; selection-background-color: #354075;
            selection-color: white; outline: none;
        }
        QHeaderView::section {
            background: #16223a; color: #aebbd0; border: none; border-bottom: 1px solid #2b3b58;
            padding: 10px; font-weight: 700;
        }
        QTableWidget::item { padding: 8px; border-bottom: 1px solid #1e2b43; }
        QTableWidget::item:hover { background:#1b2944; }
        QListWidget {
            background: #10192c; border: 1px solid #263550; border-radius: 12px;
            color: #e7edfb; outline: none; padding: 6px;
        }
        QListWidget::item { padding: 10px; border-radius: 8px; margin: 2px; }
        QListWidget::item:selected { background: #354075; }
        QProgressBar {
            background: #0c1425; border: 1px solid #283955; border-radius: 8px;
            text-align: center; color: white; min-height: 17px;
        }
        QProgressBar::chunk { border-radius: 7px; background: qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #735bf1,stop:1 #22b5df); }
        QGroupBox { border: 1px solid #263550; border-radius: 12px; margin-top: 14px; padding: 14px; font-weight: 700; }
        QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #cbd5eb; }
        QDialog { background: #0d1426; }
        QDialogButtonBox QPushButton { min-width: 90px; }
        QMessageBox { background: #111a2e; }
        QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
        QScrollBar::handle:vertical { background: #34445f; border-radius: 5px; min-height: 30px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
        QStatusBar { background: #0b1120; color: #93a2bd; border-top: 1px solid #202b43; }
        QToolTip { background: #17223a; color: white; border: 1px solid #415272; padding: 6px; }
    )QSS");

    if (networkOptions_.mode == LanLaunchOptions::Mode::Host) {
        lan_->startHost(networkOptions_.port, networkStartupError_, networkOptions_.pairingCode);
    } else if (networkOptions_.mode == LanLaunchOptions::Mode::Client) {
        const auto cacheRoot = joinedCacheRoot(networkOptions_.address, networkOptions_.port);
        auto candidate = std::make_unique<LanSyncManager>(cacheRoot);
        if (candidate->joinHost(networkOptions_.address, networkOptions_.port,
                                networkOptions_.pairingCode, networkStartupError_)) {
            activeRoot_ = cacheRoot;
            service_ = std::make_unique<TeamSyncService>(activeRoot_);
            lan_ = std::move(candidate);
        }
    }

    buildInterface();
    configureLanCallbacks();
    auto* timer = new QTimer(this);
    timer->setInterval(2000);
    connect(timer, &QTimer::timeout, this, [this] {
        pullNetworkChanges(false);
        updateConnectionUi();
    });
    timer->start();
    animateWindowIn();

    if (!networkStartupError_.isEmpty()) {
        QTimer::singleShot(250, this, [this] {
            QMessageBox::critical(this, "TeamSync network", networkStartupError_ +
                "\n\nTeamSync opened a local workspace instead.");
        });
    }

    if (!service_->fileManager().warnings().empty()) {
        QTimer::singleShot(250, this, [this] {
            QString details;
            for (const auto& warning : service_->fileManager().warnings()) details += "• " + q(warning) + "\n";
            QMessageBox::warning(this, "Data recovery notice",
                                 "TeamSync skipped one or more malformed saved records:\n\n" + details);
        });
    }
}

TeamSyncGui::~TeamSyncGui() = default;

void TeamSyncGui::buildInterface() {
    rootStack_ = new QStackedWidget;
    rootStack_->setObjectName("rootStack");
    authPage_ = buildAuthPage();
    shellPage_ = buildShellPage();
    rootStack_->addWidget(authPage_);
    rootStack_->addWidget(shellPage_);
    setCentralWidget(rootStack_);
    applyTheme(darkTheme_);
    statusBar()->showMessage("Ready");
}

QWidget* TeamSyncGui::buildAuthPage() {
    auto* page = new QWidget;
    page->setObjectName("authPage");
    auto* outer = new QHBoxLayout(page);
    outer->setContentsMargins(34, 34, 34, 34);
    outer->setSpacing(34);

    auto* brand = new QFrame;
    brand->setObjectName("brandPanel");
    brand->setMinimumWidth(500);
    auto* brandLayout = new QVBoxLayout(brand);
    brandLayout->setContentsMargins(52, 52, 52, 52);
    brandLayout->setSpacing(18);
    auto* logo = new QLabel("TS  TeamSync");
    logo->setObjectName("brandTitle");
    brandLayout->addWidget(logo);
    brandLayout->addSpacing(45);
    auto* tagline = new QLabel("One workspace. Clear ownership.\nBetter teamwork.");
    tagline->setObjectName("brandTagline");
    tagline->setWordWrap(true);
    brandLayout->addWidget(tagline);
    brandLayout->addWidget(mutedLabel("Plan projects, assign work, share updates, and understand status without losing the human context."));
    brandLayout->addSpacing(22);
    for (const QString& feature : {"Live project and task overview", "Focused team conversations", "Smart workload recommendations"}) {
        auto* chip = new QLabel("  ✓  " + feature);
        chip->setObjectName("featureChip");
        brandLayout->addWidget(chip);
    }
    brandLayout->addStretch();
    auto* privacy = new QLabel("LOCAL-FIRST  •  OPTIONAL TRUSTED LAN SHARING");
    privacy->setObjectName("privacyLabel");
    brandLayout->addWidget(privacy);

    auto* loginArea = new QWidget;
    auto* loginAreaLayout = new QVBoxLayout(loginArea);
    loginAreaLayout->setContentsMargins(24, 34, 24, 34);
    authThemeButton_ = button("");
    authThemeButton_->setObjectName("themeButton");
    authThemeButton_->setToolTip("Switch between dark and light theme");
    loginAreaLayout->addWidget(authThemeButton_, 0, Qt::AlignRight);
    loginAreaLayout->addStretch();
    auto* loginCard = card();
    loginCard->setObjectName("authCard");
    loginCard->setMinimumWidth(440);
    loginCard->setMaximumWidth(500);
    auto* formLayout = new QVBoxLayout(loginCard);
    formLayout->setContentsMargins(30, 28, 30, 28);
    formLayout->setSpacing(9);
    auto* heading = new QLabel("Welcome back");
    heading->setObjectName("authHeading");
    formLayout->addWidget(heading);
    formLayout->addWidget(mutedLabel("Sign in to continue to your TeamSync workspace."));
    formLayout->addSpacing(10);
    formLayout->addWidget(new QLabel("Username"));
    loginUsername_ = new QLineEdit;
    loginUsername_->setObjectName("loginUsername");
    loginUsername_->setPlaceholderText("Enter your username");
    loginUsername_->setClearButtonEnabled(true);
    formLayout->addWidget(loginUsername_);
    formLayout->addWidget(new QLabel("Password"));
    loginPassword_ = new QLineEdit;
    loginPassword_->setObjectName("loginPassword");
    loginPassword_->setPlaceholderText("Enter your password");
    loginPassword_->setEchoMode(QLineEdit::Password);
    formLayout->addWidget(loginPassword_);
    auto* forgotPassword = new QPushButton("Forgot password?");
    forgotPassword->setObjectName("linkButton");
    forgotPassword->setCursor(Qt::PointingHandCursor);
    forgotPassword->setToolTip("Reset using the email saved on your account");
    formLayout->addWidget(forgotPassword, 0, Qt::AlignRight);
    auto* login = button("Sign in", true);
    login->setObjectName("loginButton");
    login->setMinimumHeight(44);
    formLayout->addWidget(login);
    auto* registerButton = button("Create a new account");
    formLayout->addWidget(registerButton);
    auto* tutorial = button("Tutorial && Credits");
    tutorial->setObjectName("tonalButton");
    formLayout->addWidget(tutorial);
    auto* connectionBox = new QFrame;
    connectionBox->setObjectName("connectionPanel");
    connectionBox->setMinimumHeight(125);
    auto* connectionLayout = new QVBoxLayout(connectionBox);
    connectionLayout->setContentsMargins(12, 10, 12, 10);
    connectionLayout->setSpacing(7);
    auto* connectionTitle = new QLabel("Workspace connection");
    connectionTitle->setObjectName("connectionPanelTitle");
    connectionLayout->addWidget(connectionTitle);
    networkStatus_ = mutedLabel("Connection: " + lan_->statusText());
    networkStatus_->setWordWrap(true);
    networkStatus_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    connectionLayout->addWidget(networkStatus_);
    auto* connectionActions = new QHBoxLayout;
    auto* local = button("Local");
    local->setObjectName("localConnectionButton");
    auto* host = button("Host", true);
    host->setObjectName("hostConnectionButton");
    auto* join = button("Join");
    join->setObjectName("joinConnectionButton");
    connectionActions->addWidget(local);
    connectionActions->addWidget(host);
    connectionActions->addWidget(join);
    connectionLayout->addLayout(connectionActions);
    formLayout->addWidget(connectionBox);
    loginAreaLayout->addWidget(loginCard, 0, Qt::AlignHCenter);
    loginAreaLayout->addStretch();

    outer->addWidget(brand, 11);
    outer->addWidget(loginArea, 9);

    connect(login, &QPushButton::clicked, this, [this] { attemptLogin(); });
    connect(loginPassword_, &QLineEdit::returnPressed, this, [this] { attemptLogin(); });
    connect(loginUsername_, &QLineEdit::returnPressed, loginPassword_, qOverload<>(&QWidget::setFocus));
    connect(registerButton, &QPushButton::clicked, this, [this] { showRegistrationDialog(); });
    connect(forgotPassword, &QPushButton::clicked, this, [this] { showForgotPasswordDialog(); });
    connect(tutorial, &QPushButton::clicked, this, [this] { showTutorialAndCreditsDialog(); });
    connect(authThemeButton_, &QPushButton::clicked, this, [this] { toggleTheme(); });
    connect(local, &QPushButton::clicked, this, [this] { switchToLocalWorkspace(); });
    connect(host, &QPushButton::clicked, this, [this] { startHostingDialog(); });
    connect(join, &QPushButton::clicked, this, [this] { showJoinWorkspaceDialog(); });
    return page;
}

QWidget* TeamSyncGui::buildShellPage() {
    auto* page = new QWidget;
    page->setObjectName("shellPage");
    auto* shell = new QHBoxLayout(page);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);

    auto* sidebar = new QFrame;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(246);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(18, 24, 18, 20);
    sidebarLayout->setSpacing(7);
    auto* logo = new QLabel("TS  TeamSync");
    logo->setObjectName("sidebarLogo");
    sidebarLayout->addWidget(logo);
    sidebarLayout->addSpacing(18);
    signedInUser_ = new QLabel("Not signed in");
    signedInUser_->setObjectName("sidebarUser");
    signedInUser_->setWordWrap(true);
    sidebarLayout->addWidget(signedInUser_);
    sidebarLayout->addSpacing(14);

    const QStringList labels = {
        "Overview", "Teams", "Projects", "Tasks", "Shared files",
        "Reports", "Activity", "Connection", "Tutorial && Credits", "My profile"
    };
    auto* group = new QButtonGroup(sidebar);
    group->setExclusive(true);
    for (int index = 0; index < labels.size(); ++index) {
        auto* nav = new QPushButton(labels[index]);
        nav->setObjectName("navButton");
        nav->setCheckable(true);
        nav->setCursor(Qt::PointingHandCursor);
        nav->setMinimumHeight(42);
        group->addButton(nav, index);
        sidebarLayout->addWidget(nav);
        navigationButtons_.push_back(nav);
        connect(nav, &QPushButton::clicked, this, [this, index] { navigateTo(static_cast<Page>(index)); });
    }
    sidebarLayout->addStretch();
    auto* logoutButton = button("Sign out");
    logoutButton->setObjectName("logoutButton");
    sidebarLayout->addWidget(logoutButton);
    connect(logoutButton, &QPushButton::clicked, this, [this] { logout(); });

    auto* pageArea = new QWidget;
    pageArea->setObjectName("pageArea");
    auto* pageLayout = new QVBoxLayout(pageArea);
    pageLayout->setContentsMargins(0, 0, 0, 0);
    pageLayout->setSpacing(0);
    auto* header = new QFrame;
    header->setObjectName("pageHeader");
    header->setFixedHeight(92);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(30, 14, 30, 14);
    auto* titles = new QVBoxLayout;
    pageTitle_ = new QLabel("Overview");
    pageTitle_->setObjectName("pageTitle");
    pageSubtitle_ = new QLabel("Your workspace at a glance");
    pageSubtitle_->setObjectName("pageSubtitle");
    titles->addWidget(pageTitle_);
    titles->addWidget(pageSubtitle_);
    headerLayout->addLayout(titles);
    headerLayout->addStretch();
    notificationButton_ = button("");
    notificationButton_->setObjectName("notificationButton");
    notificationButton_->setIcon(notificationBellIcon());
    notificationButton_->setIconSize(QSize(24, 24));
    notificationButton_->setToolTip("Open notifications");
    headerLayout->addWidget(notificationButton_);
    shellThemeButton_ = button("");
    shellThemeButton_->setObjectName("themeButton");
    shellThemeButton_->setToolTip("Switch between dark and light theme");
    headerLayout->addWidget(shellThemeButton_);
    auto* refresh = button("Refresh");
    headerLayout->addWidget(refresh);
    connect(refresh, &QPushButton::clicked, this, [this] { refreshCurrentPage(); showStatus("Workspace refreshed"); });
    connect(notificationButton_, &QPushButton::clicked, this, [this] { showNotificationsPanel(); });
    connect(shellThemeButton_, &QPushButton::clicked, this, [this] { toggleTheme(); });
    pageLayout->addWidget(header);

    contentStack_ = new QStackedWidget;
    contentStack_->setObjectName("contentStack");
    contentStack_->setContentsMargins(0, 0, 0, 0);
    contentStack_->addWidget(buildDashboardPage());
    contentStack_->addWidget(buildTeamsPage());
    contentStack_->addWidget(buildProjectsPage());
    contentStack_->addWidget(buildTasksPage());
    contentStack_->addWidget(buildFilesPage());
    contentStack_->addWidget(buildReportsPage());
    contentStack_->addWidget(buildActivityPage());
    contentStack_->addWidget(buildConnectionPage());
    contentStack_->addWidget(buildTutorialPage());
    contentStack_->addWidget(buildProfilePage());
    pageLayout->addWidget(contentStack_, 1);

    floatingChatButton_ = new QPushButton(pageArea);
    floatingChatButton_->setObjectName("floatingChatButton");
    floatingChatButton_->setIcon(chatBubbleIcon());
    floatingChatButton_->setIconSize(QSize(28, 28));
    floatingChatButton_->setFixedSize(58, 58);
    floatingChatButton_->setCursor(Qt::PointingHandCursor);
    floatingChatButton_->setToolTip("Open team chat");
    floatingChatButton_->setAccessibleName("Open team chat");
    auto* chatShadow = new QGraphicsDropShadowEffect(floatingChatButton_);
    chatShadow->setBlurRadius(28);
    chatShadow->setOffset(0, 8);
    chatShadow->setColor(QColor(73, 89, 255, 150));
    floatingChatButton_->setGraphicsEffect(chatShadow);
    floatingChatButton_->raise();
    pageArea->installEventFilter(this);
    connect(floatingChatButton_, &QPushButton::clicked, this, [this] { showChatPanel(); });

    shell->addWidget(sidebar);
    shell->addWidget(pageArea, 1);
    QTimer::singleShot(0, this, [this] {
        if (floatingChatButton_ && floatingChatButton_->parentWidget()) {
            floatingChatButton_->move(floatingChatButton_->parentWidget()->width() - 88,
                                      floatingChatButton_->parentWidget()->height() - 88);
            floatingChatButton_->raise();
        }
    });
    return page;
}

bool TeamSyncGui::perform(const std::function<void()>& action, const QString& successMessage) {
    try {
        if (lan_->mode() == LanSyncManager::Mode::Client) {
            bool changed = false;
            QString networkError;
            if (!lan_->pull(changed, networkError)) {
                networkOnline_ = false;
                lastNetworkError_ = networkError;
                updateConnectionUi();
                throw PersistenceError(s(networkError));
            }
            networkOnline_ = true;
            lastNetworkError_.clear();
            if (changed) reloadNetworkData();
        }
        const auto before = lan_->mode() == LanSyncManager::Mode::Local
            ? QByteArray{} : lan_->workspaceFingerprint();
        action();
        if (lan_->mode() != LanSyncManager::Mode::Local &&
            before != lan_->workspaceFingerprint()) {
            if (lan_->mode() == LanSyncManager::Mode::Host) {
                lan_->noteLocalChange();
            } else {
                QString networkError;
                if (!lan_->push(networkError)) {
                    networkOnline_ = false;
                    lastNetworkError_ = networkError;
                    const int userId = currentUser_ ? currentUser_->id() : 0;
                    service_->reload();
                    currentUser_ = nullptr;
                    if (userId > 0) {
                        try { currentUser_ = &service_->user(userId); } catch (...) {}
                    }
                    throw PersistenceError(s(networkError));
                }
            }
        }
        if (currentUser_) updateNotificationButton();
        if (!successMessage.isEmpty()) showStatus(successMessage);
        return true;
    } catch (const std::exception& error) {
        showStatus(q(error.what()), false);
        QMessageBox::critical(this, "TeamSync", q(error.what()));
        return false;
    }
}

bool TeamSyncGui::pullNetworkChanges(const bool showError) {
    if (lan_->mode() != LanSyncManager::Mode::Client) return true;
    bool changed = false;
    QString networkError;
    if (!lan_->pull(changed, networkError)) {
        networkOnline_ = false;
        lastNetworkError_ = networkError;
        updateConnectionUi();
        showStatus("Network sync paused: " + networkError, false);
        if (showError) QMessageBox::warning(this, "TeamSync network", networkError);
        return false;
    }
    networkOnline_ = true;
    lastNetworkError_.clear();
    if (changed) reloadNetworkData();
    return true;
}

void TeamSyncGui::reloadNetworkData() {
    const int userId = currentUser_ ? currentUser_->id() : 0;
    service_->reload();
    currentUser_ = nullptr;
    if (userId > 0) {
        try { currentUser_ = &service_->user(userId); } catch (...) {}
    }
    if (networkStatus_) networkStatus_->setText("Connection: " + lan_->statusText());
    updateConnectionUi();
    if (currentUser_) {
        refreshTeamSelectors();
        updateNotificationButton();
        refreshCurrentPage();
        if (chatDialog_ && chatDialog_->isVisible()) refreshChat();
        if (notificationsDialog_ && notificationsDialog_->isVisible()) refreshNotifications();
        showStatus("New changes received from the shared workspace");
    }
}

std::filesystem::path TeamSyncGui::joinedCacheRoot(const QString& address, const quint16 port) const {
    const std::string identity = s(address.trimmed()) + ':' + std::to_string(port);
    return localRoot_ / "network_cache" / std::to_string(std::hash<std::string>{}(identity));
}

void TeamSyncGui::configureLanCallbacks() {
    lan_->setRemoteCommitHandler([this] {
        reloadNetworkData();
        updateConnectionUi();
        showStatus("A connected computer synchronized new changes");
    });
}

void TeamSyncGui::startHostingDialog() {
    if (currentUser_) {
        QMessageBox::information(this, "Connection mode", "Sign out before changing the workspace connection.");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle("Host this workspace");
    dialog.setMinimumWidth(480);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(mutedLabel("Other computers on the same Wi-Fi or hotspot will join this computer. Keep TeamSync open while collaborating."));
    auto* form = new QFormLayout;
    auto* port = new QSpinBox;
    port->setObjectName("hostPortSpin");
    port->setRange(1024, 65535);
    port->setValue(45454);
    form->addRow("Network port", port);
    layout->addLayout(form);
    layout->addWidget(mutedLabel("Windows Firewall may ask for permission. Allow Private networks only."));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("Start hosting");
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    if (dialog.exec() != QDialog::Accepted) return;

    if (lan_->mode() != LanSyncManager::Mode::Local) {
        if (QMessageBox::question(this, "Switch workspace",
                "Stop the current connection and host your local workspace instead?") != QMessageBox::Yes) return;
        lan_ = std::make_unique<LanSyncManager>(localRoot_);
        activeRoot_ = localRoot_;
        service_ = std::make_unique<TeamSyncService>(activeRoot_);
    }
    QString error;
    if (!lan_->startHost(static_cast<quint16>(port->value()), error, networkOptions_.pairingCode)) {
        QMessageBox::critical(this, "Could not host workspace", error);
        showConnectionToast("Hosting could not start", false);
        return;
    }
    configureLanCallbacks();
    networkOnline_ = true;
    lastNetworkError_.clear();
    loginUsername_->clear();
    loginPassword_->clear();
    updateConnectionUi();
    showConnectionToast("Workspace is now available on your local network");
}

void TeamSyncGui::showJoinWorkspaceDialog() {
    if (currentUser_) {
        QMessageBox::information(this, "Connection mode", "Sign out before changing the workspace connection.");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle("Join a TeamSync workspace");
    dialog.setMinimumWidth(500);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(mutedLabel("Enter the details shown on the host computer. Both computers must be on the same Wi-Fi or hotspot."));
    auto* form = new QFormLayout;
    auto* address = new QLineEdit;
    address->setObjectName("joinHostAddress");
    address->setPlaceholderText("Example: 192.168.1.25");
    auto* port = new QSpinBox;
    port->setObjectName("joinPortSpin");
    port->setRange(1024, 65535);
    port->setValue(45454);
    auto* code = new QLineEdit;
    code->setObjectName("joinPairingCode");
    code->setPlaceholderText("Six-digit pairing code");
    code->setMaxLength(6);
    form->addRow("Host IPv4 address", address);
    form->addRow("Network port", port);
    form->addRow("Pairing code", code);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("Connect");
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (address->text().trimmed().isEmpty() || code->text().size() != 6) {
            QMessageBox::warning(&dialog, "Connection details", "Enter the host IPv4 address and six-digit pairing code.");
            return;
        }
        const auto cacheRoot = joinedCacheRoot(address->text(), static_cast<quint16>(port->value()));
        auto candidate = std::make_unique<LanSyncManager>(cacheRoot);
        QString error;
        QApplication::setOverrideCursor(Qt::WaitCursor);
        const bool connected = candidate->joinHost(address->text(), static_cast<quint16>(port->value()),
                                                   code->text(), error);
        QApplication::restoreOverrideCursor();
        if (!connected) {
            QMessageBox::critical(&dialog, "Could not connect", error);
            return;
        }
        activeRoot_ = cacheRoot;
        service_ = std::make_unique<TeamSyncService>(activeRoot_);
        lan_ = std::move(candidate);
        configureLanCallbacks();
        networkOnline_ = true;
        lastNetworkError_.clear();
        loginUsername_->clear();
        loginPassword_->clear();
        updateConnectionUi();
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) {
        showConnectionToast("Connected—workspace synchronized successfully");
    }
}

void TeamSyncGui::switchToLocalWorkspace() {
    if (currentUser_) {
        QMessageBox::information(this, "Connection mode", "Sign out before changing the workspace connection.");
        return;
    }
    if (lan_->mode() == LanSyncManager::Mode::Local && activeRoot_ == localRoot_) {
        showConnectionToast("Already using the local workspace");
        return;
    }
    if (QMessageBox::question(this, "Use local workspace",
            "Disconnect from the shared workspace and return to this computer's local data?") != QMessageBox::Yes) return;
    lan_ = std::make_unique<LanSyncManager>(localRoot_);
    activeRoot_ = localRoot_;
    service_ = std::make_unique<TeamSyncService>(activeRoot_);
    configureLanCallbacks();
    networkOnline_ = true;
    lastNetworkError_.clear();
    loginUsername_->clear();
    loginPassword_->clear();
    updateConnectionUi();
    showConnectionToast("Using this computer's local workspace");
}

void TeamSyncGui::refreshConnection() {
    updateConnectionUi();
}

void TeamSyncGui::updateConnectionUi() {
    const auto mode = lan_->mode();
    if (networkStatus_) {
        if (mode == LanSyncManager::Mode::Host) networkStatus_->setText(lan_->statusText());
        else if (mode == LanSyncManager::Mode::Client && networkOnline_) networkStatus_->setText("Connected to " + lan_->hostAddress());
        else if (mode == LanSyncManager::Mode::Client) networkStatus_->setText("Connection interrupted • " + lan_->hostAddress());
        else networkStatus_->setText("Local workspace • This computer only");
    }
    if (!connectionModeLabel_) return;

    if (mode == LanSyncManager::Mode::Host) {
        connectionModeLabel_->setText("Hosting workspace");
        connectionDetailLabel_->setText(lan_->statusText());
        connectionPeersLabel_->setText(QString::number(lan_->activePeerCount()) + " connected computer(s)");
    } else if (mode == LanSyncManager::Mode::Client && networkOnline_) {
        connectionModeLabel_->setText("Connected workspace");
        connectionDetailLabel_->setText(lan_->statusText());
        connectionPeersLabel_->setText("Connected to 1 host computer");
    } else if (mode == LanSyncManager::Mode::Client) {
        connectionModeLabel_->setText("Connection interrupted");
        connectionDetailLabel_->setText(lastNetworkError_.isEmpty() ? "The host is currently unreachable." : lastNetworkError_);
        connectionPeersLabel_->setText("Host computer is offline or unreachable");
    } else {
        connectionModeLabel_->setText("Local workspace");
        connectionDetailLabel_->setText("This workspace is available only on this computer.");
        connectionPeersLabel_->setText("0 connected computers");
    }
    connectionRevisionLabel_->setText("Sync state: " + lan_->lastSyncText() +
                                      "  •  Revision " + QString::number(lan_->revision()));
    connectionPeersList_->clear();
    if (mode == LanSyncManager::Mode::Host) {
        const auto peers = lan_->activePeerAddresses();
        for (const auto& peer : peers) connectionPeersList_->addItem("Connected PC  •  " + peer);
        if (peers.empty()) connectionPeersList_->addItem("Waiting for other computers to join...");
    } else if (mode == LanSyncManager::Mode::Client && networkOnline_) {
        connectionPeersList_->addItem("Host computer  •  " + lan_->hostAddress() + ':' + QString::number(lan_->port()));
        connectionPeersList_->addItem("This computer  •  synchronized client");
    } else if (mode == LanSyncManager::Mode::Client) {
        connectionPeersList_->addItem("Host computer is currently unreachable");
        connectionPeersList_->addItem("This computer  •  showing the last synchronized data");
    } else {
        connectionPeersList_->addItem("This computer only");
    }
}

void TeamSyncGui::showConnectionToast(const QString& message, const bool success) {
    auto* toast = new QDialog(this, Qt::Tool | Qt::FramelessWindowHint);
    toast->setAttribute(Qt::WA_DeleteOnClose);
    toast->setAttribute(Qt::WA_ShowWithoutActivating);
    auto* layout = new QHBoxLayout(toast);
    layout->setContentsMargins(18, 14, 18, 14);
    auto* label = new QLabel((success ? "✓  " : "!  ") + message);
    label->setStyleSheet("font-weight:700; color:white;");
    layout->addWidget(label);
    toast->setStyleSheet(success
        ? "QDialog{background:#176b57; border:1px solid #48d5ae; border-radius:12px;}"
        : "QDialog{background:#7a2738; border:1px solid #ff8194; border-radius:12px;}");
    toast->adjustSize();
    const QPoint topRight = mapToGlobal(rect().topRight());
    toast->move(topRight.x() - toast->width() - 28, topRight.y() + 28);
    toast->show();
    QTimer::singleShot(2600, toast, &QDialog::close);
}

int TeamSyncGui::currentUserId() const {
    if (!currentUser_) throw AuthenticationError("No user is signed in.");
    return currentUser_->id();
}

int TeamSyncGui::selectedId(QTableWidget* source) const {
    if (!source || source->currentRow() < 0 || !source->item(source->currentRow(), 0)) {
        QMessageBox::information(const_cast<TeamSyncGui*>(this), "Select an item",
                                 "Select a row first, then try again.");
        return 0;
    }
    return source->item(source->currentRow(), 0)->data(Qt::UserRole).toInt();
}

void TeamSyncGui::showStatus(const QString& message, const bool success) {
    statusBar()->setStyleSheet(success ? "QStatusBar{color:#7ce7c4;}" : "QStatusBar{color:#ff9aa6;}");
    statusBar()->showMessage(message, 5000);
}

void TeamSyncGui::applyTheme(const bool dark) {
    darkTheme_ = dark;
    struct Colors {
        QString window, surface, surfaceHigh, surfaceHighest, outline, text, muted;
        QString primary, onPrimary, primaryContainer, onPrimaryContainer;
        QString secondaryContainer, onSecondaryContainer, errorContainer, onErrorContainer;
        QString brandA, brandB, shadow;
    } colors;
    if (dark) {
        colors = {"#111318", "#1B1B1F", "#242329", "#2B2930", "#49454F",
                  "#E6E1E5", "#CAC4D0", "#D0BCFF", "#381E72", "#4F378B",
                  "#EADDFF", "#4A4458", "#E8DEF8", "#633B48", "#FFD8E4",
                  "#4F378B", "#006874", "rgba(0,0,0,150)"};
    } else {
        colors = {"#FFFBFE", "#FFFFFF", "#F7F2FA", "#EDE7F0", "#CAC4D0",
                  "#1D1B20", "#625B71", "#6750A4", "#FFFFFF", "#EADDFF",
                  "#21005D", "#E8DEF8", "#1D192B", "#FFD8E4", "#410E0B",
                  "#6750A4", "#006874", "rgba(45,35,55,55)"};
    }

    QString style = R"QSS(
        * { font-family:"Segoe UI Variable Text","Segoe UI"; font-size:10pt; }
        QMainWindow, QWidget { background:@WINDOW@; color:@TEXT@; }
        QLabel { background:transparent; }
        #authPage { background:@WINDOW@; }
        #brandPanel {
            background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 @BRANDA@,stop:0.55 @BRANDB@,stop:1 @PRIMARY@);
            border:none; border-radius:32px;
        }
        #brandTitle { color:white; font-size:36pt; font-weight:800; letter-spacing:-1px; }
        #brandTagline { color:white; font-size:19pt; font-weight:700; }
        #brandPanel #mutedLabel { color:white; }
        #privacyLabel { color:white; font-size:9pt; font-weight:700; letter-spacing:1px; }
        #featureChip { background:rgba(255,255,255,0.14); border:1px solid rgba(255,255,255,0.22); border-radius:18px; color:white; padding:12px 16px; }
        #authCard, #card { background:@SURFACE@; border:1px solid @OUTLINE@; border-radius:28px; }
        #authHeading { color:@TEXT@; font-size:26pt; font-weight:750; letter-spacing:-0.5px; }
        #shellPage, #pageArea { background:@WINDOW@; }
        #sidebar { background:@SURFACE@; border:none; border-right:1px solid @OUTLINE@; }
        #sidebarLogo { color:@TEXT@; font-size:20pt; font-weight:800; }
        #sidebarUser { background:@SURFACEHIGH@; border:none; border-radius:20px; padding:14px; }
        #pageHeader { background:@WINDOW@; border:none; }
        #pageTitle, #dialogHeroTitle { color:@TEXT@; font-size:23pt; font-weight:750; letter-spacing:-0.5px; }
        #pageSubtitle, #mutedLabel { color:@MUTED@; }
        #sectionTitle { color:@TEXT@; font-size:15pt; font-weight:720; }
        #metricValue { color:@TEXT@; font-size:27pt; font-weight:780; }
        #metricLabel { color:@MUTED@; font-weight:650; }
        #metricAccent { color:@PRIMARY@; font-size:9pt; font-weight:700; }
        #emptyState { color:@MUTED@; padding:28px; }
        #connectionPanel { background:@SURFACEHIGH@; border:none; border-radius:22px; }
        #connectionPanelTitle { color:@TEXT@; font-weight:700; }
        #tutorialStep, #materialInset { background:@SURFACEHIGH@; border:none; border-radius:18px; padding:13px 15px; }
        QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QDateEdit, QSpinBox {
            background:@SURFACEHIGH@; color:@TEXT@; border:1px solid @OUTLINE@;
            border-radius:16px; padding:10px 13px; min-height:24px; selection-background-color:@PRIMARY@;
        }
        QLineEdit:hover, QTextEdit:hover, QPlainTextEdit:hover, QComboBox:hover, QDateEdit:hover, QSpinBox:hover { border-color:@MUTED@; }
        QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QDateEdit:focus, QSpinBox:focus { border:2px solid @PRIMARY@; padding:9px 12px; }
        QLineEdit[readOnly="true"] { color:@MUTED@; background:@SURFACE@; }
        QComboBox::drop-down { border:none; width:32px; }
        QComboBox QAbstractItemView { background:@SURFACEHIGH@; color:@TEXT@; border:1px solid @OUTLINE@; selection-background-color:@PRIMARYCONTAINER@; selection-color:@ONPRIMARYCONTAINER@; }
        QPushButton {
            background:@SURFACEHIGH@; color:@TEXT@; border:none; border-radius:20px;
            padding:9px 17px; min-height:22px; font-weight:650;
        }
        QPushButton:hover { background:@SURFACEHIGHEST@; }
        QPushButton:pressed { background:@SECONDARYCONTAINER@; padding-top:11px; padding-bottom:7px; }
        QPushButton[primary="true"] { background:@PRIMARY@; color:@ONPRIMARY@; border:none; }
        QPushButton[primary="true"]:hover { background:@PRIMARYCONTAINER@; color:@ONPRIMARYCONTAINER@; }
        QPushButton#tonalButton { background:@SECONDARYCONTAINER@; color:@ONSECONDARYCONTAINER@; }
        QPushButton#linkButton { background:transparent; border:none; color:@PRIMARY@; padding:5px 8px; }
        QPushButton#linkButton:hover { background:@PRIMARYCONTAINER@; color:@ONPRIMARYCONTAINER@; }
        QPushButton#themeButton { background:@SURFACEHIGH@; color:@TEXT@; border-radius:20px; min-width:84px; }
        QPushButton#notificationButton { background:@PRIMARY@; color:@ONPRIMARY@; border-radius:20px; min-width:48px; padding:8px 12px; }
        QPushButton#notificationButton[hasUnread="true"] { background:@PRIMARY@; color:@ONPRIMARY@; }
        QPushButton#navButton { background:transparent; color:@MUTED@; border:none; border-radius:24px; text-align:left; padding:12px 16px; font-weight:650; }
        QPushButton#navButton:hover { background:@SURFACEHIGH@; color:@TEXT@; }
        QPushButton#navButton:checked { background:@SECONDARYCONTAINER@; color:@ONSECONDARYCONTAINER@; border:none; }
        QPushButton#dangerButton, QPushButton#logoutButton { color:@ONERRORCONTAINER@; background:@ERRORCONTAINER@; }
        QPushButton#floatingChatButton { background:@PRIMARY@; border:none; border-radius:29px; padding:0; }
        QPushButton#floatingChatButton:hover { background:@PRIMARYCONTAINER@; }
        QTableWidget { background:@SURFACE@; alternate-background-color:@SURFACEHIGH@; border:1px solid @OUTLINE@; border-radius:24px; color:@TEXT@; selection-background-color:@SECONDARYCONTAINER@; selection-color:@ONSECONDARYCONTAINER@; outline:none; }
        QHeaderView::section { background:@SURFACEHIGH@; color:@MUTED@; border:none; border-bottom:1px solid @OUTLINE@; padding:12px; font-weight:700; }
        QTableWidget::item { padding:10px; border-bottom:1px solid @OUTLINE@; }
        QTableWidget::item:hover { background:@SURFACEHIGHEST@; }
        QListWidget { background:@SURFACE@; border:1px solid @OUTLINE@; border-radius:24px; color:@TEXT@; outline:none; padding:8px; }
        QListWidget::item { padding:12px; border-radius:16px; margin:3px; }
        QListWidget::item:hover { background:@SURFACEHIGH@; }
        QListWidget::item:selected { background:@SECONDARYCONTAINER@; color:@ONSECONDARYCONTAINER@; }
        QProgressBar { background:@SURFACEHIGH@; border:none; border-radius:8px; text-align:center; color:@TEXT@; min-height:18px; }
        QProgressBar::chunk { border-radius:8px; background:@PRIMARY@; }
        QGroupBox { border:1px solid @OUTLINE@; border-radius:22px; margin-top:16px; padding:16px; font-weight:700; }
        QGroupBox::title { subcontrol-origin:margin; left:16px; padding:0 7px; color:@TEXT@; }
        QDialog { background:@SURFACE@; }
        QDialogButtonBox QPushButton { min-width:96px; }
        QMessageBox { background:@SURFACE@; }
        QScrollArea, QScrollArea > QWidget > QWidget { background:transparent; border:none; }
        QScrollBar:vertical { background:transparent; width:11px; margin:3px; }
        QScrollBar::handle:vertical { background:@OUTLINE@; border-radius:5px; min-height:36px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
        QStatusBar { background:@SURFACE@; color:@MUTED@; border:none; }
        QToolTip { background:@SURFACEHIGHEST@; color:@TEXT@; border:1px solid @OUTLINE@; border-radius:10px; padding:7px; }
    )QSS";
    const std::vector<std::pair<QString, QString>> replacements = {
        {"@WINDOW@", colors.window}, {"@SURFACE@", colors.surface},
        {"@SURFACEHIGH@", colors.surfaceHigh}, {"@SURFACEHIGHEST@", colors.surfaceHighest},
        {"@OUTLINE@", colors.outline}, {"@TEXT@", colors.text}, {"@MUTED@", colors.muted},
        {"@PRIMARY@", colors.primary}, {"@ONPRIMARY@", colors.onPrimary},
        {"@PRIMARYCONTAINER@", colors.primaryContainer}, {"@ONPRIMARYCONTAINER@", colors.onPrimaryContainer},
        {"@SECONDARYCONTAINER@", colors.secondaryContainer}, {"@ONSECONDARYCONTAINER@", colors.onSecondaryContainer},
        {"@ERRORCONTAINER@", colors.errorContainer}, {"@ONERRORCONTAINER@", colors.onErrorContainer},
        {"@BRANDA@", colors.brandA}, {"@BRANDB@", colors.brandB}, {"@SHADOW@", colors.shadow}
    };
    for (const auto& [token, value] : replacements) style.replace(token, value);
    setStyleSheet(style);

    const QString switchLabel = dark ? "☀  Light" : "☾  Dark";
    if (authThemeButton_) authThemeButton_->setText(switchLabel);
    if (shellThemeButton_) shellThemeButton_->setText(switchLabel);
    const std::array<ushort, 10> navigationGlyphs = {
        0xE80F, 0xE716, 0xE8B7, 0xE73E, 0xE8A5,
        0xE9D2, 0xE81C, 0xE968, 0xE897, 0xE77B
    };
    for (std::size_t index = 0; index < navigationButtons_.size() && index < navigationGlyphs.size(); ++index) {
        navigationButtons_[index]->setIcon(materialGlyphIcon(navigationGlyphs[index], QColor(colors.muted)));
        navigationButtons_[index]->setIconSize(QSize(24, 24));
    }
    QSettings("TeamSync", "TeamSync").setValue("appearance/darkTheme", darkTheme_);
}

void TeamSyncGui::toggleTheme() {
    applyTheme(!darkTheme_);
    showStatus(darkTheme_ ? "Dark theme enabled" : "Light theme enabled");
}

void TeamSyncGui::animateWindowIn() {
    setWindowOpacity(0.0);
    auto* animation = new QPropertyAnimation(this, "windowOpacity", this);
    animation->setDuration(420);
    animation->setStartValue(0.0);
    animation->setEndValue(1.0);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void TeamSyncGui::animatePage(QWidget* page) {
    if (!page) return;
    const QPoint finalPosition = page->pos();
    page->move(finalPosition + QPoint(18, 0));
    auto* animation = new QPropertyAnimation(page, "pos", page);
    animation->setDuration(190);
    animation->setStartValue(page->pos());
    animation->setEndValue(finalPosition);
    animation->setEasingCurve(QEasingCurve::OutCubic);
    connect(animation, &QPropertyAnimation::finished, page, [page, finalPosition] {
        page->move(finalPosition);
    });
    animation->start(QAbstractAnimation::DeleteWhenStopped);
}

void TeamSyncGui::animatePopup(QDialog* dialog) {
    if (!dialog) return;
    const QPoint finalPosition = dialog->pos();
    dialog->move(finalPosition + QPoint(0, 18));
    dialog->setWindowOpacity(0.0);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    auto* group = new QParallelAnimationGroup(dialog);
    auto* position = new QPropertyAnimation(dialog, "pos", group);
    position->setDuration(220);
    position->setStartValue(dialog->pos());
    position->setEndValue(finalPosition);
    position->setEasingCurve(QEasingCurve::OutCubic);
    auto* opacity = new QPropertyAnimation(dialog, "windowOpacity", group);
    opacity->setDuration(180);
    opacity->setStartValue(0.0);
    opacity->setEndValue(1.0);
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void TeamSyncGui::resizeEvent(QResizeEvent* event) {
    QMainWindow::resizeEvent(event);
    if (floatingChatButton_ && floatingChatButton_->parentWidget()) {
        floatingChatButton_->move(floatingChatButton_->parentWidget()->width() - 88,
                                  floatingChatButton_->parentWidget()->height() - 88);
        floatingChatButton_->raise();
    }
}

bool TeamSyncGui::eventFilter(QObject* watched, QEvent* event) {
    if (floatingChatButton_ && watched == floatingChatButton_->parentWidget() &&
        event->type() == QEvent::Resize) {
        auto* parent = floatingChatButton_->parentWidget();
        floatingChatButton_->move(parent->width() - 88, parent->height() - 88);
        floatingChatButton_->raise();
    }
    return QMainWindow::eventFilter(watched, event);
}

void TeamSyncGui::navigateTo(const Page page) {
    static const QStringList titles = {
        "Overview", "Teams", "Projects", "Tasks", "Shared files",
        "Reports", "Activity", "Connection", "Tutorial & Credits", "My profile"
    };
    static const QStringList subtitles = {
        "Your workspace at a glance", "People, roles, and team membership",
        "Track outcomes from start to finish", "Priorities, ownership, and clear status",
        "Local files synchronized with your team", "Readable summaries for decisions and reviews",
        "A transparent history of workspace actions", "Live LAN sync and connected computers",
        "Learn the workflow and meet the project team",
        "Your identity, skills, and security"
    };
    contentStack_->setCurrentIndex(page);
    pageTitle_->setText(titles[page]);
    pageSubtitle_->setText(subtitles[page]);
    if (page < static_cast<int>(navigationButtons_.size())) navigationButtons_[page]->setChecked(true);
    refreshCurrentPage();
    animatePage(contentStack_->currentWidget());
}

QWidget* TeamSyncGui::buildDashboardPage() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(30, 26, 30, 30);
    layout->setSpacing(20);

    auto* welcome = new QLabel("Today in your workspace");
    welcome->setObjectName("sectionTitle");
    layout->addWidget(welcome);

    auto* metricGrid = new QGridLayout;
    metricGrid->setSpacing(14);
    const QStringList names = {"My teams", "Projects", "Pending tasks", "In progress", "Completed", "Unread notices"};
    const QStringList accents = {"COLLABORATION", "ACTIVE WORK", "NEEDS ACTION", "MOVING FORWARD", "DONE", "UPDATES"};
    for (int index = 0; index < names.size(); ++index) {
        auto* metric = card();
        auto* metricLayout = new QVBoxLayout(metric);
        metricLayout->setContentsMargins(18, 17, 18, 17);
        auto* accent = new QLabel(accents[index]);
        accent->setObjectName("metricAccent");
        auto* value = new QLabel("0");
        value->setObjectName("metricValue");
        auto* name = new QLabel(names[index]);
        name->setObjectName("metricLabel");
        metricLayout->addWidget(accent);
        metricLayout->addWidget(value);
        metricLayout->addWidget(name);
        dashboardValues_.push_back(value);
        metricGrid->addWidget(metric, index / 3, index % 3);
    }
    layout->addLayout(metricGrid);

    auto* statusCard = card();
    auto* statusLayout = new QVBoxLayout(statusCard);
    statusLayout->setContentsMargins(20, 18, 20, 18);
    auto* statusTitle = new QLabel("Your task status");
    statusTitle->setObjectName("sectionTitle");
    dashboardStatusSummary_ = new QLabel("No assigned tasks yet");
    dashboardStatusSummary_->setObjectName("metricLabel");
    statusLayout->addWidget(statusTitle);
    statusLayout->addWidget(mutedLabel("Task states update from Pending to In Progress to Completed—no percentage entry required."));
    statusLayout->addWidget(dashboardStatusSummary_);
    layout->addWidget(statusCard);

    auto* activityGrid = new QGridLayout;
    activityGrid->setSpacing(16);
    auto* messagesCard = card();
    auto* messagesLayout = new QVBoxLayout(messagesCard);
    auto* messageTitle = new QLabel("Recent conversations");
    messageTitle->setObjectName("sectionTitle");
    recentMessages_ = new QListWidget;
    recentMessages_->setMinimumHeight(190);
    messagesLayout->addWidget(messageTitle);
    messagesLayout->addWidget(recentMessages_);
    auto* activityCard = card();
    auto* activityLayout = new QVBoxLayout(activityCard);
    auto* activityTitle = new QLabel("Recent activity");
    activityTitle->setObjectName("sectionTitle");
    recentActivities_ = new QListWidget;
    recentActivities_->setMinimumHeight(190);
    activityLayout->addWidget(activityTitle);
    activityLayout->addWidget(recentActivities_);
    activityGrid->addWidget(messagesCard, 0, 0);
    activityGrid->addWidget(activityCard, 0, 1);
    layout->addLayout(activityGrid);

    auto* quickCard = card();
    auto* quickLayout = new QHBoxLayout(quickCard);
    quickLayout->setContentsMargins(18, 16, 18, 16);
    auto* quickText = new QVBoxLayout;
    auto* quickTitle = new QLabel("Quick actions");
    quickTitle->setObjectName("sectionTitle");
    quickText->addWidget(quickTitle);
    quickText->addWidget(mutedLabel("Jump directly to the work you want to manage."));
    quickLayout->addLayout(quickText);
    quickLayout->addStretch();
    auto* teams = button("Manage teams");
    auto* tasks = button("Open tasks", true);
    auto* chat = button("Team chat");
    quickLayout->addWidget(teams);
    quickLayout->addWidget(tasks);
    quickLayout->addWidget(chat);
    connect(teams, &QPushButton::clicked, this, [this] { navigateTo(TeamsPage); });
    connect(tasks, &QPushButton::clicked, this, [this] { navigateTo(TasksPage); });
    connect(chat, &QPushButton::clicked, this, [this] { showChatPanel(); });
    layout->addWidget(quickCard);
    layout->addStretch();
    scroll->setWidget(content);
    return scroll;
}

QWidget* TeamSyncGui::buildTeamsPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 26, 30, 30);
    layout->setSpacing(14);
    auto* toolbar = new QHBoxLayout;
    teamSearch_ = new QLineEdit;
    teamSearch_->setPlaceholderText("Search your teams...");
    teamSearch_->setClearButtonEnabled(true);
    auto* create = button("Create team", true);
    auto* join = button("Join with code");
    auto* details = button("View members");
    auto* leave = button("Leave team");
    leave->setObjectName("dangerButton");
    toolbar->addWidget(teamSearch_, 1);
    toolbar->addWidget(create);
    toolbar->addWidget(join);
    toolbar->addWidget(details);
    toolbar->addWidget(leave);
    layout->addLayout(toolbar);
    teamsTable_ = table({"ID", "Team", "My role", "Members", "Join code", "Status"});
    teamsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(teamsTable_, 1);
    layout->addWidget(mutedLabel("Team admins can promote or remove members from the member-details window."));
    connect(teamSearch_, &QLineEdit::textChanged, this, [this] { refreshTeams(); });
    connect(create, &QPushButton::clicked, this, [this] { showCreateTeamDialog(); });
    connect(join, &QPushButton::clicked, this, [this] { joinTeam(); });
    connect(details, &QPushButton::clicked, this, [this] { showTeamDetails(); });
    connect(leave, &QPushButton::clicked, this, [this] { leaveSelectedTeam(); });
    connect(teamsTable_, &QTableWidget::itemDoubleClicked, this, [this] { showTeamDetails(); });
    return page;
}

QWidget* TeamSyncGui::buildProjectsPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 26, 30, 30);
    layout->setSpacing(14);
    auto* toolbar = new QHBoxLayout;
    projectSearch_ = new QLineEdit;
    projectSearch_->setPlaceholderText("Search projects...");
    projectSearch_->setClearButtonEnabled(true);
    auto* create = button("New project", true);
    auto* edit = button("Edit");
    auto* archive = button("Archive");
    toolbar->addWidget(projectSearch_, 1);
    toolbar->addWidget(create);
    toolbar->addWidget(edit);
    toolbar->addWidget(archive);
    layout->addLayout(toolbar);
    projectsTable_ = table({"ID", "Project", "Team", "Status", "Start", "Due"});
    projectsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(projectsTable_, 1);
    connect(projectSearch_, &QLineEdit::textChanged, this, [this] { refreshProjects(); });
    connect(create, &QPushButton::clicked, this, [this] { showProjectDialog(false); });
    connect(edit, &QPushButton::clicked, this, [this] { showProjectDialog(true); });
    connect(archive, &QPushButton::clicked, this, [this] { archiveSelectedProject(); });
    connect(projectsTable_, &QTableWidget::itemDoubleClicked, this, [this] { showProjectDialog(true); });
    return page;
}

QWidget* TeamSyncGui::buildTasksPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 26, 30, 30);
    layout->setSpacing(12);
    auto* filters = new QHBoxLayout;
    taskSearch_ = new QLineEdit;
    taskSearch_->setPlaceholderText("Search tasks by title or description...");
    taskSearch_->setClearButtonEnabled(true);
    taskFilter_ = new QComboBox;
    taskFilter_->addItems({"All statuses", "Pending", "In progress", "Completed", "Overdue"});
    auto* create = button("New task", true);
    auto* edit = button("Edit");
    filters->addWidget(taskSearch_, 1);
    filters->addWidget(taskFilter_);
    filters->addWidget(create);
    filters->addWidget(edit);
    layout->addLayout(filters);
    tasksTable_ = table({"ID", "Task", "Team", "Assignee", "Priority", "Status", "Due", "Skill"});
    tasksTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(tasksTable_, 1);
    auto* actions = new QHBoxLayout;
    auto* progress = button("Start task");
    auto* complete = button("Mark complete");
    auto* reopen = button("Reopen");
    auto* evidence = button("Completion details");
    auto* comments = button("Comments");
    auto* recommend = button("Recommend member");
    auto* remove = button("Delete");
    remove->setObjectName("dangerButton");
    actions->addWidget(progress);
    actions->addWidget(complete);
    actions->addWidget(reopen);
    actions->addWidget(evidence);
    actions->addWidget(comments);
    actions->addWidget(recommend);
    actions->addStretch();
    actions->addWidget(remove);
    layout->addLayout(actions);
    connect(taskSearch_, &QLineEdit::textChanged, this, [this] { refreshTasks(); });
    connect(taskFilter_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refreshTasks(); });
    connect(create, &QPushButton::clicked, this, [this] { showTaskDialog(false); });
    connect(edit, &QPushButton::clicked, this, [this] { showTaskDialog(true); });
    connect(progress, &QPushButton::clicked, this, [this] { startSelectedTask(); });
    connect(complete, &QPushButton::clicked, this, [this] { completeSelectedTask(); });
    connect(reopen, &QPushButton::clicked, this, [this] { reopenSelectedTask(); });
    connect(evidence, &QPushButton::clicked, this, [this] { showCompletionDetails(); });
    connect(comments, &QPushButton::clicked, this, [this] { showTaskComments(); });
    connect(recommend, &QPushButton::clicked, this, [this] { showRecommendations(); });
    connect(remove, &QPushButton::clicked, this, [this] { deleteSelectedTask(); });
    connect(tasksTable_, &QTableWidget::itemDoubleClicked, this, [this] { showTaskDialog(true); });
    return page;
}

QWidget* TeamSyncGui::buildChatPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 26, 30, 30);
    layout->setSpacing(14);
    auto* top = new QVBoxLayout;
    auto* teamRow = new QHBoxLayout;
    teamRow->addWidget(new QLabel("Team"));
    chatTeam_ = new QComboBox;
    chatTeam_->setMinimumWidth(220);
    auto* refresh = button("Refresh conversation");
    auto* remove = button("Delete my message");
    remove->setObjectName("dangerButton");
    teamRow->addWidget(chatTeam_, 1);
    top->addLayout(teamRow);
    auto* chatActions = new QHBoxLayout;
    chatActions->addStretch();
    chatActions->addWidget(refresh);
    chatActions->addWidget(remove);
    top->addLayout(chatActions);
    layout->addLayout(top);
    chatMessages_ = new QListWidget;
    chatMessages_->setSpacing(4);
    layout->addWidget(chatMessages_, 1);
    auto* composer = card();
    auto* composerLayout = new QHBoxLayout(composer);
    chatInput_ = new QLineEdit;
    chatInput_->setPlaceholderText("Write a message to your team...");
    auto* send = button("Send message", true);
    composerLayout->addWidget(chatInput_, 1);
    composerLayout->addWidget(send);
    layout->addWidget(composer);
    connect(chatTeam_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refreshChat(); });
    connect(refresh, &QPushButton::clicked, this, [this] { refreshChat(); });
    connect(send, &QPushButton::clicked, this, [this] { sendChatMessage(); });
    connect(chatInput_, &QLineEdit::returnPressed, this, [this] { sendChatMessage(); });
    connect(remove, &QPushButton::clicked, this, [this] { deleteSelectedMessage(); });
    return page;
}

QWidget* TeamSyncGui::buildFilesPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 26, 30, 30);
    layout->setSpacing(14);
    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel("Team"));
    filesTeam_ = new QComboBox;
    filesTeam_->setMinimumWidth(250);
    auto* share = button("Share local file", true);
    auto* open = button("Open file");
    auto* remove = button("Remove record");
    remove->setObjectName("dangerButton");
    toolbar->addWidget(filesTeam_);
    toolbar->addStretch();
    toolbar->addWidget(share);
    toolbar->addWidget(open);
    toolbar->addWidget(remove);
    layout->addLayout(toolbar);
    filesTable_ = table({"ID", "File", "Description", "Uploader", "Shared", "Size", "Available"});
    filesTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(filesTable_, 1);
    layout->addWidget(mutedLabel("TeamSync copies the file into the workspace so every connected computer receives it."));
    connect(filesTeam_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refreshFiles(); });
    connect(share, &QPushButton::clicked, this, [this] { shareFile(); });
    connect(open, &QPushButton::clicked, this, [this] { openSelectedFile(); });
    connect(remove, &QPushButton::clicked, this, [this] { removeSelectedFile(); });
    connect(filesTable_, &QTableWidget::itemDoubleClicked, this, [this] { openSelectedFile(); });
    return page;
}

QWidget* TeamSyncGui::buildNotificationsPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 26, 30, 30);
    layout->setSpacing(14);
    auto* top = new QHBoxLayout;
    top->addWidget(mutedLabel("Updates created by team, project, and task activity."));
    top->addStretch();
    auto* markRead = button("Mark all as read", true);
    top->addWidget(markRead);
    layout->addLayout(top);
    notificationsTable_ = table({"Status", "Notification", "Date / time"});
    notificationsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(notificationsTable_, 1);
    connect(markRead, &QPushButton::clicked, this, [this] {
        if (perform([this] { service_->markAllNotificationsRead(currentUserId()); }, "Notifications marked as read")) refreshNotifications();
    });
    return page;
}

void TeamSyncGui::showChatPanel() {
    if (!currentUser_) return;
    if (!chatDialog_) {
        chatDialog_ = new QDialog(this, Qt::Dialog);
        chatDialog_->setWindowTitle("Team chat");
        chatDialog_->setObjectName("chatPanel");
        chatDialog_->resize(540, 650);
        chatDialog_->setMinimumSize(440, 500);
        auto* layout = new QVBoxLayout(chatDialog_);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(buildChatPage());
    }
    refreshTeamSelectors();
    refreshChat();
    const QPoint bottomRight = mapToGlobal(rect().bottomRight());
    chatDialog_->move(bottomRight.x() - chatDialog_->width() - 28,
                      bottomRight.y() - chatDialog_->height() - 38);
    animatePopup(chatDialog_);
}

void TeamSyncGui::showNotificationsPanel() {
    if (!currentUser_) return;
    if (!notificationsDialog_) {
        notificationsDialog_ = new QDialog(this, Qt::Dialog);
        notificationsDialog_->setWindowTitle("Notifications");
        notificationsDialog_->setObjectName("notificationsPanel");
        notificationsDialog_->resize(720, 520);
        notificationsDialog_->setMinimumSize(540, 400);
        auto* layout = new QVBoxLayout(notificationsDialog_);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(buildNotificationsPage());
    }
    refreshNotifications();
    const QPoint topRight = mapToGlobal(rect().topRight());
    notificationsDialog_->move(topRight.x() - notificationsDialog_->width() - 28,
                               topRight.y() + 88);
    animatePopup(notificationsDialog_);
}

QWidget* TeamSyncGui::buildReportsPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 26, 30, 30);
    layout->setSpacing(14);
    auto* toolbar = new QHBoxLayout;
    reportTeam_ = new QComboBox;
    reportType_ = new QComboBox;
    reportType_->addItems({"Task completion", "Pending tasks", "Overdue tasks", "Team activity",
                           "Member contribution", "Project status", "Shared files", "Team member list"});
    auto* generate = button("Generate report", true);
    auto* exportButton = button("Export text file");
    toolbar->addWidget(new QLabel("Team"));
    toolbar->addWidget(reportTeam_);
    toolbar->addWidget(new QLabel("Report"));
    toolbar->addWidget(reportType_);
    toolbar->addStretch();
    toolbar->addWidget(generate);
    toolbar->addWidget(exportButton);
    layout->addLayout(toolbar);
    reportPreview_ = new QTextEdit;
    reportPreview_->setReadOnly(true);
    reportPreview_->setPlaceholderText("Choose a team and report type, then select Generate report.");
    reportPreview_->setFont(QFont("Cascadia Mono", 10));
    layout->addWidget(reportPreview_, 1);
    connect(generate, &QPushButton::clicked, this, [this] { generateReport(); });
    connect(exportButton, &QPushButton::clicked, this, [this] { exportReport(); });
    return page;
}

QWidget* TeamSyncGui::buildActivityPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 26, 30, 30);
    layout->setSpacing(14);
    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel("Filter by team"));
    activityTeam_ = new QComboBox;
    activityTeam_->setMinimumWidth(260);
    toolbar->addWidget(activityTeam_);
    toolbar->addStretch();
    auto* refresh = button("Refresh activity");
    toolbar->addWidget(refresh);
    layout->addLayout(toolbar);
    activityTable_ = table({"Date / time", "Team", "Person", "Action"});
    activityTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    layout->addWidget(activityTable_, 1);
    connect(activityTeam_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refreshActivities(); });
    connect(refresh, &QPushButton::clicked, this, [this] { refreshActivities(); });
    return page;
}

QWidget* TeamSyncGui::buildConnectionPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(30, 26, 30, 30);
    layout->setSpacing(16);

    auto* summary = card();
    auto* summaryLayout = new QVBoxLayout(summary);
    summaryLayout->setContentsMargins(22, 20, 22, 20);
    auto* heading = new QLabel("Workspace connection");
    heading->setObjectName("sectionTitle");
    connectionModeLabel_ = new QLabel;
    connectionModeLabel_->setObjectName("metricValue");
    connectionDetailLabel_ = mutedLabel("");
    connectionDetailLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    connectionPeersLabel_ = new QLabel;
    connectionPeersLabel_->setObjectName("metricLabel");
    connectionRevisionLabel_ = mutedLabel("");
    summaryLayout->addWidget(heading);
    summaryLayout->addWidget(connectionModeLabel_);
    summaryLayout->addWidget(connectionDetailLabel_);
    summaryLayout->addWidget(connectionPeersLabel_);
    summaryLayout->addWidget(connectionRevisionLabel_);
    layout->addWidget(summary);

    auto* peersCard = card();
    auto* peersLayout = new QVBoxLayout(peersCard);
    auto* peersTitle = new QLabel("Connected computers");
    peersTitle->setObjectName("sectionTitle");
    connectionPeersList_ = new QListWidget;
    connectionPeersList_->setMinimumHeight(190);
    peersLayout->addWidget(peersTitle);
    peersLayout->addWidget(connectionPeersList_);
    layout->addWidget(peersCard, 1);

    auto* actions = new QHBoxLayout;
    auto* refresh = button("Sync now", true);
    auto* copy = button("Copy connection details");
    actions->addWidget(refresh);
    actions->addWidget(copy);
    actions->addStretch();
    actions->addWidget(mutedLabel("To change connection mode, sign out and use the controls on the sign-in screen."));
    layout->addLayout(actions);
    connect(refresh, &QPushButton::clicked, this, [this] {
        if (pullNetworkChanges(true)) {
            updateConnectionUi();
            showConnectionToast("Workspace synchronized");
        }
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(lan_->statusText());
        showConnectionToast("Connection details copied");
    });
    updateConnectionUi();
    return page;
}

QWidget* TeamSyncGui::buildTutorialPage() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(30, 26, 30, 34);
    layout->setSpacing(18);

    auto* intro = card();
    auto* introLayout = new QVBoxLayout(intro);
    introLayout->setContentsMargins(24, 22, 24, 22);
    auto* introTitle = new QLabel("Welcome to TeamSync");
    introTitle->setObjectName("sectionTitle");
    introLayout->addWidget(introTitle);
    introLayout->addWidget(mutedLabel(
        "Use this short guide to create a workspace, collaborate over Wi-Fi, and finish work with clear evidence."));
    auto* shortcuts = new QHBoxLayout;
    auto* tasks = button("Open tasks", true);
    auto* connection = button("View connection");
    auto* chat = button("Open chat");
    shortcuts->addWidget(tasks);
    shortcuts->addWidget(connection);
    shortcuts->addWidget(chat);
    shortcuts->addStretch();
    introLayout->addLayout(shortcuts);
    connect(tasks, &QPushButton::clicked, this, [this] { navigateTo(TasksPage); });
    connect(connection, &QPushButton::clicked, this, [this] { navigateTo(ConnectionPage); });
    connect(chat, &QPushButton::clicked, this, [this] { showChatPanel(); });
    layout->addWidget(intro);

    auto* guide = card();
    auto* guideLayout = new QVBoxLayout(guide);
    guideLayout->setContentsMargins(24, 22, 24, 22);
    guideLayout->setSpacing(12);
    auto* guideTitle = new QLabel("Quick-start tutorial");
    guideTitle->setObjectName("sectionTitle");
    guideLayout->addWidget(guideTitle);
    const QStringList steps = {
        "1. Create an account and complete all three security questions, then sign in.",
        "2. Create a team and share its join code. Team members join from the Teams page.",
        "3. A team admin creates a project and its tasks, choosing an assignee, due date, priority, and skill.",
        "4. Any member of that team can start, complete, or reopen a task. Completion can include notes and files.",
        "5. Use the chat bubble in the bottom-right corner for team messages and the bell in the top-right for notices.",
        "6. For multiple computers, Host on one sign-in screen and Join on the others using the displayed IP and code.",
        "7. Shared files, task updates, messages, and completion evidence synchronize automatically on the trusted LAN."
    };
    for (const auto& step : steps) {
        auto* label = new QLabel(step);
        label->setWordWrap(true);
        label->setObjectName("tutorialStep");
        guideLayout->addWidget(label);
    }
    layout->addWidget(guide);

    auto* detailsGrid = new QGridLayout;
    detailsGrid->setSpacing(18);
    auto* security = card();
    auto* securityLayout = new QVBoxLayout(security);
    securityLayout->setContentsMargins(22, 20, 22, 20);
    auto* securityTitle = new QLabel("Account recovery");
    securityTitle->setObjectName("sectionTitle");
    securityLayout->addWidget(securityTitle);
    securityLayout->addWidget(mutedLabel(
        "New accounts answer three personal questions. Forgot password asks the same questions and never displays the saved answers."));
    auto* securityText = new QLabel(
        "<b>Good answers are:</b><br>Memorable to you<br>Hard for other people to guess<br>Entered consistently<br><br>"
        "<b>Existing account?</b><br>Open My profile and choose Configure security questions.");
    securityText->setWordWrap(true);
    securityText->setObjectName("materialInset");
    securityLayout->addWidget(securityText);
    securityLayout->addStretch();

    auto* credits = card();
    auto* creditsLayout = new QVBoxLayout(credits);
    creditsLayout->setContentsMargins(22, 20, 22, 20);
    auto* creditsTitle = new QLabel("Credits & acknowledgement");
    creditsTitle->setObjectName("sectionTitle");
    creditsLayout->addWidget(creditsTitle);
    auto* creditsText = new QLabel(
        "<b>Developers</b><br><br>"
        "<b>Sudhan Bhattarai</b><br>"
        "<a href=\"https://www.sudhanb.com.np\">Portfolio &#8599;</a>"
        "&nbsp;&nbsp;&middot;&nbsp;&nbsp;"
        "<a href=\"https://github.com/SudhanTheDev\">GitHub &#8599;</a><br><br>"
        "<b>Severoos Nepali</b><br>"
        "<a href=\"https://www.severoos-nepali.com.np/\">Portfolio &#8599;</a>"
        "&nbsp;&nbsp;&middot;&nbsp;&nbsp;"
        "<a href=\"https://github.com/Severoos-ui\">GitHub &#8599;</a><br><br>"
        "<b>Project Support</b><br>Senior Sudip Khanal<br><br>"
        "<b>Academic Guidance</b><br>Ojan Adhikari<br><br>"
        "Second Semester · Object-Oriented Programming · C++17 and Qt");
    creditsText->setWordWrap(true);
    creditsText->setTextFormat(Qt::RichText);
    creditsText->setTextInteractionFlags(Qt::TextBrowserInteraction);
    creditsText->setOpenExternalLinks(true);
    creditsText->setObjectName("materialInset");
    creditsLayout->addWidget(creditsText);
    creditsLayout->addStretch();
    detailsGrid->addWidget(security, 0, 0);
    detailsGrid->addWidget(credits, 0, 1);
    detailsGrid->setColumnStretch(0, 1);
    detailsGrid->setColumnStretch(1, 1);
    layout->addLayout(detailsGrid);
    layout->addStretch();
    scroll->setWidget(content);
    return scroll;
}

QWidget* TeamSyncGui::buildProfilePage() {
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(30, 26, 30, 30);
    auto* profileCard = card();
    profileCard->setMaximumWidth(760);
    auto* layout = new QVBoxLayout(profileCard);
    layout->setContentsMargins(28, 26, 28, 26);
    layout->setSpacing(12);
    auto* heading = new QLabel("Profile information");
    heading->setObjectName("sectionTitle");
    layout->addWidget(heading);
    layout->addWidget(mutedLabel("Your skills are used by TeamSync's member recommendation scoring."));
    auto* form = new QFormLayout;
    form->setSpacing(12);
    profileName_ = new QLineEdit;
    profileUsername_ = new QLineEdit;
    profileUsername_->setReadOnly(true);
    profileEmail_ = new QLineEdit;
    profileSkills_ = new QLineEdit;
    profileSkills_->setPlaceholderText("C++, research, documentation");
    profileRole_ = new QLabel;
    form->addRow("Full name", profileName_);
    form->addRow("Username", profileUsername_);
    form->addRow("Email", profileEmail_);
    form->addRow("Skills", profileSkills_);
    form->addRow("Account role", profileRole_);
    layout->addLayout(form);
    auto* actions = new QHBoxLayout;
    auto* save = button("Save profile", true);
    auto* password = button("Change password");
    auto* security = button("Configure security questions");
    actions->addWidget(save);
    actions->addWidget(password);
    actions->addWidget(security);
    actions->addStretch();
    layout->addLayout(actions);
    outer->addWidget(profileCard, 0, Qt::AlignHCenter);
    outer->addStretch();
    connect(save, &QPushButton::clicked, this, [this] { saveProfile(); });
    connect(password, &QPushButton::clicked, this, [this] { showChangePasswordDialog(); });
    connect(security, &QPushButton::clicked, this, [this] { showSecurityQuestionsDialog(); });
    return page;
}

void TeamSyncGui::attemptLogin() {
    if (loginUsername_->text().trimmed().isEmpty() || loginPassword_->text().isEmpty()) {
        QMessageBox::information(this, "Sign in", "Enter both your username and password.");
        return;
    }
    if (!perform([this] {
            currentUser_ = &service_->login(s(loginUsername_->text().trimmed()), s(loginPassword_->text()));
        })) return;

    signedInUser_->setText("<b>" + q(currentUser_->fullName()).toHtmlEscaped() + "</b><br>" +
                           q(currentUser_->username()).toHtmlEscaped() + "  •  " + q(toString(currentUser_->role())));
    loginPassword_->clear();
    rootStack_->setCurrentWidget(shellPage_);
    refreshTeamSelectors();
    updateNotificationButton();
    navigateTo(DashboardPage);
    animatePage(shellPage_);
    showStatus("Signed in successfully");
}

void TeamSyncGui::showRegistrationDialog() {
    QDialog dialog(this);
    dialog.setObjectName("registrationDialog");
    dialog.setWindowTitle("Create TeamSync account");
    dialog.setMinimumWidth(520);
    auto* layout = new QVBoxLayout(&dialog);
    auto* heading = new QLabel("Create your account");
    heading->setObjectName("sectionTitle");
    layout->addWidget(heading);
    layout->addWidget(mutedLabel("The account is stored locally and works in both console and GUI modes."));
    auto* form = new QFormLayout;
    auto* name = new QLineEdit;
    auto* username = new QLineEdit;
    auto* email = new QLineEdit;
    auto* skills = new QLineEdit;
    skills->setPlaceholderText("Comma-separated skills");
    auto* role = new QComboBox;
    role->addItems({"Administrator", "Team member"});
    auto* password = new QLineEdit;
    auto* confirmation = new QLineEdit;
    std::array<QLineEdit*, 3> securityAnswers{};
    password->setEchoMode(QLineEdit::Password);
    confirmation->setEchoMode(QLineEdit::Password);
    form->addRow("Full name", name);
    form->addRow("Username", username);
    form->addRow("Email (optional)", email);
    form->addRow("Skills", skills);
    form->addRow("Role", role);
    form->addRow("Password", password);
    form->addRow("Confirm password", confirmation);
    for (std::size_t index = 0; index < securityQuestions().size(); ++index) {
        securityAnswers[index] = new QLineEdit;
        securityAnswers[index]->setEchoMode(QLineEdit::Password);
        securityAnswers[index]->setPlaceholderText("Required recovery answer");
        form->addRow(q(securityQuestions()[index]), securityAnswers[index]);
    }
    layout->addLayout(form);
    layout->addWidget(mutedLabel(
        "Security answers are required, normalized for capitalization, and stored only as hashes."));
    auto* tutorial = button("Tutorial && Credits");
    tutorial->setObjectName("tonalButton");
    layout->addWidget(tutorial, 0, Qt::AlignLeft);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("Create account");
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(tutorial, &QPushButton::clicked, this, [this] { showTutorialAndCreditsDialog(); });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        std::vector<std::string> skillValues;
        for (const auto& part : util::split(s(skills->text()), ',')) {
            const auto cleaned = util::trim(part);
            if (!cleaned.empty()) skillValues.push_back(cleaned);
        }
        std::vector<std::string> answerValues;
        for (auto* answer : securityAnswers) answerValues.push_back(s(answer->text()));
        if (perform([&] {
                service_->registerUser(s(name->text()), s(username->text()), s(password->text()),
                                      s(confirmation->text()), s(email->text()),
                                      role->currentIndex() == 0 ? UserRole::Admin : UserRole::TeamMember,
                                      skillValues, answerValues);
            }, "Account created")) {
            loginUsername_->setText(username->text().trimmed());
            dialog.accept();
            QMessageBox::information(this, "Account ready", "Your account was created. You can sign in now.");
        }
    });
    dialog.exec();
}

void TeamSyncGui::showForgotPasswordDialog() {
    QDialog dialog(this);
    dialog.setObjectName("recoveryDialog");
    dialog.setWindowTitle("Recover TeamSync password");
    dialog.setMinimumWidth(500);
    auto* layout = new QVBoxLayout(&dialog);
    auto* heading = new QLabel("Reset your password");
    heading->setObjectName("sectionTitle");
    layout->addWidget(heading);
    layout->addWidget(mutedLabel(
        "Answer the same three personal questions you completed when creating your account."));
    auto* form = new QFormLayout;
    auto* username = new QLineEdit;
    username->setObjectName("recoveryUsername");
    username->setText(loginUsername_->text().trimmed());
    std::array<QLineEdit*, 3> securityAnswers{};
    auto* replacement = new QLineEdit;
    replacement->setObjectName("recoveryPassword");
    replacement->setEchoMode(QLineEdit::Password);
    auto* confirmation = new QLineEdit;
    confirmation->setObjectName("recoveryConfirmation");
    confirmation->setEchoMode(QLineEdit::Password);
    form->addRow("Username", username);
    for (std::size_t index = 0; index < securityQuestions().size(); ++index) {
        securityAnswers[index] = new QLineEdit;
        securityAnswers[index]->setEchoMode(QLineEdit::Password);
        securityAnswers[index]->setPlaceholderText("Your original answer");
        form->addRow(q(securityQuestions()[index]), securityAnswers[index]);
    }
    form->addRow("New password", replacement);
    form->addRow("Confirm password", confirmation);
    layout->addLayout(form);
    layout->addWidget(mutedLabel(
        "Answers ignore capitalization and spaces at the beginning or end. Existing accounts can configure them from My profile."));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("Reset password");
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        std::vector<std::string> answerValues;
        for (auto* answer : securityAnswers) answerValues.push_back(s(answer->text()));
        if (perform([&] {
                service_->resetPassword(s(username->text()), answerValues,
                                        s(replacement->text()), s(confirmation->text()));
            }, "Password reset successfully")) {
            loginUsername_->setText(username->text().trimmed());
            loginPassword_->clear();
            dialog.accept();
            showConnectionToast("Password reset—sign in with your new password");
        }
    });
    dialog.exec();
}

void TeamSyncGui::showTutorialAndCreditsDialog() {
    QDialog dialog(this);
    dialog.setObjectName("tutorialCreditsDialog");
    dialog.setWindowTitle("Tutorial & Credits");
    dialog.resize(760, 680);
    dialog.setMinimumSize(580, 520);
    auto* outer = new QVBoxLayout(&dialog);
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);
    auto* heading = new QLabel("Tutorial & Credits");
    heading->setObjectName("dialogHeroTitle");
    layout->addWidget(heading);
    layout->addWidget(mutedLabel("Everything you need to get started with TeamSync."));

    const QStringList steps = {
        "1. Create an account and answer all three personal security questions.",
        "2. Sign in, create a team, and share its join code with your members.",
        "3. Team admins create projects and tasks. Any team member can start or complete team tasks.",
        "4. Use the bottom-right chat bubble and top-right notification bell to stay coordinated.",
        "5. Host or Join from the sign-in screen to synchronize computers on the same trusted Wi-Fi or hotspot.",
        "6. Add completion notes and files when finishing work; connected computers receive the update automatically."
    };
    for (const auto& step : steps) {
        auto* label = new QLabel(step);
        label->setWordWrap(true);
        label->setObjectName("tutorialStep");
        layout->addWidget(label);
    }

    auto* security = new QLabel(
        "<b>Password recovery</b><br>Your three answers are stored only as hashes. If you forget your password, enter the same answers. "
        "Capitalization and spaces at the beginning or end are ignored.");
    security->setWordWrap(true);
    security->setObjectName("materialInset");
    layout->addWidget(security);
    auto* credits = new QLabel(
        "<b>Developers</b><br><br>"
        "<b>Sudhan Bhattarai</b><br>"
        "<a href=\"https://www.sudhanb.com.np\">Portfolio &#8599;</a>"
        "&nbsp;&nbsp;&middot;&nbsp;&nbsp;"
        "<a href=\"https://github.com/SudhanTheDev\">GitHub &#8599;</a><br><br>"
        "<b>Severoos Nepali</b><br>"
        "<a href=\"https://www.severoos-nepali.com.np/\">Portfolio &#8599;</a>"
        "&nbsp;&nbsp;&middot;&nbsp;&nbsp;"
        "<a href=\"https://github.com/Severoos-ui\">GitHub &#8599;</a><br><br>"
        "<b>Project Support</b><br>Senior Sudip Khanal<br><br>"
        "<b>Academic Guidance</b><br>Ojan Adhikari");
    credits->setWordWrap(true);
    credits->setTextFormat(Qt::RichText);
    credits->setTextInteractionFlags(Qt::TextBrowserInteraction);
    credits->setOpenExternalLinks(true);
    credits->setObjectName("materialInset");
    layout->addWidget(credits);
    layout->addStretch();
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    outer->addWidget(close);
    dialog.exec();
}

void TeamSyncGui::logout() {
    if (!currentUser_) return;
    if (QMessageBox::question(this, "Sign out", "Sign out of TeamSync?") != QMessageBox::Yes) return;
    perform([this] { service_->logout(currentUserId()); });
    if (chatDialog_) chatDialog_->hide();
    if (notificationsDialog_) notificationsDialog_->hide();
    currentUser_ = nullptr;
    signedInUser_->setText("Not signed in");
    rootStack_->setCurrentWidget(authPage_);
    loginPassword_->clear();
    loginUsername_->setFocus();
    animatePage(authPage_);
    showStatus("Signed out safely");
}

void TeamSyncGui::refreshCurrentPage() {
    if (!currentUser_ || !contentStack_) return;
    switch (static_cast<Page>(contentStack_->currentIndex())) {
        case DashboardPage: refreshDashboard(); break;
        case TeamsPage: refreshTeams(); break;
        case ProjectsPage: refreshProjects(); break;
        case TasksPage: refreshTasks(); break;
        case FilesPage: refreshTeamSelectors(); refreshFiles(); break;
        case ReportsPage: refreshTeamSelectors(); break;
        case ActivityPage: refreshTeamSelectors(); refreshActivities(); break;
        case ConnectionPage: refreshConnection(); break;
        case TutorialPage: break;
        case ProfilePage: refreshProfile(); break;
        default: break;
    }
}

void TeamSyncGui::refreshDashboard() {
    updateNotificationButton();
    perform([this] {
        const auto stats = Dashboard::calculate(*service_, currentUserId());
        const std::vector<int> values = {stats.joinedTeams, stats.projects, stats.pendingTasks,
                                         stats.inProgressTasks, stats.completedTasks, stats.unreadNotifications};
        for (std::size_t index = 0; index < dashboardValues_.size() && index < values.size(); ++index) {
            dashboardValues_[index]->setText(QString::number(values[index]));
        }
        dashboardStatusSummary_->setText(QString("%1 Pending   •   %2 In Progress   •   %3 Completed")
            .arg(stats.pendingTasks).arg(stats.inProgressTasks).arg(stats.completedTasks));
        recentMessages_->clear();
        for (const auto& message : stats.recentMessages) recentMessages_->addItem(q(message));
        if (stats.recentMessages.empty()) recentMessages_->addItem("No recent team messages yet.");
        recentActivities_->clear();
        for (const auto& activity : stats.recentActivities) recentActivities_->addItem(q(activity));
        if (stats.recentActivities.empty()) recentActivities_->addItem("No recent activity yet.");
    });
}

void TeamSyncGui::refreshTeams() {
    if (!currentUser_) return;
    perform([this] {
        const auto values = service_->teamsForUser(currentUserId());
        const auto query = teamSearch_ ? teamSearch_->text().trimmed() : QString{};
        teamsTable_->setRowCount(0);
        for (const auto* value : values) {
            if (!query.isEmpty() && !q(value->name()).contains(query, Qt::CaseInsensitive) &&
                !q(value->description()).contains(query, Qt::CaseInsensitive)) continue;
            const int row = teamsTable_->rowCount();
            teamsTable_->insertRow(row);
            auto* id = new QTableWidgetItem(QString::number(value->id()));
            id->setData(Qt::UserRole, value->id());
            teamsTable_->setItem(row, 0, id);
            teamsTable_->setItem(row, 1, new QTableWidgetItem(q(value->name())));
            teamsTable_->setItem(row, 2, new QTableWidgetItem(value->isAdmin(currentUserId()) ? "Team admin" : "Member"));
            teamsTable_->setItem(row, 3, new QTableWidgetItem(QString::number(value->memberIds().size())));
            teamsTable_->setItem(row, 4, new QTableWidgetItem(q(value->joinCode())));
            teamsTable_->setItem(row, 5, new QTableWidgetItem(value->status() == TeamStatus::Active ? "Active" : "Archived"));
        }
    });
}

void TeamSyncGui::refreshProjects() {
    if (!currentUser_) return;
    perform([this] {
        const auto values = service_->searchProjects(currentUserId(), s(projectSearch_ ? projectSearch_->text() : QString{}));
        projectsTable_->setRowCount(0);
        for (const auto* value : values) {
            const int row = projectsTable_->rowCount();
            projectsTable_->insertRow(row);
            auto* id = new QTableWidgetItem(QString::number(value->id()));
            id->setData(Qt::UserRole, value->id());
            projectsTable_->setItem(row, 0, id);
            projectsTable_->setItem(row, 1, new QTableWidgetItem(q(value->title())));
            projectsTable_->setItem(row, 2, new QTableWidgetItem(q(service_->team(value->teamId()).name())));
            projectsTable_->setItem(row, 3, new QTableWidgetItem(q(toString(value->status()))));
            projectsTable_->setItem(row, 4, new QTableWidgetItem(q(value->startDate().toString())));
            projectsTable_->setItem(row, 5, new QTableWidgetItem(q(value->dueDate().toString())));
        }
    });
}

void TeamSyncGui::refreshTasks() {
    if (!currentUser_) return;
    perform([this] {
        auto values = service_->searchTasks(currentUserId(), s(taskSearch_ ? taskSearch_->text() : QString{}));
        const int filter = taskFilter_ ? taskFilter_->currentIndex() : 0;
        if (filter > 0) {
            const auto wanted = static_cast<TaskStatus>(filter - 1);
            values.erase(std::remove_if(values.begin(), values.end(), [wanted](const Task* value) {
                return value->status() != wanted;
            }), values.end());
        }
        tasksTable_->setRowCount(0);
        for (const auto* value : values) {
            const int row = tasksTable_->rowCount();
            tasksTable_->insertRow(row);
            auto* id = new QTableWidgetItem(QString::number(value->id()));
            id->setData(Qt::UserRole, value->id());
            tasksTable_->setItem(row, 0, id);
            tasksTable_->setItem(row, 1, new QTableWidgetItem(q(value->title())));
            tasksTable_->setItem(row, 2, new QTableWidgetItem(q(service_->team(value->teamId()).name())));
            tasksTable_->setItem(row, 3, new QTableWidgetItem(q(service_->user(value->assignedUserId()).fullName())));
            tasksTable_->setItem(row, 4, new QTableWidgetItem(q(toString(value->priority()))));
            tasksTable_->setItem(row, 5, new QTableWidgetItem(q(toString(value->status()))));
            tasksTable_->setItem(row, 6, new QTableWidgetItem(q(value->dueDate().toString())));
            tasksTable_->setItem(row, 7, new QTableWidgetItem(q(value->requiredSkill())));
        }
    });
}

void TeamSyncGui::refreshTeamSelectors() {
    if (!currentUser_) return;
    const auto teams = service_->teamsForUser(currentUserId());
    auto fill = [&teams](QComboBox* combo, const bool includeAll) {
        if (!combo) return;
        const int previous = combo->currentData().toInt();
        const QSignalBlocker blocker(combo);
        combo->clear();
        if (includeAll) combo->addItem("All visible teams", 0);
        for (const auto* team : teams) combo->addItem(q(team->name()), team->id());
        const int index = combo->findData(previous);
        combo->setCurrentIndex(index >= 0 ? index : 0);
    };
    fill(chatTeam_, false);
    fill(filesTeam_, false);
    fill(reportTeam_, false);
    fill(activityTeam_, true);
}

void TeamSyncGui::refreshChat() {
    if (!currentUser_ || !chatMessages_) return;
    chatMessages_->clear();
    const int teamId = chatTeam_->currentData().toInt();
    if (teamId <= 0) {
        chatMessages_->addItem("Join or create a team to start a conversation.");
        return;
    }
    perform([this, teamId] {
        const auto messages = service_->messagesForTeam(currentUserId(), teamId);
        for (const auto& message : messages) {
            auto* item = new QListWidgetItem(
                q(service_->user(message.senderId()).fullName()) + "  •  " + q(message.dateTime()) +
                "\n" + q(message.text()));
            item->setData(Qt::UserRole, message.id());
            item->setData(Qt::UserRole + 1, message.senderId());
            chatMessages_->addItem(item);
        }
        if (messages.empty()) chatMessages_->addItem("No messages yet. Start the conversation below.");
        chatMessages_->scrollToBottom();
    });
}

void TeamSyncGui::refreshFiles() {
    if (!currentUser_ || !filesTable_) return;
    filesTable_->setRowCount(0);
    const int teamId = filesTeam_->currentData().toInt();
    if (teamId <= 0) return;
    perform([this, teamId] {
        const auto values = service_->filesForTeam(currentUserId(), teamId);
        for (const auto* value : values) {
            const int row = filesTable_->rowCount();
            filesTable_->insertRow(row);
            auto* id = new QTableWidgetItem(QString::number(value->id()));
            id->setData(Qt::UserRole, value->id());
            filesTable_->setItem(row, 0, id);
            filesTable_->setItem(row, 1, new QTableWidgetItem(q(value->fileName())));
            filesTable_->setItem(row, 2, new QTableWidgetItem(q(value->description())));
            filesTable_->setItem(row, 3, new QTableWidgetItem(q(service_->user(value->uploaderId()).fullName())));
            filesTable_->setItem(row, 4, new QTableWidgetItem(q(value->dateShared())));
            filesTable_->setItem(row, 5, new QTableWidgetItem(QString::number(value->recordedSize()) + " bytes"));
            filesTable_->setItem(row, 6, new QTableWidgetItem(value->exists() ? "Yes" : "Missing"));
        }
    });
}

void TeamSyncGui::refreshNotifications() {
    if (!currentUser_) return;
    updateNotificationButton();
    if (!notificationsTable_) return;
    perform([this] {
        const auto values = service_->notificationsForUser(currentUserId());
        notificationsTable_->setRowCount(0);
        for (const auto* value : values) {
            const int row = notificationsTable_->rowCount();
            notificationsTable_->insertRow(row);
            notificationsTable_->setItem(row, 0, new QTableWidgetItem(value->isRead() ? "Read" : "New"));
            notificationsTable_->setItem(row, 1, new QTableWidgetItem(q(value->text())));
            notificationsTable_->setItem(row, 2, new QTableWidgetItem(q(value->dateTime())));
            if (!value->isRead()) {
                for (int column = 0; column < notificationsTable_->columnCount(); ++column) {
                    notificationsTable_->item(row, column)->setForeground(QColor("#9ee8ff"));
                }
            }
        }
    });
}

void TeamSyncGui::updateNotificationButton() {
    if (!notificationButton_) return;
    int unread = 0;
    if (currentUser_) {
        for (const auto& notice : service_->notifications()) {
            if (notice.userId() == currentUser_->id() && !notice.isRead()) ++unread;
        }
    }
    notificationButton_->setText(unread > 0 ? QString::number(unread) : QString{});
    notificationButton_->setToolTip(unread > 0
        ? QString("%1 unread notification(s)").arg(unread)
        : "No unread notifications");
    notificationButton_->setProperty("hasUnread", unread > 0);
    notificationButton_->style()->unpolish(notificationButton_);
    notificationButton_->style()->polish(notificationButton_);
}

void TeamSyncGui::refreshActivities() {
    if (!currentUser_ || !activityTable_) return;
    const int teamId = activityTeam_->currentData().toInt();
    perform([this, teamId] {
        const auto values = service_->activitiesForUser(currentUserId(), teamId);
        activityTable_->setRowCount(0);
        for (const auto* value : values) {
            const int row = activityTable_->rowCount();
            activityTable_->insertRow(row);
            activityTable_->setItem(row, 0, new QTableWidgetItem(q(value->dateTime())));
            activityTable_->setItem(row, 1, new QTableWidgetItem(value->teamId() > 0 ? q(service_->team(value->teamId()).name()) : "—"));
            activityTable_->setItem(row, 2, new QTableWidgetItem(q(service_->user(value->userId()).fullName())));
            activityTable_->setItem(row, 3, new QTableWidgetItem(q(value->action())));
        }
    });
}

void TeamSyncGui::refreshProfile() {
    if (!currentUser_) return;
    profileName_->setText(q(currentUser_->fullName()));
    profileUsername_->setText(q(currentUser_->username()));
    profileEmail_->setText(q(currentUser_->email()));
    profileSkills_->setText(joined(currentUser_->skills()));
    profileRole_->setText(q(toString(currentUser_->role())) + "  •  Member since " +
        q(currentUser_->dateCreated()) + "  •  Recovery: " +
        (currentUser_->securityQuestionsConfigured() ? "Ready" : "Not configured"));
}

void TeamSyncGui::showCreateTeamDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Create team");
    dialog.setMinimumWidth(480);
    auto* layout = new QVBoxLayout(&dialog);
    auto* title = new QLabel("Create a new team");
    title->setObjectName("sectionTitle");
    layout->addWidget(title);
    auto* form = new QFormLayout;
    auto* name = new QLineEdit;
    auto* description = new QTextEdit;
    description->setMaximumHeight(110);
    form->addRow("Team name", name);
    form->addRow("Description", description);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("Create team");
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (perform([&] {
                const auto& created = service_->createTeam(currentUserId(), s(name->text()), s(description->toPlainText()));
                QApplication::clipboard()->setText(q(created.joinCode()));
                showStatus("Team created; join code copied to the clipboard");
            })) dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) {
        refreshTeams();
        refreshTeamSelectors();
    }
}

void TeamSyncGui::joinTeam() {
    bool accepted = false;
    const auto value = QInputDialog::getText(this, "Join a team", "Team ID or join code",
                                             QLineEdit::Normal, {}, &accepted);
    if (!accepted || value.trimmed().isEmpty()) return;
    if (perform([&] { service_->joinTeam(currentUserId(), s(value.trimmed())); }, "You joined the team")) {
        refreshTeams();
        refreshTeamSelectors();
    }
}

void TeamSyncGui::showTeamDetails() {
    const int teamId = selectedId(teamsTable_);
    if (teamId <= 0) return;
    perform([this, teamId] {
        const auto& selected = service_->team(teamId);
        QDialog dialog(this);
        dialog.setWindowTitle(q(selected.name()) + " - Members");
        dialog.resize(720, 500);
        auto* layout = new QVBoxLayout(&dialog);
        auto* title = new QLabel(q(selected.name()));
        title->setObjectName("sectionTitle");
        layout->addWidget(title);
        layout->addWidget(mutedLabel(q(selected.description())));
        auto* details = new QLabel("Join code: <b>" + q(selected.joinCode()).toHtmlEscaped() +
                                   "</b>   •   Created " + q(selected.dateCreated()));
        layout->addWidget(details);
        auto* members = table({"User ID", "Name", "Username", "Account role", "Team role"});
        for (const int memberId : selected.memberIds()) {
            const auto& member = service_->user(memberId);
            const int row = members->rowCount();
            members->insertRow(row);
            auto* id = new QTableWidgetItem(QString::number(member.id()));
            id->setData(Qt::UserRole, member.id());
            members->setItem(row, 0, id);
            members->setItem(row, 1, new QTableWidgetItem(q(member.fullName())));
            members->setItem(row, 2, new QTableWidgetItem(q(member.username())));
            members->setItem(row, 3, new QTableWidgetItem(q(toString(member.role()))));
            members->setItem(row, 4, new QTableWidgetItem(selected.isAdmin(memberId) ? "Team admin" : "Member"));
        }
        layout->addWidget(members, 1);
        auto* actions = new QHBoxLayout;
        auto* copyCode = button("Copy join code");
        actions->addWidget(copyCode);
        connect(copyCode, &QPushButton::clicked, &dialog, [&] {
            QApplication::clipboard()->setText(q(selected.joinCode()));
            showStatus("Join code copied");
        });
        if (selected.isAdmin(currentUserId())) {
            auto* promote = button("Promote member");
            auto* remove = button("Remove member");
            remove->setObjectName("dangerButton");
            actions->addWidget(promote);
            actions->addWidget(remove);
            connect(promote, &QPushButton::clicked, &dialog, [&] {
                const int memberId = selectedId(members);
                if (memberId > 0 && perform([&] { service_->promoteMember(currentUserId(), teamId, memberId); },
                                              "Member promoted")) dialog.accept();
            });
            connect(remove, &QPushButton::clicked, &dialog, [&] {
                const int memberId = selectedId(members);
                if (memberId <= 0) return;
                if (QMessageBox::question(&dialog, "Remove member", "Remove the selected member from this team?") == QMessageBox::Yes &&
                    perform([&] { service_->removeMember(currentUserId(), teamId, memberId); }, "Member removed")) dialog.accept();
            });
        }
        actions->addStretch();
        auto* close = button("Close", true);
        actions->addWidget(close);
        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
        layout->addLayout(actions);
        dialog.exec();
        refreshTeams();
    });
}

void TeamSyncGui::leaveSelectedTeam() {
    const int teamId = selectedId(teamsTable_);
    if (teamId <= 0) return;
    if (QMessageBox::question(this, "Leave team",
                              "Leave the selected team? Your historical activity will remain visible.") != QMessageBox::Yes) return;
    if (perform([this, teamId] { service_->leaveTeam(currentUserId(), teamId); }, "Team left")) {
        refreshTeams();
        refreshTeamSelectors();
    }
}

void TeamSyncGui::showProjectDialog(const bool editExisting) {
    int projectId = 0;
    const Project* existing = nullptr;
    if (editExisting) {
        projectId = selectedId(projectsTable_);
        if (projectId <= 0) return;
        try { existing = &service_->project(projectId); }
        catch (const std::exception& error) { QMessageBox::critical(this, "TeamSync", q(error.what())); return; }
    }
    const auto teams = service_->teamsForUser(currentUserId());
    if (!editExisting && teams.empty()) {
        QMessageBox::information(this, "Create a project", "Create or join a team before creating a project.");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(editExisting ? "Edit project" : "Create project");
    dialog.setMinimumWidth(540);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* team = new QComboBox;
    for (const auto* value : teams) team->addItem(q(value->name()), value->id());
    auto* title = new QLineEdit;
    auto* description = new QTextEdit;
    description->setMaximumHeight(100);
    auto* start = new QDateEdit(QDate::currentDate());
    start->setCalendarPopup(true);
    start->setDisplayFormat("yyyy-MM-dd");
    auto* due = new QDateEdit(QDate::currentDate().addDays(30));
    due->setCalendarPopup(true);
    due->setDisplayFormat("yyyy-MM-dd");
    if (existing) {
        team->setCurrentIndex(team->findData(existing->teamId()));
        team->setEnabled(false);
        title->setText(q(existing->title()));
        description->setPlainText(q(existing->description()));
        start->setDate(toQDate(existing->startDate()));
        due->setDate(toQDate(existing->dueDate()));
    }
    form->addRow("Team", team);
    form->addRow("Project title", title);
    form->addRow("Description", description);
    form->addRow("Start date", start);
    form->addRow("Due date", due);
    if (editExisting) form->addRow("Status", new QLabel(q(toString(existing->status())) + "  (automatic)"));
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText(editExisting ? "Save project" : "Create project");
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const bool completed = perform([&] {
            if (editExisting) {
                service_->editProject(currentUserId(), projectId, s(title->text()), s(description->toPlainText()),
                                     toTeamSyncDate(start->date()), toTeamSyncDate(due->date()));
            } else {
                service_->createProject(currentUserId(), team->currentData().toInt(), s(title->text()),
                                       s(description->toPlainText()), toTeamSyncDate(start->date()),
                                       toTeamSyncDate(due->date()));
            }
        }, editExisting ? "Project updated" : "Project created");
        if (completed) dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) refreshProjects();
}

void TeamSyncGui::archiveSelectedProject() {
    const int projectId = selectedId(projectsTable_);
    if (projectId <= 0) return;
    if (QMessageBox::question(this, "Archive project", "Archive the selected project?") != QMessageBox::Yes) return;
    if (perform([this, projectId] { service_->archiveProject(currentUserId(), projectId); }, "Project archived")) refreshProjects();
}

void TeamSyncGui::showTaskDialog(const bool editExisting) {
    int taskId = 0;
    const Task* existing = nullptr;
    if (editExisting) {
        taskId = selectedId(tasksTable_);
        if (taskId <= 0) return;
        try { existing = &service_->task(taskId); }
        catch (const std::exception& error) { QMessageBox::critical(this, "TeamSync", q(error.what())); return; }
    }
    const auto projects = service_->projectsForUser(currentUserId());
    if (!editExisting && projects.empty()) {
        QMessageBox::information(this, "Create a task", "Create an accessible project before creating a task.");
        return;
    }
    QDialog dialog(this);
    dialog.setWindowTitle(editExisting ? "Edit task" : "Create task");
    dialog.resize(610, 620);
    auto* layout = new QVBoxLayout(&dialog);
    auto* form = new QFormLayout;
    auto* project = new QComboBox;
    for (const auto* value : projects) project->addItem(q(value->title()) + "  •  " + q(service_->team(value->teamId()).name()), value->id());
    auto* type = new QComboBox;
    type->addItems({"General", "Development", "Research", "Documentation"});
    auto* title = new QLineEdit;
    auto* description = new QTextEdit;
    description->setMaximumHeight(100);
    auto* assignee = new QComboBox;
    auto* priority = new QComboBox;
    priority->addItems({"Low", "Medium", "High", "Urgent"});
    auto* due = new QDateEdit(QDate::currentDate().addDays(7));
    due->setCalendarPopup(true);
    due->setDisplayFormat("yyyy-MM-dd");
    auto* skill = new QLineEdit;
    skill->setPlaceholderText("Optional required skill");
    auto fillAssignees = [&, this] {
        assignee->clear();
        const int projectIdValue = project->currentData().toInt();
        if (projectIdValue <= 0) return;
        const auto& parent = service_->team(service_->project(projectIdValue).teamId());
        for (const int memberId : parent.memberIds()) {
            const auto& member = service_->user(memberId);
            assignee->addItem(q(member.fullName()) + "  (" + q(member.username()) + ")", memberId);
        }
    };
    connect(project, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [fillAssignees] { fillAssignees(); });
    if (existing) {
        project->setCurrentIndex(project->findData(existing->projectId()));
        project->setEnabled(false);
        type->setCurrentIndex(static_cast<int>(existing->type()));
        type->setEnabled(false);
        title->setText(q(existing->title()));
        description->setPlainText(q(existing->description()));
        priority->setCurrentIndex(static_cast<int>(existing->priority()) - 1);
        due->setDate(toQDate(existing->dueDate()));
        skill->setText(q(existing->requiredSkill()));
    }
    fillAssignees();
    if (existing) {
        assignee->setCurrentIndex(assignee->findData(existing->assignedUserId()));
        assignee->setEnabled(false);
    }
    form->addRow("Project", project);
    form->addRow("Task type", type);
    form->addRow("Title", title);
    form->addRow("Description", description);
    form->addRow("Assignee", assignee);
    form->addRow("Priority", priority);
    form->addRow("Due date", due);
    form->addRow("Required skill", skill);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText(editExisting ? "Save task" : "Create task");
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        const bool completed = perform([&] {
            if (editExisting) {
                service_->editTask(currentUserId(), taskId, s(title->text()), s(description->toPlainText()),
                                  static_cast<TaskPriority>(priority->currentIndex() + 1),
                                  toTeamSyncDate(due->date()), s(skill->text()));
            } else {
                service_->createTask(currentUserId(), project->currentData().toInt(),
                                    static_cast<TaskType>(type->currentIndex()), s(title->text()),
                                    s(description->toPlainText()), assignee->currentData().toInt(),
                                    static_cast<TaskPriority>(priority->currentIndex() + 1),
                                    toTeamSyncDate(due->date()), s(skill->text()));
            }
        }, editExisting ? "Task updated" : "Task created");
        if (completed) dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) refreshTasks();
}

void TeamSyncGui::startSelectedTask() {
    const int taskId = selectedId(tasksTable_);
    if (taskId <= 0) return;
    if (perform([this, taskId] { service_->startTask(currentUserId(), taskId); },
                "Task is now in progress")) {
        refreshTasks();
        refreshProjects();
    }
}

void TeamSyncGui::completeSelectedTask() {
    const int taskId = selectedId(tasksTable_);
    if (taskId <= 0) return;
    QDialog dialog(this);
    dialog.setWindowTitle("Complete task");
    dialog.resize(620, 470);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(mutedLabel("Describe what you finished. You may also attach files; TeamSync copies them into the shared workspace."));
    auto* note = new QTextEdit;
    note->setPlaceholderText("Completion note or finished content (optional)");
    note->setMaximumHeight(150);
    layout->addWidget(note);
    auto* files = new QListWidget;
    files->addItem("No files selected.");
    layout->addWidget(files, 1);
    auto* choose = button("Add files...");
    layout->addWidget(choose, 0, Qt::AlignLeft);
    QStringList selectedFiles;
    connect(choose, &QPushButton::clicked, &dialog, [&] {
        const auto picked = QFileDialog::getOpenFileNames(&dialog, "Choose completion files");
        for (const auto& path : picked) if (!selectedFiles.contains(path)) selectedFiles.push_back(path);
        files->clear();
        if (selectedFiles.empty()) files->addItem("No files selected.");
        else for (const auto& path : selectedFiles) files->addItem(path);
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("Mark completed");
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        std::vector<std::filesystem::path> paths;
        for (const auto& path : selectedFiles) paths.emplace_back(s(path));
        if (perform([&] { service_->markTaskComplete(currentUserId(), taskId,
                                                     s(note->toPlainText()), paths); },
                    "Task marked complete")) dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) {
        refreshTasks();
        refreshProjects();
    }
}

void TeamSyncGui::reopenSelectedTask() {
    const int taskId = selectedId(tasksTable_);
    if (taskId > 0 && perform([this, taskId] { service_->reopenTask(currentUserId(), taskId); },
                              "Task reopened")) {
        refreshTasks();
        refreshProjects();
    }
}

void TeamSyncGui::showCompletionDetails() {
    const int taskId = selectedId(tasksTable_);
    if (taskId <= 0) return;
    perform([this, taskId] {
        const auto& target = service_->task(taskId);
        const auto attachments = service_->attachmentsForTask(currentUserId(), taskId);
        QDialog dialog(this);
        dialog.setWindowTitle("Completion details");
        dialog.resize(650, 480);
        auto* layout = new QVBoxLayout(&dialog);
        auto* status = new QLabel("Status: " + q(toString(target.status())));
        status->setObjectName("sectionTitle");
        layout->addWidget(status);
        auto* note = new QTextEdit;
        note->setReadOnly(true);
        note->setPlainText(target.completionNote().empty() ? "No completion content was added." : q(target.completionNote()));
        note->setMaximumHeight(150);
        layout->addWidget(note);
        layout->addWidget(new QLabel("Attached files (double-click to open)"));
        auto* files = new QListWidget;
        for (const auto* attachment : attachments) {
            auto* item = new QListWidgetItem(q(attachment->fileName()) + "  (" +
                                             QString::number(attachment->size()) + " bytes)");
            item->setData(Qt::UserRole, attachment->id());
            files->addItem(item);
        }
        if (attachments.empty()) files->addItem("No completion files were added.");
        layout->addWidget(files, 1);
        connect(files, &QListWidget::itemDoubleClicked, &dialog, [this](QListWidgetItem* item) {
            const int id = item->data(Qt::UserRole).toInt();
            if (id <= 0) return;
            const auto path = service_->attachmentPath(currentUserId(), id);
            if (!QFileInfo::exists(QString::fromStdWString(path.wstring()))) {
                QMessageBox::warning(this, "TeamSync", "The attachment is missing from this workspace.");
                return;
            }
            QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(path.wstring())));
        });
        auto* close = button("Close");
        layout->addWidget(close, 0, Qt::AlignRight);
        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialog.exec();
    });
}

void TeamSyncGui::showTaskComments() {
    const int taskId = selectedId(tasksTable_);
    if (taskId <= 0) return;
    perform([this, taskId] {
        QDialog dialog(this);
        dialog.setWindowTitle("Task comments");
        dialog.resize(650, 480);
        auto* layout = new QVBoxLayout(&dialog);
        auto* title = new QLabel(q(service_->task(taskId).title()));
        title->setObjectName("sectionTitle");
        layout->addWidget(title);
        auto* comments = new QListWidget;
        for (const auto& value : service_->task(taskId).comments()) comments->addItem(q(value));
        if (service_->task(taskId).comments().empty()) comments->addItem("No comments or updates yet.");
        layout->addWidget(comments, 1);
        auto* composer = new QHBoxLayout;
        auto* input = new QLineEdit;
        input->setPlaceholderText("Add an update...");
        auto* add = button("Add comment", true);
        composer->addWidget(input, 1);
        composer->addWidget(add);
        layout->addLayout(composer);
        auto* close = button("Close");
        layout->addWidget(close, 0, Qt::AlignRight);
        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
        connect(add, &QPushButton::clicked, &dialog, [&] {
            if (input->text().trimmed().isEmpty()) return;
            if (perform([&] { service_->addTaskComment(currentUserId(), taskId, s(input->text())); }, "Comment added")) {
                comments->clear();
                for (const auto& value : service_->task(taskId).comments()) comments->addItem(q(value));
                input->clear();
            }
        });
        connect(input, &QLineEdit::returnPressed, add, &QPushButton::click);
        dialog.exec();
    });
}

void TeamSyncGui::showRecommendations() {
    const int taskId = selectedId(tasksTable_);
    if (taskId <= 0) return;
    perform([this, taskId] {
        const auto values = service_->recommendMembers(taskId);
        QDialog dialog(this);
        dialog.setWindowTitle("Smart member recommendation");
        dialog.resize(850, 500);
        auto* layout = new QVBoxLayout(&dialog);
        auto* title = new QLabel("Recommendations for “" + q(service_->task(taskId).title()) + "”");
        title->setObjectName("sectionTitle");
        layout->addWidget(title);
        layout->addWidget(mutedLabel("Transparent score: skill match, workload, completion rate, experience, and priority fit."));
        auto* results = table({"Rank", "Member", "Skill match", "Active", "Completed", "Completion rate", "Score"});
        int rank = 1;
        for (const auto& value : values) {
            const int row = results->rowCount();
            results->insertRow(row);
            results->setItem(row, 0, new QTableWidgetItem(QString::number(rank++)));
            results->setItem(row, 1, new QTableWidgetItem(q(value.memberName)));
            results->setItem(row, 2, new QTableWidgetItem(QString::number(value.skillMatchPercent) + "%"));
            results->setItem(row, 3, new QTableWidgetItem(QString::number(value.activeTasks)));
            results->setItem(row, 4, new QTableWidgetItem(QString::number(value.completedTasks)));
            results->setItem(row, 5, new QTableWidgetItem(QString::number(value.completionRate, 'f', 1) + "%"));
            results->setItem(row, 6, new QTableWidgetItem(QString::number(value.score) + "/100"));
        }
        layout->addWidget(results, 1);
        auto* close = button("Close", true);
        layout->addWidget(close, 0, Qt::AlignRight);
        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialog.exec();
    });
}

void TeamSyncGui::deleteSelectedTask() {
    const int taskId = selectedId(tasksTable_);
    if (taskId <= 0) return;
    if (QMessageBox::warning(this, "Delete task", "Permanently delete the selected task?",
                             QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
    if (perform([this, taskId] { service_->deleteTask(currentUserId(), taskId); }, "Task deleted")) refreshTasks();
}

void TeamSyncGui::sendChatMessage() {
    const int teamId = chatTeam_->currentData().toInt();
    const auto message = chatInput_->text().trimmed();
    if (teamId <= 0) {
        QMessageBox::information(this, "Team chat", "Choose or join a team first.");
        return;
    }
    if (message.isEmpty()) return;
    if (perform([this, teamId, message] { service_->sendMessage(currentUserId(), teamId, s(message)); },
                "Message sent")) {
        chatInput_->clear();
        refreshChat();
    }
}

void TeamSyncGui::deleteSelectedMessage() {
    auto* item = chatMessages_->currentItem();
    if (!item || item->data(Qt::UserRole).toInt() <= 0) {
        QMessageBox::information(this, "Delete message", "Select one of your messages first.");
        return;
    }
    const int messageId = item->data(Qt::UserRole).toInt();
    if (QMessageBox::question(this, "Delete message", "Delete the selected message?") != QMessageBox::Yes) return;
    if (perform([this, messageId] { service_->deleteMessage(currentUserId(), messageId); }, "Message deleted")) refreshChat();
}

void TeamSyncGui::shareFile() {
    const int teamId = filesTeam_->currentData().toInt();
    if (teamId <= 0) {
        QMessageBox::information(this, "Share a file", "Choose or join a team first.");
        return;
    }
    const auto path = QFileDialog::getOpenFileName(this, "Choose a local file to share");
    if (path.isEmpty()) return;
    QDialog dialog(this);
    dialog.setWindowTitle("Share file");
    dialog.setMinimumWidth(520);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(mutedLabel("TeamSync copies this file into the workspace and synchronizes it to all connected computers. Your original remains untouched."));
    auto* form = new QFormLayout;
    auto* displayName = new QLineEdit(QFileInfo(path).fileName());
    auto* description = new QLineEdit;
    auto* pathValue = new QLineEdit(path);
    pathValue->setReadOnly(true);
    form->addRow("Local path", pathValue);
    form->addRow("Display name", displayName);
    form->addRow("Description", description);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("Share file");
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (perform([&] {
                service_->shareFile(currentUserId(), teamId, std::filesystem::path(s(path)),
                                   s(displayName->text()), s(description->text()));
            }, "File shared and ready to synchronize")) dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) refreshFiles();
}

void TeamSyncGui::openSelectedFile() {
    const int fileId = selectedId(filesTable_);
    if (fileId <= 0) return;
    perform([this, fileId] {
        auto& value = service_->sharedFile(fileId);
        if (!value.exists()) throw ValidationError("The file has been moved or deleted from its recorded path.");
        if (value.isExecutable()) {
            if (QMessageBox::warning(this, "Executable file",
                "This file may run code. Open it only if you trust the uploader and the file.",
                QMessageBox::Open | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Open) return;
        } else if (QMessageBox::question(this, "Open file", "Open this file with its default application?") != QMessageBox::Yes) {
            return;
        }
        if (!value.openWithDefaultApplication()) throw TeamSyncError("Windows could not open the selected file.");
        showStatus("Open request sent to Windows");
    });
}

void TeamSyncGui::removeSelectedFile() {
    const int fileId = selectedId(filesTable_);
    if (fileId <= 0) return;
    if (QMessageBox::question(this, "Remove file record",
        "Remove only the TeamSync record? The original local file will not be deleted.") != QMessageBox::Yes) return;
    if (perform([this, fileId] { service_->removeSharedFile(currentUserId(), fileId); },
                "File record removed; original file unchanged")) refreshFiles();
}

void TeamSyncGui::generateReport() {
    const int teamId = reportTeam_->currentData().toInt();
    if (teamId <= 0) {
        QMessageBox::information(this, "Generate report", "Choose or join a team first.");
        return;
    }
    const auto type = static_cast<ReportType>(reportType_->currentIndex() + 1);
    perform([this, teamId, type] {
        ReportGenerator generator(*service_);
        reportPreview_->setPlainText(q(generator.generate(type, currentUserId(), teamId)));
        service_->recordReportGenerated(currentUserId(), teamId, toString(type));
    }, "Report generated");
}

void TeamSyncGui::exportReport() {
    const int teamId = reportTeam_->currentData().toInt();
    if (teamId <= 0) {
        QMessageBox::information(this, "Export report", "Choose or join a team first.");
        return;
    }
    bool accepted = false;
    const auto name = QInputDialog::getText(this, "Export report",
        "Optional file name (leave blank for an automatic name)", QLineEdit::Normal, {}, &accepted);
    if (!accepted) return;
    const auto type = static_cast<ReportType>(reportType_->currentIndex() + 1);
    perform([this, teamId, type, name] {
        ReportGenerator generator(*service_);
        const auto path = generator.exportText(type, currentUserId(), teamId, s(name.trimmed()));
        service_->recordReportGenerated(currentUserId(), teamId, "exported " + toString(type));
        QMessageBox::information(this, "Report exported", "Saved to:\n" + q(path.string()));
    }, "Report exported");
}

void TeamSyncGui::saveProfile() {
    std::vector<std::string> skills;
    for (const auto& part : util::split(s(profileSkills_->text()), ',')) {
        const auto value = util::trim(part);
        if (!value.empty()) skills.push_back(value);
    }
    if (perform([this, &skills] {
            service_->editProfile(currentUserId(), s(profileName_->text()), s(profileEmail_->text()), skills);
        }, "Profile updated")) {
        signedInUser_->setText("<b>" + q(currentUser_->fullName()).toHtmlEscaped() + "</b><br>" +
                               q(currentUser_->username()).toHtmlEscaped() + "  •  " + q(toString(currentUser_->role())));
        refreshProfile();
    }
}

void TeamSyncGui::showChangePasswordDialog() {
    QDialog dialog(this);
    dialog.setWindowTitle("Change password");
    dialog.setMinimumWidth(460);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(mutedLabel("Choose a password you do not reuse elsewhere."));
    auto* form = new QFormLayout;
    auto* current = new QLineEdit;
    auto* replacement = new QLineEdit;
    auto* confirmation = new QLineEdit;
    current->setEchoMode(QLineEdit::Password);
    replacement->setEchoMode(QLineEdit::Password);
    confirmation->setEchoMode(QLineEdit::Password);
    form->addRow("Current password", current);
    form->addRow("New password", replacement);
    form->addRow("Confirm password", confirmation);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("Change password");
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (perform([&] {
                service_->changePassword(currentUserId(), s(current->text()), s(replacement->text()),
                                        s(confirmation->text()));
            }, "Password changed")) dialog.accept();
    });
    dialog.exec();
}

void TeamSyncGui::showSecurityQuestionsDialog() {
    if (!currentUser_) return;
    QDialog dialog(this);
    dialog.setWindowTitle("Configure security questions");
    dialog.setMinimumWidth(620);
    auto* layout = new QVBoxLayout(&dialog);
    auto* heading = new QLabel(currentUser_->securityQuestionsConfigured()
        ? "Replace your recovery answers" : "Protect your account");
    heading->setObjectName("sectionTitle");
    layout->addWidget(heading);
    layout->addWidget(mutedLabel(
        "Enter your current password and answer all three questions. Existing answers are never displayed."));
    auto* form = new QFormLayout;
    auto* currentPassword = new QLineEdit;
    currentPassword->setEchoMode(QLineEdit::Password);
    form->addRow("Current password", currentPassword);
    std::array<QLineEdit*, 3> answers{};
    for (std::size_t index = 0; index < securityQuestions().size(); ++index) {
        answers[index] = new QLineEdit;
        answers[index]->setEchoMode(QLineEdit::Password);
        answers[index]->setPlaceholderText("New recovery answer");
        form->addRow(q(securityQuestions()[index]), answers[index]);
    }
    layout->addLayout(form);
    layout->addWidget(mutedLabel("Use memorable answers that other people cannot easily discover."));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("Save security questions");
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        std::vector<std::string> values;
        for (auto* answer : answers) values.push_back(s(answer->text()));
        if (perform([&] {
                service_->setSecurityAnswers(currentUserId(), s(currentPassword->text()), values);
            }, "Security questions saved")) {
            refreshProfile();
            dialog.accept();
            showConnectionToast("Account recovery is ready");
        }
    });
    dialog.exec();
}

} // namespace teamsync
