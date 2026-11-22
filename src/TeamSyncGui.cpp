#include "TeamSyncGui.h"
#include "AppSettings.h"

#include "Dashboard.h"
#include "Exceptions.h"
#include "LanSyncManager.h"
#include "ReportGenerator.h"
#include "ThemeManager.h"
#include "PremiumWidgets.h"
#include "FileDropSupport.h"
#include "WorkspaceRecovery.h"
#include "Utils.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QAction>
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
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHeaderView>
#include <QIcon>
#include <QGraphicsDropShadowEffect>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QPainter>
#include <QProgressBar>
#include <QParallelAnimationGroup>
#include <QProgressDialog>
#include <QPointer>
#include <QScrollArea>
#include <QScopedValueRollback>
#include <QResizeEvent>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStandardItemModel>
#include <QStyle>
#include <QTableWidget>
#include <QTableView>
#include <QThread>
#include <QTabWidget>
#include <QStyleHints>
#include <QTextEdit>
#include <QToolButton>
#include <QSvgRenderer>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <sstream>
#include <set>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#endif

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

void iconFor(QPushButton* control, const QString& name) {
    control->setProperty("iconName", name);
    control->setIcon(ThemeManager::icon(name, qApp->property("darkTheme").toBool(), false, control->property("primary").toBool()));
    control->setIconSize(QSize(16, 16));
}

QPushButton* button(const QString& text, const bool primary = false) {
    auto* result = new ui::AnimatedButton(text);
    result->setCursor(Qt::PointingHandCursor);
    result->setMinimumHeight(34);
    result->setProperty("primary", primary);
    result->setProperty("iconOnly", text.isEmpty());
    result->setFocusPolicy(Qt::StrongFocus);
    result->setAccessibleName(QString(text).replace("&&", "&"));
    result->setAutoDefault(false);
    const QString label = text.toLower();
    if (label.startsWith("new ") || label.startsWith("create ")) iconFor(result, "plus");
    else if (label.startsWith("refresh") || label == "sync now") iconFor(result, "refresh");
    else if (label.startsWith("copy ")) iconFor(result, "copy");
    else if (label.startsWith("host")) iconFor(result, "host");
    else if (label.contains("join")) iconFor(result, "join");
    else if (label.contains("complete")) iconFor(result, "check");
    return result;
}

// A viewport label keeps empty tables useful without fabricating a data row.
class TableEmptyState final : public QLabel {
public:
    explicit TableEmptyState(QTableView* view) : QLabel(view->viewport()), view_(view) {
        setObjectName("emptyState");
        setAlignment(Qt::AlignCenter);
        setWordWrap(true);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        view->viewport()->installEventFilter(this);
        auto update = [this] {
            setText(view_->property("emptyText").toString().isEmpty()
                ? "Nothing to show yet." : view_->property("emptyText").toString());
            setVisible(view_->model() && view_->model()->rowCount() == 0);
            setGeometry(view_->viewport()->rect().adjusted(20, 12, -20, -12));
        };
        if (view->model()) {
            QObject::connect(view->model(), &QAbstractItemModel::rowsInserted, this, update);
            QObject::connect(view->model(), &QAbstractItemModel::rowsRemoved, this, update);
            QObject::connect(view->model(), &QAbstractItemModel::modelReset, this, update);
        }
        QTimer::singleShot(0, this, update);
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == view_->viewport() && event->type() == QEvent::Resize)
            setGeometry(view_->viewport()->rect().adjusted(20, 12, -20, -12));
        return QLabel::eventFilter(watched, event);
    }
private:
    QTableView* view_;
};

void configureTable(QTableView* result) {
    result->horizontalHeader()->setStretchLastSection(true);
    result->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    result->horizontalHeader()->setMinimumSectionSize(48);
    result->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    result->setSelectionBehavior(QAbstractItemView::SelectRows);
    result->setSelectionMode(QAbstractItemView::SingleSelection);
    result->setEditTriggers(QAbstractItemView::NoEditTriggers);
    result->setAlternatingRowColors(false);
    result->verticalHeader()->setVisible(false);
    result->verticalHeader()->setDefaultSectionSize(40);
    result->setShowGrid(false);
    result->setWordWrap(false);
    result->setTextElideMode(Qt::ElideRight);
    result->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    result->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    result->setCornerButtonEnabled(false);
    result->setItemDelegate(new ui::ReferenceItemDelegate(result));
    new TableEmptyState(result);
}

QTableWidget* table(const QStringList& headings) {
    auto* result = new QTableWidget(0, headings.size());
    result->setHorizontalHeaderLabels(headings);
    configureTable(result);
    QObject::connect(result, &QTableWidget::itemChanged, result, [](QTableWidgetItem* item) {
        if (item->toolTip() != item->text()) item->setToolTip(item->text());
    });
    return result;
}

void requireSelection(QTableWidget* source, const std::initializer_list<QPushButton*>& controls) {
    const QList<QPushButton*> buttons(controls);
    for (auto* control : buttons) control->setEnabled(false);
    QObject::connect(source, &QTableWidget::itemSelectionChanged, source, [source, buttons] {
        const bool selected = !source->selectedItems().empty();
        for (auto* control : buttons) control->setEnabled(selected);
    });
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
    ui::softShadow(result);
    return result;
}

QString joined(const std::vector<std::string>& values) {
    QStringList result;
    for (const auto& value : values) result << q(value);
    return result.join(", ");
}

QByteArray protectSavedPassword(const QByteArray& value) {
#ifdef Q_OS_WIN
    DATA_BLOB source{static_cast<DWORD>(value.size()),
                     reinterpret_cast<BYTE*>(const_cast<char*>(value.constData()))};
    DATA_BLOB encrypted{};
    if (!CryptProtectData(&source, L"TeamSync saved sign-in", nullptr, nullptr, nullptr, 0, &encrypted)) return {};
    const QByteArray result(reinterpret_cast<const char*>(encrypted.pbData), static_cast<int>(encrypted.cbData));
    LocalFree(encrypted.pbData);
    return result;
#else
    Q_UNUSED(value);
    return {};
#endif
}

QByteArray unprotectSavedPassword(const QByteArray& value) {
#ifdef Q_OS_WIN
    DATA_BLOB encrypted{static_cast<DWORD>(value.size()),
                        reinterpret_cast<BYTE*>(const_cast<char*>(value.constData()))};
    DATA_BLOB decrypted{};
    if (!CryptUnprotectData(&encrypted, nullptr, nullptr, nullptr, nullptr, 0, &decrypted)) return {};
    const QByteArray result(reinterpret_cast<const char*>(decrypted.pbData), static_cast<int>(decrypted.cbData));
    LocalFree(decrypted.pbData);
    return result;
#else
    Q_UNUSED(value);
    return {};
#endif
}

} // namespace

TeamSyncGui::TeamSyncGui(const std::filesystem::path& applicationRoot,
                         LanLaunchOptions network, QWidget* parent)
    : QMainWindow(parent), localRoot_(std::filesystem::absolute(applicationRoot)),
      activeRoot_(localRoot_), service_(std::make_unique<TeamSyncService>(localRoot_)),
      lan_(std::make_unique<LanSyncManager>(localRoot_)),
      networkOptions_(std::move(network)) {
    auto settings = appSettings();
    const QString preference = settings.value("appearance/theme", "system").toString();
    darkTheme_ = preference == "system" ? ThemeManager::systemPrefersDark()
        : preference == "dark";
    // Migrate the previous light/dark preference without losing the user's choice.
    if (!settings.contains("appearance/theme") && settings.contains("appearance/darkTheme")) {
        darkTheme_ = settings.value("appearance/darkTheme").toBool();
        settings.setValue("appearance/theme", darkTheme_ ? "dark" : "light");
    }
    popupAnimations_ = settings.value("appearance/popupAnimations", true).toBool();
    setWindowTitle("TeamSync");
    resize(1280, 900);
    setMinimumSize(980, 720);
    setWindowIcon(ThemeManager::applicationIcon());

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
    ui::installMotionEffects(this);
    configureLanCallbacks();
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (appSettings().value("appearance/theme", "system").toString() == "system")
            applyTheme(ThemeManager::systemPrefersDark());
    });
#endif
    auto* timer = new QTimer(this);
    timer->setInterval(2000);
    connect(timer, &QTimer::timeout, this, [this] {
        if (networkBusy_ || operationInProgress_ || QApplication::activeModalWidget() || QApplication::activePopupWidget()) return;
        if (remoteReloadPending_) { remoteReloadPending_ = false; reloadNetworkData(); }
        QScopedValueRollback<bool> background(backgroundSync_, true);
        pullNetworkChanges(false);
        updateConnectionUi();
    });
    timer->start();
    animateWindowIn();

    QTimer::singleShot(0, this, [this] { attemptRememberedLogin(); });
    QTimer::singleShot(120, this, [this] {
        const auto root = activeRoot_;
        const QPointer<TeamSyncGui> owner(this);
        backupWorker_ = std::thread([root, owner] {
            // Sign-in may atomically replace activity records while startup is
            // taking inventory. A checked, optimistic copy is safe to retry;
            // normal login activity is not a user-facing recovery error.
            for (int attempt = 0; attempt < 3; ++attempt) {
                try { WorkspaceRecovery(root).createBackup("Automatic daily safety backup", true); return; }
                catch (const std::exception&) {
                    if (attempt < 2) std::this_thread::sleep_for(std::chrono::milliseconds(350 * (attempt + 1)));
                }
            }
            if (owner) QMetaObject::invokeMethod(owner, [owner] {
                if (owner) owner->showStatus("Automatic backup postponed while files are busy. You can create a backup from Settings.");
            }, Qt::QueuedConnection);
        });
    });

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

TeamSyncGui::~TeamSyncGui() {
    if (backupWorker_.joinable()) backupWorker_.join();
}

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
    auto* page = new ui::ReferenceBackdrop;
    page->setObjectName("authPage");
    auto* outer = new QHBoxLayout(page);
    outer->setContentsMargins(32, 32, 32, 24);
    outer->setSpacing(32);

    auto* brand = new QWidget;
    brand->setObjectName("loginBrand");
    auto* brandLayout = new QVBoxLayout(brand);
    brandLayout->setContentsMargins(8, 20, 8, 0);
    brandLayout->setSpacing(12);
    brandLayout->addSpacing(20);
    auto* logo = new QLabel;
    logo->setPixmap(ThemeManager::applicationIcon().pixmap(80, 80));
    brandLayout->addWidget(logo, 0, Qt::AlignHCenter);
    auto* wordmark = new QLabel("TeamSync");
    wordmark->setObjectName("loginWordmark");
    brandLayout->addWidget(wordmark, 0, Qt::AlignHCenter);
    auto* tagline = new QLabel("Collaborate. Organize. Get things done.");
    tagline->setObjectName("loginTagline");
    tagline->setAlignment(Qt::AlignCenter);
    tagline->setWordWrap(true);
    brandLayout->addWidget(tagline);
    brandLayout->addSpacing(18);
    auto* features = new QWidget;
    features->setMaximumWidth(380);
    auto* featureLayout = new QVBoxLayout(features);
    featureLayout->setContentsMargins(0, 0, 0, 0);
    featureLayout->setSpacing(24);
    const std::array<QStringList, 3> highlights{{
        {"teams", "Work together", "Manage projects and tasks\nwith your team."},
        {"activity", "Stay in sync", "Share files, discuss ideas\nand get real-time updates."},
        {"tasks", "Your workspace", "Local-first with optional\nteam connection."}
    }};
    for (const auto& feature : highlights) {
        auto* row = new QHBoxLayout;
        row->setSpacing(16);
        auto* tile = new ui::IconTile(feature[0], "blue", 52);
        tile->setProperty("circular", true);
        row->addWidget(tile);
        auto* text = new QVBoxLayout;
        text->setSpacing(4);
        auto* title = new QLabel(feature[1]);
        title->setObjectName("featureTitle");
        auto* description = new QLabel(feature[2]);
        description->setObjectName("featureBody");
        description->setWordWrap(true);
        text->addWidget(title);
        text->addWidget(description);
        row->addLayout(text, 1);
        featureLayout->addLayout(row);
    }
    brandLayout->addWidget(features, 0, Qt::AlignHCenter);
    brandLayout->addSpacing(12);
    class CollaborationArt final : public QWidget {
    public:
        CollaborationArt() : renderer_(QString(":/teamsync/team-collaboration.svg")) {
            setMinimumHeight(100);
            setMaximumHeight(285);
            setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        }
    protected:
        void paintEvent(QPaintEvent*) override {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing);
            QSizeF size = renderer_.viewBoxF().size();
            size.scale(this->size(), Qt::KeepAspectRatio);
            renderer_.render(&painter, QRectF((width() - size.width())/2, height() - size.height(), size.width(), size.height()));
        }
    private:
        QSvgRenderer renderer_;
    };
    brandLayout->addWidget(new CollaborationArt, 2);
    outer->addWidget(brand, 44);

    auto* right = new QWidget;
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(8, 8, 12, 8);
    rightLayout->setSpacing(16);
    rightLayout->addStretch();
    auto* loginCard = card();
    loginCard->setObjectName("authCard");
    loginCard->setMinimumWidth(412);
    loginCard->setMaximumWidth(530);
    ui::softShadow(loginCard, 38, 10);
    auto* form = new QVBoxLayout(loginCard);
    form->setSizeConstraint(QLayout::SetDefaultConstraint);
    form->setContentsMargins(32, 28, 32, 26);
    form->setSpacing(12);
    auto* heading = new QLabel("Welcome back");
    heading->setObjectName("authHeading");
    form->addWidget(heading);
    form->addWidget(mutedLabel("Sign in to your TeamSync workspace"));
    form->addSpacing(8);
    loginUsername_ = new QLineEdit;
    loginUsername_->setObjectName("loginUsername");
    loginUsername_->setAccessibleName("Username");
    loginUsername_->setPlaceholderText("Username");
    loginUsername_->setMinimumHeight(38);
    loginUsername_->setClearButtonEnabled(true);
    loginUsername_->addAction(ThemeManager::icon("user", darkTheme_), QLineEdit::LeadingPosition);
    loginPassword_ = new QLineEdit;
    loginPassword_->setObjectName("loginPassword");
    loginPassword_->setAccessibleName("Password");
    loginPassword_->setPlaceholderText("Password");
    loginPassword_->setMinimumHeight(38);
    loginPassword_->setEchoMode(QLineEdit::Password);
    loginPassword_->addAction(ThemeManager::icon("lock", darkTheme_), QLineEdit::LeadingPosition);
    auto* reveal = loginPassword_->addAction(ThemeManager::icon("eye", darkTheme_), QLineEdit::TrailingPosition);
    reveal->setObjectName("passwordVisibilityAction");
    reveal->setToolTip("Show password");
    connect(reveal, &QAction::triggered, this, [this, reveal] {
        const bool show = loginPassword_->echoMode() == QLineEdit::Password;
        loginPassword_->setEchoMode(show ? QLineEdit::Normal : QLineEdit::Password);
        reveal->setIcon(ThemeManager::icon(show ? "eye-off" : "eye", darkTheme_));
        reveal->setToolTip(show ? "Hide password" : "Show password");
    });
    form->addWidget(loginUsername_);
    form->addWidget(loginPassword_);
    auto* options = new QHBoxLayout;
    rememberLogin_ = new QCheckBox("Keep me signed in");
    rememberLogin_->setObjectName("rememberLogin");
    rememberLogin_->setChecked(true);
    rememberLogin_->setToolTip("Save sign-in with Windows account encryption");
    options->addWidget(rememberLogin_);
    options->addStretch();
    auto* forgot = button("Forgot password?");
    forgot->setObjectName("forgotPasswordButton");
    forgot->setProperty("link", true);
    options->addWidget(forgot);
    form->addLayout(options);
    auto* login = button("Sign in", true);
    login->setObjectName("loginButton");
    login->setMinimumHeight(42);
    form->addWidget(login);
    auto* divider = new QHBoxLayout;
    auto* lineA = new QFrame; lineA->setFrameShape(QFrame::HLine);
    auto* lineB = new QFrame; lineB->setFrameShape(QFrame::HLine);
    lineA->setObjectName("authDivider");
    lineB->setObjectName("authDivider");
    divider->addWidget(lineA, 1);
    auto* orLabel = mutedLabel("or");
    orLabel->setAlignment(Qt::AlignCenter);
    divider->addWidget(orLabel);
    divider->addWidget(lineB, 1);
    form->addLayout(divider);
    auto* registration = button("Create a new account");
    registration->setIcon({});
    registration->setProperty("iconName", QString());
    registration->setObjectName("registerButton");
    auto* guest = button("Continue as guest");
    guest->setObjectName("guestButton");
    form->addWidget(registration);
    form->addWidget(guest);
    form->addSpacing(4);

    auto* connection = new QFrame;
    connection->setObjectName("connectionPanel");
    auto* connectionLayout = new QVBoxLayout(connection);
    connectionLayout->setContentsMargins(16, 14, 16, 14);
    connectionLayout->setSpacing(10);
    auto* connectionTitle = new QLabel("Workspace connection");
    connectionTitle->setObjectName("connectionPanelTitle");
    connectionLayout->addWidget(connectionTitle);
    auto* choices = new QHBoxLayout;
    choices->setSpacing(8);
    auto* choiceGroup = new QButtonGroup(connection);
    auto* local = button("Local");
    local->setObjectName("localConnectionButton");
    auto* host = button("Host");
    host->setObjectName("hostConnectionButton");
    auto* join = button("Join");
    join->setObjectName("joinConnectionButton");
    for (auto* choice : {local, host, join}) {
        choice->setIcon({});
        choice->setProperty("iconName", QString());
        choice->setProperty("connectionChoice", true);
        choice->setCheckable(true);
        choiceGroup->addButton(choice);
        choices->addWidget(choice, 1);
    }
    local->setChecked(true);
    connectionLayout->addLayout(choices);
    networkStatus_ = mutedLabel("Use a local workspace on this computer");
    networkStatus_->setObjectName("authConnectionStatus");
    networkStatus_->setWordWrap(true);
    networkStatus_->setMinimumHeight(30);
    networkStatus_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    connectionLayout->addWidget(networkStatus_);
    form->addWidget(connection);
    rightLayout->addWidget(loginCard);
    auto* footer = new QHBoxLayout;
    footer->addStretch();
    auto* taglineFooter = mutedLabel("●  ●   Simple tools. Strong teams.");
    taglineFooter->setWordWrap(false);
    taglineFooter->setObjectName("loginFooter");
    footer->addWidget(taglineFooter);
    auto* help = button("Tutorial && Credits");
    help->setProperty("link", true);
    footer->addWidget(help);
    authThemeButton_ = button("");
    authThemeButton_->setObjectName("themeButton");
    authThemeButton_->setAccessibleName("Switch color theme");
    footer->addWidget(authThemeButton_);
    footer->addStretch();
    rightLayout->addLayout(footer);
    rightLayout->addStretch();
    auto* scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(right);
    outer->addWidget(scroll, 56);

    QWidget::setTabOrder(loginUsername_, loginPassword_);
    QWidget::setTabOrder(loginPassword_, rememberLogin_);
    QWidget::setTabOrder(rememberLogin_, forgot);
    QWidget::setTabOrder(forgot, login);
    QWidget::setTabOrder(login, registration);
    QWidget::setTabOrder(registration, guest);
    connect(login, &QPushButton::clicked, this, [this] { attemptLogin(); });
    connect(loginPassword_, &QLineEdit::returnPressed, this, [this] { attemptLogin(); });
    connect(loginUsername_, &QLineEdit::returnPressed, loginPassword_, qOverload<>(&QWidget::setFocus));
    for (auto* input : {loginUsername_, loginPassword_}) connect(input, &QLineEdit::textChanged, this, [input] {
        input->setProperty("validationError", false);
        input->style()->unpolish(input); input->style()->polish(input);
    });
    connect(registration, &QPushButton::clicked, this, [this] { showRegistrationDialog(); });
    connect(guest, &QPushButton::clicked, this, [this] { showGuestDialog(); });
    connect(forgot, &QPushButton::clicked, this, [this] { showForgotPasswordDialog(); });
    connect(help, &QPushButton::clicked, this, [this] { showTutorialAndCreditsDialog(); });
    connect(authThemeButton_, &QPushButton::clicked, this, [this] { toggleTheme(); });
    connect(local, &QPushButton::clicked, this, [this] { switchToLocalWorkspace(); });
    connect(host, &QPushButton::clicked, this, [this] { startHostingDialog(); updateConnectionUi(); });
    connect(join, &QPushButton::clicked, this, [this] { showJoinWorkspaceDialog(); updateConnectionUi(); });
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
    sidebar->setFixedWidth(220);
    auto* navigation = new QVBoxLayout(sidebar);
    navigation->setContentsMargins(12, 22, 12, 12);
    navigation->setSpacing(4);
    auto* brandRow = new QHBoxLayout;
    brandRow->setContentsMargins(8, 0, 8, 0);
    brandRow->setSpacing(10);
    auto* logo = new QLabel;
    logo->setPixmap(ThemeManager::applicationIcon().pixmap(32, 32));
    brandRow->addWidget(logo);
    auto* name = new QLabel("TeamSync");
    name->setObjectName("sidebarLogo");
    brandRow->addWidget(name);
    brandRow->addStretch();
    navigation->addLayout(brandRow);
    navigation->addSpacing(18);
    auto* section = new QLabel("WORKSPACE");
    section->setObjectName("navSection");
    navigation->addWidget(section);
    const QStringList labels = {"Overview", "Teams", "Projects", "Tasks", "Shared files",
        "Reports", "Activity", "Connection", "Help && Tutorial", "My profile", "Settings"};
    const QStringList icons = {"overview", "teams", "projects", "tasks", "files",
        "reports", "activity", "connection", "help", "user", "settings"};
    navigationButtons_.resize(PageCount);
    auto* group = new QButtonGroup(sidebar);
    group->setExclusive(true);
    auto addNav = [&](int index, bool addToColumn = true) {
        auto* control = button(labels[index]);
        control->setObjectName("navButton");
        control->setProperty("pageIndex", index);
        control->setProperty("pageName", QString(labels[index]).replace("&&", "&"));
        control->setCheckable(true);
        control->setFixedHeight(40);
        iconFor(control, icons[index]);
        control->setToolTip(index < 8 ? labels[index] + QString(" (Ctrl+%1)").arg(index + 1) : labels[index]);
        group->addButton(control, index);
        if (addToColumn) navigation->addWidget(control);
        navigationButtons_[index] = control;
        connect(control, &QPushButton::clicked, this, [this, index] { navigateTo(static_cast<Page>(index)); });
        return control;
    };
    for (int i = 0; i < 8; ++i) addNav(i);
    navigationIndicator_ = new QWidget(sidebar);
    navigationIndicator_->setObjectName("navigationIndicator");
    navigationIndicator_->setAttribute(Qt::WA_TransparentForMouseEvents);
    navigationIndicator_->setAutoFillBackground(true);
    navigationIndicator_->hide();
    navigation->addStretch();
    addNav(SettingsPage);
    addNav(TutorialPage);
    auto* profileRow = new QHBoxLayout;
    profileRow->setContentsMargins(0, 6, 0, 0);
    profileRow->setSpacing(0);
    auto* profile = new ui::ProfileButton("My profile", "Team member");
    profile->setProperty("pageIndex", ProfilePage);
    profile->setProperty("pageName", "My profile");
    navigationButtons_[ProfilePage] = profile;
    profile->setCheckable(true);
    group->addButton(profile, ProfilePage);
    profileRow->addWidget(profile, 1);
    auto* accountMenu = new QMenu(profile);
    accountMenu->setObjectName("accountMenu");
    accountMenu->addAction("My profile", this, [this] { navigateTo(ProfilePage); });
    accountMenu->addAction("Settings", this, [this] { navigateTo(SettingsPage); });
    accountMenu->addSeparator();
    accountMenu->addAction("Sign out", this, [this] { logout(); });
    profile->setMenu(accountMenu);
    navigation->addLayout(profileRow);
    signedInUser_ = new QLabel(sidebar);
    signedInUser_->hide();

    auto* area = new QWidget;
    area->setObjectName("pageArea");
    auto* body = new QVBoxLayout(area);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);
    auto* header = new QFrame;
    header->setObjectName("pageHeader");
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(28, 24, 24, 20);
    headerLayout->setSpacing(12);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(4);
    pageTitle_ = new QLabel("Overview");
    pageTitle_->setObjectName("pageTitle");
    pageSubtitle_ = mutedLabel("Your workspace at a glance");
    pageSubtitle_->setObjectName("pageSubtitle");
    titles->addWidget(pageTitle_);
    titles->addWidget(pageSubtitle_);
    headerLayout->addLayout(titles, 1);
    globalSearch_ = new QLineEdit;
    globalSearch_->setObjectName("referenceSearch");
    globalSearch_->setPlaceholderText("Search anything...");
    globalSearch_->setAccessibleName("Search workspace");
    globalSearch_->setClearButtonEnabled(true);
    globalSearch_->setFixedWidth(264);
    globalSearch_->addAction(ThemeManager::icon("search", darkTheme_), QLineEdit::LeadingPosition);
    headerLayout->addWidget(globalSearch_);
    pageHeaderAction_ = button("New project", true);
    pageHeaderAction_->setObjectName("pageHeaderAction");
    iconFor(pageHeaderAction_, "plus");
    headerLayout->addWidget(pageHeaderAction_);
    pageHeaderAction_->hide();
    connect(pageHeaderAction_, &QPushButton::clicked, this, [this] {
        if (contentStack_->currentIndex() == ProjectsPage) showProjectDialog(false);
        else if (contentStack_->currentIndex() == TasksPage) showTaskDialog(false);
    });
    notificationButton_ = button("");
    notificationButton_->setObjectName("notificationButton");
    notificationButton_->setAccessibleName("Notifications");
    notificationButton_->setFixedSize(34, 34);
    iconFor(notificationButton_, "bell");
    auto* dot = new QLabel(notificationButton_);
    dot->setObjectName("notificationDot");
    dot->setFixedSize(8, 8);
    dot->move(24, 3);
    dot->hide();
    headerLayout->addWidget(notificationButton_);
    shellThemeButton_ = button("");
    shellThemeButton_->setObjectName("themeButton");
    shellThemeButton_->setAccessibleName("Switch color theme");
    headerLayout->addWidget(shellThemeButton_);
    body->addWidget(header);
    connectionBadge_ = button("Local");
    connectionBadge_->setParent(header);
    connectionBadge_->setObjectName("connectionBadge");
    connectionBadge_->hide();
    connect(notificationButton_, &QPushButton::clicked, this, [this] { showNotificationsPanel(); });
    connect(shellThemeButton_, &QPushButton::clicked, this, [this] { toggleTheme(); });
    connect(globalSearch_, &QLineEdit::returnPressed, this, [this] { showGlobalSearch(); });

    contentStack_ = new QStackedWidget;
    contentStack_->setObjectName("contentStack");
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
    contentStack_->addWidget(buildSettingsPage());
    body->addWidget(contentStack_, 1);
    auto* chatRow = new QHBoxLayout;
    chatRow->setContentsMargins(20, 0, 20, 8);
    chatRow->addStretch();
    floatingChatButton_ = button("");
    floatingChatButton_->setObjectName("floatingChatButton");
    floatingChatButton_->setFixedSize(36, 34);
    floatingChatButton_->setAccessibleName("Open team chat");
    floatingChatButton_->setToolTip("Team chat");
    iconFor(floatingChatButton_, "chat");
    chatRow->addWidget(floatingChatButton_);
    body->addLayout(chatRow);
    connect(floatingChatButton_, &QPushButton::clicked, this, [this] { showChatPanel(); });
    shell->addWidget(sidebar);
    shell->addWidget(area, 1);
    for (int index = 0; index < 8; ++index) {
        auto* shortcut = new QShortcut(QKeySequence(QString("Ctrl+%1").arg(index + 1)), this);
        connect(shortcut, &QShortcut::activated, this, [this, index] {
            if (currentUser_ && !networkBusy_) navigateTo(static_cast<Page>(index));
        });
    }
    auto* help = new QShortcut(QKeySequence(Qt::Key_F1), this);
    connect(help, &QShortcut::activated, this, [this] {
        if (currentUser_) navigateTo(TutorialPage); else showTutorialAndCreditsDialog();
    });
    auto* settings = new QShortcut(QKeySequence("Ctrl+,"), this);
    connect(settings, &QShortcut::activated, this, [this] { if (currentUser_) navigateTo(SettingsPage); });
    auto* reload = new QShortcut(QKeySequence("Ctrl+R"), this);
    connect(reload, &QShortcut::activated, this, [this] { if (currentUser_ && !networkBusy_) refreshCurrentPage(); });
    auto* search = new QShortcut(QKeySequence("Ctrl+K"), this);
    connect(search, &QShortcut::activated, this, [this] { if (currentUser_) { globalSearch_->show(); globalSearch_->setFocus(); } });
    return page;
}

bool TeamSyncGui::runNetworkOperation(const std::function<bool()>& action, const QString& message,
                                      LanSyncManager* operationManager) {
    if (networkBusy_) return false;
    networkBusy_ = true;
    auto* manager = operationManager ? operationManager : lan_.get();
    QProgressDialog progress(message, "Cancel transfer", 0, 0, QApplication::activeModalWidget()
        ? QApplication::activeModalWidget() : this);
    progress.setObjectName("networkProgress");
    progress.setWindowTitle("TeamSync");
    progress.setWindowModality(Qt::ApplicationModal);
    progress.setMinimumDuration(backgroundSync_ ? 2147483647 : 250);
    progress.setAutoClose(false);
    progress.setAutoReset(false);
    progress.setValue(0);
    connect(&progress, &QProgressDialog::canceled, &progress, [manager] { manager->cancelTransfer(); });
    QTimer details;
    details.setInterval(100);
    connect(&details, &QTimer::timeout, &progress, [&] {
        const auto value = manager->progress();
        if (!value.active) return;
        QString info = q(value.phase);
        if (!value.file.empty()) info += "\n" + q(value.file);
        if (value.total) {
            progress.setRange(0, 1000);
            progress.setValue(static_cast<int>(1000.0 * value.transferred / value.total));
            info += "\n" + QLocale().formattedDataSize(static_cast<qint64>(value.transferred)) + " / " +
                QLocale().formattedDataSize(static_cast<qint64>(value.total));
            if (value.bytesPerSecond > 0)
                info += " · " + QLocale().formattedDataSize(static_cast<qint64>(value.bytesPerSecond)) + "/s";
        } else progress.setRange(0, 0);
        progress.setLabelText(info);
    });
    details.start();
    QEventLoop loop;
    bool result = false;
    std::exception_ptr failure;
    auto* worker = QThread::create([&] {
        try { result = action(); } catch (...) { failure = std::current_exception(); }
    });
    connect(worker, &QThread::finished, &loop, &QEventLoop::quit);
    worker->start();
    loop.exec();
    worker->wait();
    details.stop();
    delete worker;
    progress.close();
    networkBusy_ = false;
    if (failure) std::rethrow_exception(failure);
    return result;
}

bool TeamSyncGui::perform(const std::function<void()>& action, const QString& successMessage) {
    if (networkBusy_) { showStatus("Synchronization is busy. Wait or cancel it from Transfers & sync.", false); return false; }
    const bool outer = !operationInProgress_;
    QScopedValueRollback<bool> guard(operationInProgress_, true);
    try {
        if (outer && remoteReloadPending_) { remoteReloadPending_ = false; reloadNetworkData(); }
        // A failed connection must not prevent local work. The shared engine
        // retains its baseline and never pulls over pending local changes.
        if (outer && lan_->mode() == LanSyncManager::Mode::Client && networkOnline_) {
            bool changed = false;
            QString problem;
            networkOnline_ = runNetworkOperation([&] { return lan_->sync(changed, problem); }, "Checking workspace updates…");
            lastNetworkError_ = problem;
            if (changed) reloadNetworkData();
        }
        const auto before = outer && lan_->mode() != LanSyncManager::Mode::Local
            ? lan_->workspaceFingerprint() : QByteArray{};
        lan_->executeLocalMutation(action);
        bool queued = false;
        if (outer && lan_->mode() != LanSyncManager::Mode::Local && before != lan_->workspaceFingerprint()) {
            lan_->noteLocalChange();
            if (lan_->mode() == LanSyncManager::Mode::Client) {
                queued = true;
                if (networkOnline_) {
                    QString problem;
                    networkOnline_ = runNetworkOperation([&] { return lan_->push(problem); }, "Sharing workspace changes…");
                    lastNetworkError_ = problem;
                    queued = !networkOnline_;
                }
            }
        }
        if (currentUser_) updateNotificationButton();
        updateConnectionUi();
        if (queued) showStatus("Saved on this computer · waiting to synchronize. Open Transfers & sync for details.");
        else if (!successMessage.isEmpty()) showStatus(successMessage);
        return true;
    } catch (const std::exception& error) {
        showStatus(q(error.what()), false);
        QMessageBox::critical(this, "TeamSync", q(error.what()));
        return false;
    }
}

bool TeamSyncGui::pullNetworkChanges(const bool showError) {
    if (networkBusy_) return false;
    if (lan_->mode() != LanSyncManager::Mode::Client) return true;
    bool changed = false;
    QString problem;
    if (!runNetworkOperation([&] { return lan_->sync(changed, problem); }, "Synchronizing workspace…")) {
        networkOnline_ = false;
        lastNetworkError_ = problem;
        updateConnectionUi();
        showStatus("Changes remain safe on this computer. Sync paused: " + problem, false);
        if (showError) QMessageBox::warning(this, "Synchronization paused",
            problem + "\n\nLocal edits are preserved. Use Retry or resolve the conflict from Transfers & sync.");
        return false;
    }
    networkOnline_ = true;
    lastNetworkError_.clear();
    if (changed) reloadNetworkData();
    return true;
}
void TeamSyncGui::reloadNetworkData() {
    const int userId = currentUser_ ? currentUser_->id() : 0;
    const auto username = currentUser_ ? currentUser_->username() : std::string{};
    lan_->executeLocalMutation([&] {
        currentUser_ = nullptr;
        service_->reload();
        if (userId > 0) {
            try {
                auto& candidate = service_->user(userId);
                if (candidate.username() == username && candidate.passwordHash() == util::hashPassword(s(sessionPassword_), username))
                    currentUser_ = &candidate;
            } catch (...) {}
        }
    });
    if (networkStatus_) networkStatus_->setText("Connection: " + lan_->statusText());
    updateConnectionUi();
    if (currentUser_) {
        refreshTeamSelectors();
        updateNotificationButton();
        refreshCurrentPage();
        if (chatDialog_ && chatDialog_->isVisible()) refreshChat();
        if (notificationsDialog_ && notificationsDialog_->isVisible()) refreshNotifications();
        showStatus("New changes received from the shared workspace");
    } else if (userId > 0) {
        if (chatDialog_) chatDialog_->hide();
        if (notificationsDialog_) notificationsDialog_->hide();
        guestSession_ = false;
        sessionPassword_.clear();
        signedInUser_->setText("Not signed in");
        rootStack_->setCurrentWidget(authPage_);
        showStatus("Your account is no longer available in this workspace. Sign in again.", false);
    }
}

std::filesystem::path TeamSyncGui::joinedCacheRoot(const QString& address, const quint16 port) const {
    const std::string identity = s(address.trimmed()) + ':' + std::to_string(port);
    return localRoot_ / "network_cache" / std::to_string(std::hash<std::string>{}(identity));
}

void TeamSyncGui::configureLanCallbacks() {
    const QPointer<TeamSyncGui> owner(this);
    const auto expectedRoot = activeRoot_;
    lan_->setRemoteCommitHandler([owner, expectedRoot] {
        if (!owner) return;
        QMetaObject::invokeMethod(owner, [owner, expectedRoot] {
            if (owner && owner->activeRoot_ == expectedRoot) owner->remoteReloadPending_ = true;
        }, Qt::QueuedConnection);
    });
}

void TeamSyncGui::startHostingDialog() {
    if (networkBusy_) return;
    if (lan_->mode() == LanSyncManager::Mode::Host) {
        showConnectionToast("Already hosting · Pairing code " + lan_->pairingCode());
        return;
    }
    if (lan_->mode() == LanSyncManager::Mode::Client) {
        switchToLocalWorkspace();
        if (lan_->mode() != LanSyncManager::Mode::Local) return;
    }
    QDialog dialog(this);
    dialog.setObjectName("hostDialog");
    dialog.setWindowTitle("Host this workspace");
    dialog.setMinimumWidth(480);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(mutedLabel("Other computers on the same Wi-Fi or hotspot will join this computer. Keep TeamSync open while collaborating."));
    layout->addWidget(mutedLabel("No IP address or port is needed. Other TeamSync computers can find this workspace automatically and enter the six-digit code shown after hosting starts."));
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
    if (!lan_->startHost(45454, error, networkOptions_.pairingCode)) {
        QMessageBox::critical(this, "Could not host workspace", error);
        showConnectionToast("Hosting could not start", false);
        return;
    }
    configureLanCallbacks();
    networkOnline_ = true;
    lastNetworkError_.clear();
    if (!currentUser_) {
        loginUsername_->clear();
        loginPassword_->clear();
    }
    updateConnectionUi();
    showConnectionToast("Hosting workspace · Pairing code " + lan_->pairingCode());
}

void TeamSyncGui::showJoinWorkspaceDialog() {
    if (networkBusy_) return;
    QDialog dialog(this);
    dialog.setObjectName("joinDialog");
    dialog.setWindowTitle("Join a TeamSync workspace");
    dialog.setMinimumWidth(540);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(mutedLabel("Find a nearby workspace, select it, and enter the six-digit code shown by its host. Use the same Wi-Fi or hotspot and the updated TeamSync version on every computer. GUI and console users can join each other."));
    auto* nearby = new QListWidget;
    nearby->setObjectName("nearbyWorkspaces");
    nearby->setMinimumHeight(130);
    nearby->addItem("Press Find nearby workspaces to search your network.");
    layout->addWidget(nearby);
    auto* find = button("Find nearby workspaces", true);
    layout->addWidget(find);
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
    form->addRow("Pairing code", code);
    auto* advanced = new QCheckBox("Enter connection details manually instead");
    advanced->setObjectName("joinManualConnection");
    layout->addWidget(advanced);
    auto* manual = new QWidget;
    auto* manualForm = new QFormLayout(manual);
    manualForm->setContentsMargins(0, 0, 0, 0);
    manualForm->addRow("Host address", address);
    manualForm->addRow("Network port", port);
    manual->setVisible(false);
    layout->addWidget(manual);
    layout->addLayout(form);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("Connect");
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(advanced, &QCheckBox::toggled, manual, &QWidget::setVisible);
    connect(find, &QPushButton::clicked, &dialog, [this, nearby, find] {
        find->setEnabled(false);
        nearby->clear();
        QList<LanSyncManager::NearbyWorkspace> workspaces;
        runNetworkOperation([&] { workspaces = LanSyncManager::discoverNearby(); return true; }, "Looking for nearby workspaces…");
        for (const auto& workspace : workspaces) {
            auto* item = new QListWidgetItem(workspace.name + "  •  Ready to join");
            item->setData(Qt::UserRole, workspace.address);
            item->setData(Qt::UserRole + 1, workspace.port);
            nearby->addItem(item);
        }
        if (workspaces.isEmpty()) nearby->addItem("No workspace found. Check Wi-Fi/hotspot and host status, then try again.");
        else nearby->setCurrentRow(0);
        find->setEnabled(true);
    });
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        QString selectedAddress = address->text().trimmed();
        quint16 selectedPort = static_cast<quint16>(port->value());
        if (!advanced->isChecked() && nearby->currentItem()) {
            selectedAddress = nearby->currentItem()->data(Qt::UserRole).toString();
            selectedPort = static_cast<quint16>(nearby->currentItem()->data(Qt::UserRole + 1).toUInt());
        }
        if (selectedAddress.isEmpty() || code->text().size() != 6) {
            QMessageBox::warning(&dialog, "Connection details", "Select a nearby workspace and enter its six-digit code. Use manual details only if discovery cannot find it.");
            return;
        }
        const auto cacheRoot = joinedCacheRoot(selectedAddress, selectedPort);
        auto candidate = std::make_unique<LanSyncManager>(cacheRoot);
        QString error;
        const QString pairingCode = code->text();
        const bool connected = runNetworkOperation([&] {
            return candidate->joinHost(selectedAddress, selectedPort, pairingCode, error);
        }, "Connecting and receiving workspace…", candidate.get());
        if (!connected) {
            QMessageBox::critical(&dialog, "Could not connect", error);
            return;
        }
        const QString previousUsername = currentUser_ ? q(currentUser_->username()) : QString();
        const QString previousPassword = sessionPassword_;
        if (chatDialog_) chatDialog_->hide();
        if (notificationsDialog_) notificationsDialog_->hide();
        currentUser_ = nullptr;
        activeRoot_ = cacheRoot;
        service_ = std::make_unique<TeamSyncService>(activeRoot_);
        lan_ = std::move(candidate);
        if (!previousUsername.isEmpty()) {
            try {
                currentUser_ = &service_->login(s(previousUsername), s(previousPassword));
            } catch (const std::exception&) {
                currentUser_ = nullptr;
                sessionPassword_.clear();
                guestSession_ = false;
                rootStack_->setCurrentWidget(authPage_);
                QMessageBox::information(&dialog, "Sign in required", "This shared workspace does not have a matching account. Please sign in or create an account there.");
            }
        }
        configureLanCallbacks();
        networkOnline_ = true;
        lastNetworkError_.clear();
        if (!currentUser_) {
            loginUsername_->clear();
            loginPassword_->clear();
        }
        updateConnectionUi();
        if (currentUser_) refreshSessionUi();
        dialog.accept();
    });
    if (dialog.exec() == QDialog::Accepted) {
        showConnectionToast("Connected—workspace synchronized successfully");
    }
}

void TeamSyncGui::switchToLocalWorkspace() {
    if (networkBusy_) return;
    if (lan_->mode() == LanSyncManager::Mode::Local && activeRoot_ == localRoot_) {
        showConnectionToast("Already using the local workspace");
        return;
    }
    if (QMessageBox::question(this, "Use local workspace",
            "Disconnect from sharing and return to this computer's local data?") != QMessageBox::Yes) return;
    const QString username = currentUser_ ? q(currentUser_->username()) : QString();
    const QString password = sessionPassword_;
    if (chatDialog_) chatDialog_->hide();
    if (notificationsDialog_) notificationsDialog_->hide();
    currentUser_ = nullptr;
    lan_ = std::make_unique<LanSyncManager>(localRoot_);
    activeRoot_ = localRoot_;
    service_ = std::make_unique<TeamSyncService>(activeRoot_);
    if (!username.isEmpty()) {
        try { currentUser_ = &service_->login(s(username), s(password)); }
        catch (const std::exception&) {}
    }
    configureLanCallbacks();
    networkOnline_ = true;
    lastNetworkError_.clear();
    if (currentUser_) refreshSessionUi();
    else {
        guestSession_ = false;
        sessionPassword_.clear();
        signedInUser_->setText("Not signed in");
        rootStack_->setCurrentWidget(authPage_);
        loginUsername_->setText(username);
        loginPassword_->clear();
        attemptRememberedLogin();
    }
    updateConnectionUi();
    showConnectionToast("Using this computer's local workspace");
}

void TeamSyncGui::refreshConnection() {
    updateConnectionUi();
}

void TeamSyncGui::updateConnectionUi() {
    if (networkBusy_) return;
    const auto mode = lan_->mode();
    for (const auto& name : {"localConnectionButton", "hostConnectionButton", "joinConnectionButton"}) {
        if (auto* control = findChild<QPushButton*>(name)) {
            const bool selected = QString(name) == "localConnectionButton" ? mode == LanSyncManager::Mode::Local :
                QString(name) == "hostConnectionButton" ? mode == LanSyncManager::Mode::Host : mode == LanSyncManager::Mode::Client;
            control->setChecked(selected);
        }
    }
    if (connectionBadge_) {
        connectionBadge_->setText(mode == LanSyncManager::Mode::Host ? "Hosting" :
            mode == LanSyncManager::Mode::Client ? (networkOnline_ ? "Connected" : "Offline") : "Local");
    }
    if (networkStatus_) {
        networkStatus_->setToolTip(lan_->statusText());
        if (mode == LanSyncManager::Mode::Host) networkStatus_->setText("Hosting · Pairing code " + lan_->pairingCode());
        else if (mode == LanSyncManager::Mode::Client && networkOnline_) networkStatus_->setText("Connected to a shared workspace");
        else if (mode == LanSyncManager::Mode::Client) networkStatus_->setText("Connection interrupted · Open Connection for details");
        else networkStatus_->setText("Use a local workspace on this computer");
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
    if (lan_->mode() == LanSyncManager::Mode::Client && lan_->hasPendingChanges())
        connectionRevisionLabel_->setText(connectionRevisionLabel_->text() + "  •  Local changes waiting to sync");
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
    toast->setObjectName("connectionToast");
    toast->setProperty("tone", success ? "success" : "danger");
    toast->setAttribute(Qt::WA_DeleteOnClose);
    toast->setAttribute(Qt::WA_ShowWithoutActivating);
    auto* layout = new QHBoxLayout(toast);
    layout->setContentsMargins(16, 12, 16, 12);
    auto* label = new QLabel(message);
    label->setWordWrap(true);
    label->setMaximumWidth(420);
    layout->addWidget(label);
    toast->adjustSize();
    const QPoint topRight = mapToGlobal(rect().topRight());
    toast->move(topRight.x() - toast->width() - 20, topRight.y() + 20);
    toast->show();
    QTimer::singleShot(2800, toast, &QDialog::close);
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
    statusBar()->setProperty("tone", success ? "normal" : "danger");
    statusBar()->style()->unpolish(statusBar());
    statusBar()->style()->polish(statusBar());
    statusBar()->showMessage(message, 5000);
}

void TeamSyncGui::applyTheme(const bool dark) {
    darkTheme_ = dark;
    qApp->setProperty("darkTheme", dark);
    qApp->setProperty("animationsEnabled", popupAnimations_);
    ThemeManager::apply(this, dark);
    for (auto* control : findChildren<QPushButton*>()) {
        const QString name = control->property("iconName").toString();
        if (!name.isEmpty()) control->setIcon(ThemeManager::icon(name, dark,
            control->objectName() == "navButton" && control->isChecked(), control->property("primary").toBool()));
    }
    for (auto* input : {loginUsername_, loginPassword_, globalSearch_}) {
        if (!input || input->actions().isEmpty()) continue;
        input->actions().first()->setIcon(ThemeManager::icon(input == loginPassword_ ? "lock" : input == loginUsername_ ? "user" : "search", dark));
    }
    if (auto* reveal = findChild<QAction*>("passwordVisibilityAction"))
        reveal->setIcon(ThemeManager::icon(loginPassword_->echoMode() == QLineEdit::Password ? "eye" : "eye-off", dark));
    if (profileChevron_) profileChevron_->setIcon(ThemeManager::icon("chevron-down", dark));
    if (navigationButtons_.size() > ProfilePage) navigationButtons_[ProfilePage]->update();
    if (navigationIndicator_) {
        auto palette = navigationIndicator_->palette();
        palette.setColor(QPalette::Window, ui::accentColor());
        navigationIndicator_->setPalette(palette);
    }
    for (auto* control : {authThemeButton_, shellThemeButton_}) {
        if (!control) continue;
        control->setText("");
        control->setIcon(ThemeManager::icon(dark ? "sun" : "moon", dark));
        control->setToolTip(dark ? "Switch to light theme" : "Switch to dark theme");
        control->setFixedSize(34, 34);
    }
    appSettings().setValue("appearance/darkTheme", dark);
}

void TeamSyncGui::toggleTheme() {
    const QPixmap previous = centralWidget()->grab();
    const bool dark = !darkTheme_;
    appSettings().setValue("appearance/theme", dark ? "dark" : "light");
    if (themeChoice_) {
        const QSignalBlocker blocker(themeChoice_);
        themeChoice_->setCurrentIndex(dark ? 2 : 1);
    }
    applyTheme(dark);
    if (currentUser_) refreshCurrentPage();
    ui::crossFade(centralWidget(), previous, 250);
    showStatus(dark ? "Dark theme" : "Light theme");
}

void TeamSyncGui::animateWindowIn() {
    setWindowOpacity(1.0);
}

void TeamSyncGui::animatePage(QWidget* page) {
    // Layouts own page geometry; moving stacked pages can leave blank content.
    if (page) page->update();
}

void TeamSyncGui::animatePopup(QDialog* dialog) {
    if (!dialog) return;
    dialog->setProperty("manualEntrance", true);
    dialog->setWindowOpacity(1.0);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
    if (!ui::motionAllowed()) return;
    auto* fade = new QPropertyAnimation(dialog, "windowOpacity", dialog);
    fade->setDuration(190);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void TeamSyncGui::navigateTo(const Page page) {
    if (!currentUser_ || page < 0 || page >= PageCount) return;
    const bool changing = contentStack_->currentIndex() != page;
    const QPixmap previous = changing && contentStack_->isVisible() ? contentStack_->grab() : QPixmap();
    static const QStringList titles = {
        "Overview", "Teams", "Projects", "Tasks", "Shared files",
        "Reports", "Activity", "Connection", "Tutorial & Credits", "My profile", "Settings"
    };
    static const QStringList subtitles = {
        "Your workspace at a glance", "People, roles, and team membership",
        "Manage your team's projects", "View and manage your tasks",
        "Local files synchronized with your team", "Readable summaries for decisions and reviews",
        "A transparent history of workspace actions", "Live LAN sync and connected computers",
        "Learn the workflow and meet the project team",
        "Your identity, skills, and security", "Appearance and sign-in preferences"
    };
    contentStack_->setCurrentIndex(page);
    pageTitle_->setText(titles[page]);
    pageSubtitle_->setText(subtitles[page]);
    const bool workPage = page == ProjectsPage || page == TasksPage;
    globalSearch_->setVisible(!workPage);
    pageHeaderAction_->setVisible(workPage);
    pageHeaderAction_->setText(page == TasksPage ? "New task" : "New project");
    for (int index = 0; index < static_cast<int>(navigationButtons_.size()); ++index) {
        auto* control = navigationButtons_[index];
        control->setChecked(index == page);
        const auto icon = control->property("iconName").toString();
        if (!icon.isEmpty()) control->setIcon(ThemeManager::icon(icon, darkTheme_, index == page));
    }
    if (navigationIndicator_) {
        navigationIndicator_->setVisible(page < 8);
        if (page < 8) {
            auto* selected = navigationButtons_[page];
            const QPoint position = selected->mapTo(navigationIndicator_->parentWidget(), QPoint(0, 0));
            const QRect target(position.x() + 1, position.y() + 8, 3, selected->height() - 16);
            if (navigationIndicator_->width() != 3 || !ui::motionAllowed()) navigationIndicator_->setGeometry(target);
            else {
                auto* slide = new QPropertyAnimation(navigationIndicator_, "geometry", navigationIndicator_);
                slide->setDuration(180); slide->setStartValue(navigationIndicator_->geometry()); slide->setEndValue(target);
                slide->setEasingCurve(QEasingCurve::OutCubic); slide->start(QAbstractAnimation::DeleteWhenStopped);
            }
            navigationIndicator_->raise();
        }
    }
    refreshCurrentPage();
    if (changing) ui::crossFade(contentStack_, previous, 200);
}

void TeamSyncGui::showGlobalSearch() {
    if (!currentUser_ || networkBusy_) return;
    const QString query = globalSearch_->text().trimmed();
    if (query.isEmpty()) return;
    QDialog dialog(this);
    dialog.setObjectName("globalSearchDialog");
    dialog.setWindowTitle("Search workspace");
    dialog.resize(760, 470);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 20);
    auto* heading = new QLabel("Results for “" + query + "”");
    heading->setTextFormat(Qt::PlainText);
    heading->setObjectName("sectionHeader");
    layout->addWidget(heading);
    auto* results = table({"Type", "Name", "Location"});
    results->setObjectName("globalSearchResults");
    results->setProperty("emptyText", "No matching items in your teams. Try another search.");
    results->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(results, 1);
    auto add = [&](const QString& type, const QString& name, const QString& location, int id) {
        const int row = results->rowCount(); results->insertRow(row);
        auto* value = new QTableWidgetItem(type);
        value->setData(Qt::UserRole, id);
        results->setItem(row, 0, value);
        results->setItem(row, 1, new QTableWidgetItem(name));
        results->setItem(row, 2, new QTableWidgetItem(location));
    };
    if (!perform([&] {
        for (const auto* task : service_->searchTasks(currentUserId(), s(query)))
            add("Task", q(task->title()), q(service_->project(task->projectId()).title()), task->id());
        for (const auto* project : service_->searchProjects(currentUserId(), s(query)))
            add("Project", q(project->title()), q(service_->team(project->teamId()).name()), project->id());
        for (const auto* team : service_->teamsForUser(currentUserId())) {
            if (q(team->name()).contains(query, Qt::CaseInsensitive)) add("Team", q(team->name()), "Workspace", team->id());
            for (const auto* file : service_->filesForTeam(currentUserId(), team->id(), s(query)))
                add("File", q(file->fileName()), q(team->name()), file->id());
        }
    })) return;
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Open | QDialogButtonBox::Close);
    layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Open)->setEnabled(results->rowCount() > 0);
    if (results->rowCount() > 0) results->selectRow(0);
    auto open = [&] {
        const int row = results->currentRow();
        if (row < 0) return;
        const QString kind = results->item(row, 0)->text();
        const int id = results->item(row, 0)->data(Qt::UserRole).toInt();
        QTableWidget* target = nullptr;
        if (kind == "Task") {
            if (auto* all = findChild<QPushButton*>("taskScope0")) all->click();
            taskSearch_->clear(); taskFilter_->setCurrentIndex(0); navigateTo(TasksPage); target = tasksTable_;
        } else if (kind == "Project") {
            if (auto* all = findChild<QPushButton*>("projectFilter0")) all->click();
            projectSearch_->clear(); navigateTo(ProjectsPage); target = projectsTable_;
        } else if (kind == "Team") { teamSearch_->clear(); navigateTo(TeamsPage); target = teamsTable_; }
        else if (kind == "File") {
            const auto& file = service_->sharedFile(id);
            navigateTo(FilesPage);
            filesTeam_->setCurrentIndex(filesTeam_->findData(file.teamId()));
            refreshFiles(); target = filesTable_;
        }
        if (target) for (int item = 0; item < target->rowCount(); ++item)
            if (target->item(item, 0)->data(Qt::UserRole).toInt() == id) target->selectRow(item);
        dialog.accept();
    };
    connect(buttons, &QDialogButtonBox::accepted, &dialog, open);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(results, &QTableWidget::itemDoubleClicked, &dialog, [&](QTableWidgetItem*) { open(); });
    dialog.exec();
}

QWidget* TeamSyncGui::buildDashboardPage() {
    auto* scroll = new QScrollArea;
    scroll->setObjectName("overviewPage");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(20, 4, 20, 12);
    layout->setSpacing(16);
    dashboardMetrics_ = new QWidget;
    auto* metrics = new QHBoxLayout(dashboardMetrics_);
    metrics->setContentsMargins(0, 0, 0, 0);
    metrics->setSpacing(12);
    const QStringList names = {"Active projects", "My tasks", "Completed tasks", "Team members"};
    const QStringList symbols = {"folder", "tasks", "check", "teams"};
    const QStringList tones = {"blue", "blue", "green", "purple"};
    for (int i = 0; i < 4; ++i) {
        auto* metric = card();
        metric->setObjectName("metricCard");
        metric->setMinimumHeight(116);
        auto* row = new QHBoxLayout(metric);
        row->setContentsMargins(16, 14, 12, 14);
        row->setSpacing(14);
        row->addWidget(new ui::IconTile(symbols[i], tones[i], 48));
        auto* text = new QVBoxLayout;
        text->setSpacing(4);
        auto* value = new QLabel("0");
        value->setObjectName("metricValue");
        auto* label = new QLabel(names[i]);
        label->setObjectName("metricLabel");
        label->setWordWrap(true);
        auto* trend = new QLabel;
        trend->setObjectName("metricTrend");
        trend->setProperty("tone", i == 1 ? "amber" : "green");
        text->addWidget(value);
        text->addWidget(label);
        text->addWidget(trend);
        dashboardValues_.push_back(value);
        dashboardTrends_.push_back(trend);
        row->addLayout(text, 1);
        metrics->addWidget(metric, 1);
    }
    layout->addWidget(dashboardMetrics_);
    dashboardEmpty_ = mutedLabel("");
    dashboardEmpty_->setObjectName("overviewEmptyState");
    layout->addWidget(dashboardEmpty_);
    dashboardEmpty_->hide();
    dashboardWork_ = new QWidget;
    auto* columns = new QHBoxLayout(dashboardWork_);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(16);
    auto* left = new QVBoxLayout;
    auto* right = new QVBoxLayout;
    left->setSpacing(16);
    right->setSpacing(16);
    auto makePanel = [&](const QString& title, Page destination, QVBoxLayout* parent) {
        auto* panel = card();
        panel->setObjectName("dashboardPanel");
        auto* contents = new QVBoxLayout(panel);
        contents->setContentsMargins(16, 14, 16, 14);
        contents->setSpacing(12);
        auto* heading = new QHBoxLayout;
        auto* name = new QLabel(title);
        name->setObjectName("sectionHeader");
        heading->addWidget(name, 1);
        auto* all = button("View all");
        all->setObjectName("panelLink");
        all->setProperty("link", true);
        heading->addWidget(all);
        connect(all, &QPushButton::clicked, this, [this, destination] { navigateTo(destination); });
        contents->addLayout(heading);
        parent->addWidget(panel);
        return contents;
    };
    auto* taskPanel = makePanel("My tasks", TasksPage, left);
    myTasksView_ = new QTableView;
    myTasksView_->setObjectName("overviewMyTasks");
    myTasksModel_ = new QStandardItemModel(0, 5, myTasksView_);
    myTasksModel_->setHorizontalHeaderLabels({"Task", "Project", "Priority", "Due date", "Status"});
    myTasksView_->setModel(myTasksModel_);
    myTasksView_->setProperty("emptyText", "No tasks assigned yet. Your team's work is in Tasks.");
    configureTable(myTasksView_);
    myTasksView_->verticalHeader()->setDefaultSectionSize(38);
    myTasksView_->setMinimumHeight(220);
    myTasksView_->setMaximumHeight(220);
    myTasksView_->horizontalHeader()->setStretchLastSection(false);
    myTasksView_->horizontalHeader()->setMinimumSectionSize(96);
    myTasksView_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    for (int col = 1; col < 5; ++col) myTasksView_->horizontalHeader()->setSectionResizeMode(col, QHeaderView::Fixed);
    myTasksView_->setColumnWidth(1, 106);
    myTasksView_->setColumnWidth(2, 84);
    myTasksView_->setColumnWidth(3, 106);
    myTasksView_->setColumnWidth(4, 108);
    taskPanel->addWidget(myTasksView_);
    connect(myTasksView_, &QTableView::doubleClicked, this, [this](const QModelIndex& index) {
        const int id = myTasksModel_->item(index.row(), 0)->data(Qt::UserRole).toInt();
        navigateTo(TasksPage);
        for (int row = 0; row < tasksTable_->rowCount(); ++row)
            if (tasksTable_->item(row, 0)->data(Qt::UserRole).toInt() == id) tasksTable_->selectRow(row);
    });
    auto* projects = makePanel("Active projects", ProjectsPage, left);
    projectRows_ = new QVBoxLayout;
    projectRows_->setContentsMargins(0, 0, 0, 0);
    projectRows_->setSpacing(0);
    projects->addLayout(projectRows_);
    auto* activity = makePanel("Recent activity", ActivityPage, right);
    activityRows_ = new QVBoxLayout;
    activityRows_->setSpacing(10);
    activityRows_->setContentsMargins(0, 0, 0, 0);
    activity->addLayout(activityRows_);
    auto* updates = makePanel("Team updates", TeamsPage, right);
    updateRows_ = new QVBoxLayout;
    updateRows_->setSpacing(10);
    updateRows_->setContentsMargins(0, 0, 0, 0);
    updates->addLayout(updateRows_);
    auto* updateLink = updates->itemAt(0)->layout();
    if (updateLink) {
        if (auto* action = qobject_cast<QPushButton*>(updateLink->itemAt(1)->widget())) {
            disconnect(action, nullptr, this, nullptr);
            connect(action, &QPushButton::clicked, this, [this] { showChatPanel(); });
        }
    }
    left->addStretch();
    right->addStretch();
    columns->addLayout(left, 64);
    columns->addLayout(right, 36);
    layout->addWidget(dashboardWork_);
    layout->addStretch();
    scroll->setWidget(page);
    return scroll;
}

QWidget* TeamSyncGui::buildTeamsPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
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
    teamsTable_->setObjectName("teamsTable");
    teamsTable_->setProperty("emptyText", "No teams yet. Create a team or join with a code.");
    teamsTable_->setColumnHidden(0, true);
    requireSelection(teamsTable_, {details, leave});
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



QWidget* TeamSyncGui::buildChatPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
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
    chatMessages_->setSpacing(2);
    chatMessages_->setWordWrap(true);
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
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
    auto* toolbar = new QHBoxLayout;
    toolbar->addWidget(new QLabel("Team"));
    filesTeam_ = new QComboBox;
    filesTeam_->setMinimumWidth(160);
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
    filesTable_->setObjectName("filesTable");
    filesTable_->setProperty("emptyText", "No shared files. Select a team, then choose Share local file.");
    filesTable_->setColumnHidden(0, true);
    requireSelection(filesTable_, {open, remove});
    layout->addWidget(filesTable_, 1);
    ui::acceptFileDrops(filesTable_->viewport(), [this](const QStringList& paths) { shareDroppedFiles(paths); });
    layout->addWidget(mutedLabel("Drop local files here or choose Share local file. Originals stay untouched; changed files transfer in resumable chunks (up to 2 GiB each)."));
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
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
    auto* top = new QHBoxLayout;
    top->addWidget(mutedLabel("Updates created by team, project, and task activity."));
    top->addStretch();
    auto* markRead = button("Mark all as read", true);
    top->addWidget(markRead);
    layout->addLayout(top);
    notificationsTable_ = table({"Status", "Notification", "Date / time"});
    notificationsTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    notificationsTable_->setProperty("emptyText", "You are all caught up. Team updates will appear here.");
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
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
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
    toolbar->setStretch(1, 1);
    toolbar->setStretch(3, 2);
    layout->addLayout(toolbar);
    auto* actions = new QHBoxLayout;
    actions->addWidget(generate);
    actions->addWidget(exportButton);
    actions->addStretch();
    layout->addLayout(actions);
    reportPreview_ = new QTextEdit;
    reportPreview_->setObjectName("reportPreview");
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
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
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
    activityTable_->setProperty("emptyText", "No activity for this team yet.");
    layout->addWidget(activityTable_, 1);
    connect(activityTeam_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refreshActivities(); });
    connect(refresh, &QPushButton::clicked, this, [this] { refreshActivities(); });
    return page;
}

QWidget* TeamSyncGui::buildConnectionPage() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);
    auto* actions = new QHBoxLayout;
    auto* host = button("Host this workspace", true);
    host->setObjectName("shellHostButton");
    auto* join = button("Join nearby workspace");
    join->setObjectName("shellJoinButton");
    auto* local = button("Use local workspace");
    local->setObjectName("shellLocalButton");
    iconFor(local, "monitor");
    actions->addWidget(host);
    actions->addWidget(join);
    actions->addWidget(local);
    actions->addStretch();
    layout->addLayout(actions);
    auto* summary = card();
    auto* details = new QVBoxLayout(summary);
    details->setContentsMargins(20, 16, 20, 16);
    details->setSpacing(8);
    connectionModeLabel_ = new QLabel;
    connectionModeLabel_->setObjectName("sectionTitle");
    connectionDetailLabel_ = mutedLabel("");
    connectionDetailLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    connectionPeersLabel_ = new QLabel;
    connectionPeersLabel_->setObjectName("statusBadge");
    connectionRevisionLabel_ = mutedLabel("");
    details->addWidget(connectionModeLabel_);
    details->addWidget(connectionDetailLabel_);
    details->addWidget(connectionPeersLabel_, 0, Qt::AlignLeft);
    details->addWidget(connectionRevisionLabel_);
    layout->addWidget(summary);
    auto* label = new QLabel("Connected computers");
    label->setObjectName("sectionTitle");
    layout->addWidget(label);
    connectionPeersList_ = new QListWidget;
    connectionPeersList_->setWordWrap(true);
    connectionPeersList_->setMinimumHeight(140);
    connectionPeersList_->setMaximumHeight(240);
    layout->addWidget(connectionPeersList_);
    auto* tools = new QHBoxLayout;
    auto* sync = button("Sync now");
    auto* copy = button("Copy connection details");
    auto* transfers = button("Transfers & sync");
    transfers->setObjectName("transferCenterButton");
    auto* recovery = button("Backups & recovery");
    recovery->setObjectName("connectionRecoveryButton");
    tools->addWidget(sync);
    tools->addWidget(copy);
    tools->addWidget(transfers);
    tools->addWidget(recovery);
    tools->addStretch();
    layout->addLayout(tools);
    layout->addWidget(mutedLabel("Keep TeamSync open on the host. Nearby discovery needs the same Wi-Fi or hotspot and permission on Windows Private networks. The workspace pairing code and team join code are separate."));
    layout->addStretch();
    connect(host, &QPushButton::clicked, this, [this] { startHostingDialog(); });
    connect(join, &QPushButton::clicked, this, [this] { showJoinWorkspaceDialog(); });
    connect(local, &QPushButton::clicked, this, [this] { switchToLocalWorkspace(); });
    connect(sync, &QPushButton::clicked, this, [this] {
        if (pullNetworkChanges(true)) { updateConnectionUi(); showConnectionToast("Workspace synchronized"); }
    });
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(lan_->statusText());
        showConnectionToast("Connection details copied");
    });
    connect(transfers, &QPushButton::clicked, this, [this] { showTransferCenter(); });
    connect(recovery, &QPushButton::clicked, this, [this] { showRecoveryDialog(); });
    updateConnectionUi();
    return page;
}

QWidget* TeamSyncGui::buildTutorialPage() {
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 20, 24, 20);
    outer->setSpacing(12);
    auto* tabs = new QTabWidget;
    tabs->setObjectName("tutorialTabs");
    auto* guide = new QWidget;
    auto* guideLayout = new QVBoxLayout(guide);
    guideLayout->setContentsMargins(20, 20, 20, 20);
    guideLayout->setSpacing(16);
    const std::vector<std::pair<QString, QString>> steps = {
        {"1. Choose how to start", "Sign in, create an account, or enter a name with Continue as guest. Remember me securely signs you in next time."},
        {"2. Connect your computers", "On the same Wi-Fi or hotspot, one person chooses Host workspace. Others use Find & join, select the computer, and enter its six-digit code. Connection controls are available before and after sign-in."},
        {"3. Join your team", "Workspace connection shares the data. Team membership gives access to work: create a team or join from Teams using its separate team code."},
        {"4. Plan and complete work", "Use the task Table or Board. Drag between Pending, In progress, and Completed, or use each card's menu/keyboard actions. Drop files onto a task to open completion with notes and attachments. Team membership and permissions still apply."},
        {"5. Stay up to date", "Use the chat button at the bottom right and the notification bell at the top. Shared files, messages, and task updates synchronize while connected."},
        {"6. Recover an account", "Forgot password uses the three answers set at signup. My profile lets account holders change their password and recovery answers."},
        {"7. Keep work safe", "Offline changes remain on this computer and retry when the host returns. Transfers & sync shows bytes, speed, Cancel and Retry. Conflicts never silently overwrite local edits: keep a recovery copy before accepting the host version."},
        {"8. Back up and restore", "Settings → Backups & recovery lists dated local backups and preserved conflict versions. Only an administrator in local mode can restore the entire workspace. A pre-restore backup is kept automatically."}
    };
    for (const auto& step : steps) {
        auto* title = new QLabel(step.first);
        title->setObjectName("sectionTitle");
        guideLayout->addWidget(title);
        guideLayout->addWidget(mutedLabel(step.second));
    }
    guideLayout->addStretch();
    auto* guideScroll = new QScrollArea;
    guideScroll->setWidgetResizable(true);
    guideScroll->setFrameShape(QFrame::NoFrame);
    guideScroll->setWidget(guide);
    tabs->addTab(guideScroll, "Quick start");
    auto* credits = new QWidget;
    auto* creditLayout = new QVBoxLayout(credits);
    creditLayout->setContentsMargins(24, 24, 24, 24);
    creditLayout->setSpacing(16);
    auto* title = new QLabel("Made for better teamwork");
    title->setObjectName("welcomeTitle");
    creditLayout->addWidget(title);
    auto* names = new QLabel(
        "<b>Developers</b><br><br>"
        "Sudhan Bhattarai<br>"
        "<a href=\"https://www.sudhanb.com.np\">Portfolio</a> · "
        "<a href=\"https://github.com/SudhanTheDev\">GitHub</a><br><br>"
        "Severoos Nepali<br>"
        "<a href=\"https://www.severoos-nepali.com.np/\">Portfolio</a> · "
        "<a href=\"https://github.com/Severoos-ui\">GitHub</a><br><br>"
        "<b>Project support</b><br>Senior Sudip Khanal<br><br>"
        "<b>Academic guidance</b><br>Ojan Adhikari<br><br>"
        "<b>Built with</b><br>C++17 · Qt 6 Widgets and Qt Network<br><br>");
    names->setWordWrap(true);
    names->setTextInteractionFlags(Qt::TextBrowserInteraction);
    names->setOpenExternalLinks(true);
    creditLayout->addWidget(names);
    creditLayout->addStretch();
    auto* creditScroll = new QScrollArea;
    creditScroll->setWidgetResizable(true);
    creditScroll->setFrameShape(QFrame::NoFrame);
    creditScroll->setWidget(credits);
    tabs->addTab(creditScroll, "Credits");
    outer->addWidget(tabs);
    return page;
}

QWidget* TeamSyncGui::buildProfilePage() {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(24, 20, 24, 20);
    auto* panel = card();
    panel->setMaximumWidth(780);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);
    auto* title = new QLabel("Profile information");
    title->setObjectName("sectionTitle");
    layout->addWidget(title);
    layout->addWidget(mutedLabel("Your name appears in team activity. Skills help with task recommendations."));
    auto* form = new QFormLayout;
    form->setSpacing(12);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    profileName_ = new QLineEdit;
    profileName_->setObjectName("profileName");
    profileUsername_ = new QLineEdit;
    profileUsername_->setReadOnly(true);
    profileEmail_ = new QLineEdit;
    profileSkills_ = new QLineEdit;
    profileSkills_->setPlaceholderText("C++, research, documentation");
    profileRole_ = mutedLabel("");
    form->addRow("Full name", profileName_);
    form->addRow("Username", profileUsername_);
    form->addRow("Email", profileEmail_);
    form->addRow("Skills", profileSkills_);
    form->addRow("Account", profileRole_);
    layout->addLayout(form);
    auto* save = button("Save profile", true);
    save->setObjectName("saveProfileButton");
    layout->addWidget(save, 0, Qt::AlignLeft);
    auto* separator = new QFrame;
    separator->setFrameShape(QFrame::HLine);
    layout->addWidget(separator);
    auto* securityTitle = new QLabel("Account security");
    securityTitle->setObjectName("sectionTitle");
    layout->addWidget(securityTitle);
    auto* securityActions = new QHBoxLayout;
    auto* password = button("Change password");
    auto* security = button("Configure security questions");
    securityActions->addWidget(password);
    securityActions->addWidget(security);
    securityActions->addStretch();
    layout->addLayout(securityActions);
    outer->addWidget(panel);
    outer->addStretch();
    connect(save, &QPushButton::clicked, this, [this] { saveProfile(); });
    connect(password, &QPushButton::clicked, this, [this] { showChangePasswordDialog(); });
    connect(security, &QPushButton::clicked, this, [this] { showSecurityQuestionsDialog(); });
    scroll->setWidget(page);
    return scroll;
}

QWidget* TeamSyncGui::buildSettingsPage() {
    auto* page = new QWidget;
    page->setObjectName("settingsPage");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(16);
    auto* appearance = card();
    appearance->setMaximumWidth(780);
    auto* options = new QVBoxLayout(appearance);
    options->setContentsMargins(20, 20, 20, 20);
    options->setSpacing(12);
    auto* title = new QLabel("Appearance");
    title->setObjectName("sectionTitle");
    options->addWidget(title);
    auto* form = new QFormLayout;
    themeChoice_ = new QComboBox;
    themeChoice_->setObjectName("themeChoice");
    themeChoice_->addItems({"Use system setting", "Light", "Dark"});
    const QString saved = appSettings().value("appearance/theme", "system").toString();
    themeChoice_->setCurrentIndex(saved == "dark" ? 2 : saved == "light" ? 1 : 0);
    form->addRow("Color theme", themeChoice_);
    options->addLayout(form);
    auto* motion = new QCheckBox("Use subtle popup animations");
    motion->setObjectName("popupAnimations");
    motion->setChecked(popupAnimations_);
    options->addWidget(motion);
    options->addWidget(mutedLabel("Preferences apply to every page and dialog, and are saved on this computer."));
    connect(themeChoice_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](const int index) {
        const QString mode = index == 0 ? "system" : index == 1 ? "light" : "dark";
        appSettings().setValue("appearance/theme", mode);
        applyTheme(index == 0 ? ThemeManager::systemPrefersDark() : index == 2);
    });
    connect(motion, &QCheckBox::toggled, this, [this](const bool enabled) {
        popupAnimations_ = enabled;
        qApp->setProperty("animationsEnabled", enabled);
        appSettings().setValue("appearance/popupAnimations", enabled);
    });
    layout->addWidget(appearance);
    auto* account = card();
    account->setMaximumWidth(780);
    auto* security = new QVBoxLayout(account);
    security->setContentsMargins(20, 20, 20, 20);
    security->setSpacing(12);
    auto* accountTitle = new QLabel("Saved sign-in");
    accountTitle->setObjectName("sectionTitle");
    security->addWidget(accountTitle);
    security->addWidget(mutedLabel("Remember me uses Windows account encryption. Sign out or clear the saved sign-in when sharing a computer. Guest sessions are not remembered."));
    auto* forget = button("Clear saved sign-in");
    forget->setObjectName("clearSavedLoginButton");
    security->addWidget(forget, 0, Qt::AlignLeft);
    connect(forget, &QPushButton::clicked, this, [this] {
        clearRememberedLogin();
        rememberLogin_->setChecked(false);
        showStatus("Saved sign-in cleared. Your current session is still open.");
    });
    layout->addWidget(account);
    auto* recovery = card();
    recovery->setMaximumWidth(780);
    auto* recoveryLayout = new QVBoxLayout(recovery);
    recoveryLayout->setContentsMargins(20, 20, 20, 20);
    auto* recoveryTitle = new QLabel("Workspace safety");
    recoveryTitle->setObjectName("sectionTitle");
    recoveryLayout->addWidget(recoveryTitle);
    recoveryLayout->addWidget(mutedLabel("Dated local backups and preserved conflict copies protect your work. Restore is available to workspace administrators in local mode."));
    auto* recoveryOpen = button("Backups & recovery");
    recoveryOpen->setObjectName("settingsRecoveryButton");
    recoveryLayout->addWidget(recoveryOpen, 0, Qt::AlignLeft);
    connect(recoveryOpen, &QPushButton::clicked, this, [this] { showRecoveryDialog(); });
    layout->addWidget(recovery);
    auto* shortcuts = mutedLabel("Keyboard shortcuts: Ctrl+1–8 switches workspace pages. Ctrl+R refreshes. Ctrl+, opens Settings. F1 opens Tutorial & Credits.");
    shortcuts->setMaximumWidth(780);
    layout->addWidget(shortcuts);
    layout->addStretch();
    return page;
}

void TeamSyncGui::attemptLogin() {
    if (loginUsername_->text().trimmed().isEmpty() || loginPassword_->text().isEmpty()) {
        for (auto* input : {loginUsername_, loginPassword_}) {
            input->setProperty("validationError", input->text().trimmed().isEmpty());
            input->style()->unpolish(input);
            input->style()->polish(input);
        }
        QMessageBox::information(this, "Sign in", "Enter both your username and password.");
        (loginUsername_->text().trimmed().isEmpty() ? loginUsername_ : loginPassword_)->setFocus();
        return;
    }
    guestSession_ = false;
    if (!perform([this] {
            currentUser_ = &service_->login(s(loginUsername_->text().trimmed()), s(loginPassword_->text()));
        })) return;

    const bool remember = rememberLogin_ && rememberLogin_->isChecked();
    if (!remember) clearRememberedLogin();
    sessionPassword_ = loginPassword_->text();
    finishLogin(remember);
}

void TeamSyncGui::finishLogin(const bool remember) {
    if (!currentUser_) return;
    if (remember && !guestSession_) {
        rememberLogin(q(currentUser_->username()), loginPassword_->text());
    }

    refreshSessionUi(false);
    loginPassword_->clear();
    navigateTo(DashboardPage);
    animatePage(shellPage_);
    showStatus("Signed in successfully");
}

void TeamSyncGui::refreshSessionUi(const bool refreshPage) {
    if (!currentUser_) return;
    signedInUser_->setText(signedInUser_->fontMetrics().elidedText(q(currentUser_->fullName()), Qt::ElideRight, 166) + "\n" +
        signedInUser_->fontMetrics().elidedText(guestSession_ ? "Guest session" : q(currentUser_->username()), Qt::ElideRight, 166));
    signedInUser_->setToolTip(q(currentUser_->fullName()) + " · " + q(toString(currentUser_->role())));
    if (navigationButtons_.size() > ProfilePage) {
        if (auto* profile = dynamic_cast<ui::ProfileButton*>(navigationButtons_[ProfilePage]))
            profile->setProfile(q(currentUser_->fullName()), guestSession_ ? "Guest session" :
                currentUser_->role() == UserRole::Admin ? "Administrator" : "Team member");
    }
    rootStack_->setCurrentWidget(shellPage_);
    refreshTeamSelectors();
    updateNotificationButton();
    if (refreshPage) refreshCurrentPage();
}

void TeamSyncGui::rememberLogin(const QString& username, const QString& password) {
    if (password.isEmpty()) return;
    const QByteArray encrypted = protectSavedPassword(password.toUtf8());
    if (encrypted.isEmpty()) return;
    auto settings = appSettings();
    settings.setValue("session/username", username);
    settings.setValue("session/protectedPassword", QString::fromLatin1(encrypted.toBase64()));
    settings.setValue("session/workspace", QString::fromStdWString(activeRoot_.wstring()));
}

void TeamSyncGui::clearRememberedLogin() {
    auto settings = appSettings();
    settings.remove("session/username");
    settings.remove("session/protectedPassword");
    settings.remove("session/workspace");
}

void TeamSyncGui::attemptRememberedLogin() {
    auto settings = appSettings();
    const QString username = settings.value("session/username").toString();
    const QByteArray encrypted = QByteArray::fromBase64(settings.value("session/protectedPassword").toString().toLatin1());
    const QString savedWorkspace = settings.value("session/workspace").toString();
    if (username.isEmpty() || encrypted.isEmpty()) return;
    if (savedWorkspace != QString::fromStdWString(activeRoot_.wstring())) return;

    const QByteArray password = unprotectSavedPassword(encrypted);
    if (password.isEmpty()) {
        clearRememberedLogin();
        return;
    }
    try {
        guestSession_ = false;
        lan_->executeLocalMutation([&] {
            currentUser_ = &service_->login(s(username), std::string(password.constData(), static_cast<std::size_t>(password.size())));
        });
        if (lan_->mode() != LanSyncManager::Mode::Local) lan_->noteLocalChange();
        sessionPassword_ = QString::fromUtf8(password);
        finishLogin(false);
        showStatus("Signed in securely");
    } catch (const std::exception&) {
        clearRememberedLogin();
        loginUsername_->setText(username);
    }
}

void TeamSyncGui::showGuestDialog() {
    bool accepted = false;
    const QString name = QInputDialog::getText(this, "Continue as guest", "Your display name:",
                                               QLineEdit::Normal, {}, &accepted).trimmed();
    if (!accepted || name.isEmpty()) return;

    const QString unique = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString username = "guest_" + unique.left(12);
    const QString password = "guest-" + unique;
    const std::vector<std::string> answers = {
        s("guest-a-" + unique), s("guest-b-" + unique), s("guest-c-" + unique)
    };
    guestSession_ = true;
    if (!perform([&] {
            currentUser_ = &service_->registerUser(s(name), s(username), s(password), s(password), "",
                                                  UserRole::TeamMember, {}, answers);
        }, "Guest session started")) {
        guestSession_ = false;
        return;
    }
    clearRememberedLogin();
    sessionPassword_ = password;
    finishLogin(false);
    showConnectionToast("Welcome, " + name + " — you are using guest mode");
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
    auto* accountHint = mutedLabel("Enter your username to unlock its recovery questions.");
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
    layout->addWidget(accountHint);
    layout->addWidget(mutedLabel(
        "Answers ignore capitalization and spaces at the beginning or end. Existing accounts can configure them from My profile."));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("Reset password");
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(buttons);
    const auto updateRecoveryAvailability = [this, username, accountHint, &securityAnswers,
                                             replacement, confirmation, buttons] {
        const bool knownAccount = service_->findUserByUsername(s(username->text())) != nullptr;
        for (auto* answer : securityAnswers) answer->setEnabled(knownAccount);
        replacement->setEnabled(knownAccount);
        confirmation->setEnabled(knownAccount);
        buttons->button(QDialogButtonBox::Ok)->setEnabled(knownAccount);
        accountHint->setText(knownAccount
            ? "Account found. Answer its three personal recovery questions."
            : "No account was found yet. Check the username or create an account.");
    };
    connect(username, &QLineEdit::textChanged, &dialog, updateRecoveryAvailability);
    updateRecoveryAvailability();
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (!service_->findUserByUsername(s(username->text()))) {
            QMessageBox::warning(&dialog, "TeamSync", "No account exists with that username.");
            updateRecoveryAvailability();
            return;
        }
        std::vector<std::string> answerValues;
        for (auto* answer : securityAnswers) answerValues.push_back(s(answer->text()));
        if (perform([&] {
                service_->resetPassword(s(username->text()), answerValues,
                                        s(replacement->text()), s(confirmation->text()));
            }, "Password reset successfully")) {
            clearRememberedLogin();
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
    dialog.resize(760, 620);
    dialog.setMinimumSize(560, 460);
    auto* layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(0, 0, 0, 12);
    layout->addWidget(buildTutorialPage(), 1);
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close);
    layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::accept);
    dialog.exec();
}

void TeamSyncGui::logout() {
    if (!currentUser_) return;
    if (QMessageBox::question(this, "Sign out", "Sign out of TeamSync?") != QMessageBox::Yes) return;
    perform([this] { service_->logout(currentUserId()); });
    clearRememberedLogin();
    if (chatDialog_) chatDialog_->hide();
    if (notificationsDialog_) notificationsDialog_->hide();
    currentUser_ = nullptr;
    guestSession_ = false;
    sessionPassword_.clear();
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
    if (!currentUser_) return;
    updateNotificationButton();
    perform([this] {
        const int userId = currentUserId();
        const auto teams = service_->teamsForUser(userId);
        const auto projects = service_->projectsForUser(userId);
        auto assigned = service_->tasksForUser(userId);
        const auto visibleTasks = service_->searchTasks(userId, "");
        std::set<int> members;
        for (const auto* team : teams) members.insert(team->memberIds().begin(), team->memberIds().end());
        int active = 0, mine = 0, completed = 0, soon = 0, completedWeek = 0;
        for (const auto* project : projects)
            if (project->status() != ProjectStatus::Archived && project->status() != ProjectStatus::Completed) ++active;
        for (const auto* task : assigned) {
            if (task->status() == TaskStatus::Completed) continue;
            ++mine;
            const int days = QDate::currentDate().daysTo(toQDate(task->dueDate()));
            if (days >= 0 && days <= 7) ++soon;
        }
        for (const auto* task : visibleTasks) if (task->status() == TaskStatus::Completed) {
            ++completed;
            if (task->completionDate()) {
                const int days = toQDate(*task->completionDate()).daysTo(QDate::currentDate());
                if (days >= 0 && days < 7) ++completedWeek;
            }
        }
        const std::array<int, 4> values = {active, mine, completed, static_cast<int>(members.size())};
        for (std::size_t i = 0; i < values.size(); ++i) dashboardValues_[i]->setText(QString::number(values[i]));
        dashboardTrends_[0]->setText(QString("%1 team%2").arg(teams.size()).arg(teams.size() == 1 ? "" : "s"));
        dashboardTrends_[1]->setText(soon > 0 ? QString("%1 due this week").arg(soon) : "No tasks due soon");
        dashboardTrends_[2]->setText(QString("%1 this week").arg(completedWeek));
        dashboardTrends_[3]->setText(guestSession_ ? "Guest workspace" : "Across your teams");
        const bool empty = teams.empty();
        dashboardEmpty_->setVisible(empty);
        dashboardEmpty_->setText("Your workspace is ready. Create a team or join with its team code to start collaborating.");
        if (empty) {
            auto* owner = dashboardEmpty_->parentWidget();
            if (!owner->findChild<QPushButton*>("overviewCreateTeam")) {
                auto* row = new QWidget;
                row->setObjectName("overviewOnboardingActions");
                auto* actions = new QHBoxLayout(row);
                actions->setContentsMargins(0, 0, 0, 0);
                auto* create = button("Create team", true);
                create->setObjectName("overviewCreateTeam");
                auto* join = button("Join a team");
                actions->addWidget(create);
                actions->addWidget(join);
                actions->addStretch();
                connect(create, &QPushButton::clicked, this, [this] { showCreateTeamDialog(); refreshDashboard(); });
                connect(join, &QPushButton::clicked, this, [this] { joinTeam(); refreshDashboard(); });
                qobject_cast<QVBoxLayout*>(owner->layout())->insertWidget(2, row);
            }
        }
        if (auto* actions = dashboardEmpty_->parentWidget()->findChild<QWidget*>("overviewOnboardingActions")) actions->setVisible(empty);
        myTasksModel_->removeRows(0, myTasksModel_->rowCount());
        std::stable_sort(assigned.begin(), assigned.end(), [](const Task* a, const Task* b) {
            if ((a->status() == TaskStatus::Completed) != (b->status() == TaskStatus::Completed))
                return a->status() != TaskStatus::Completed;
            return a->dueDate() < b->dueDate();
        });
        for (const auto* task : assigned) {
            if (myTasksModel_->rowCount() >= 5) break;
            auto* title = new QStandardItem(q(task->title()));
            title->setData(task->id(), Qt::UserRole);
            title->setToolTip(q(task->description()));
            auto* priority = new QStandardItem(q(toString(task->priority())));
            priority->setData(static_cast<int>(ui::CellKind::Priority), ui::KindRole);
            auto* status = new QStandardItem(q(toString(task->status())));
            status->setData(static_cast<int>(ui::CellKind::Status), ui::KindRole);
            myTasksModel_->appendRow({title, new QStandardItem(q(service_->project(task->projectId()).title())),
                priority, new QStandardItem(toQDate(task->dueDate()).toString("MMM d, yyyy")), status});
        }
        auto clearRows = [](QVBoxLayout* rows) {
            while (auto* item = rows->takeAt(0)) { if (auto* widget = item->widget()) delete widget; delete item; }
        };
        clearRows(projectRows_);
        clearRows(activityRows_);
        clearRows(updateRows_);
        int projectCount = 0;
        for (const auto* project : projects) {
            if (project->status() == ProjectStatus::Archived || project->status() == ProjectStatus::Completed || projectCount >= 3) continue;
            ++projectCount;
            int total = 0, done = 0;
            for (int id : project->taskIds()) {
                try { const auto& task = service_->task(id); ++total; if (task.status() == TaskStatus::Completed) ++done; }
                catch (const std::exception&) {}
            }
            const int percent = total == 0 ? 0 : qRound(100.0 * done / total);
            auto* row = new QWidget;
            auto* content = new QHBoxLayout(row);
            content->setContentsMargins(0, 12, 0, 12);
            content->setSpacing(8);
            content->addWidget(new ui::IconTile("folder", projectCount == 2 ? "amber" : "blue", 30));
            auto* text = new QVBoxLayout;
            text->setSpacing(4);
            auto* title = new QLabel(q(project->title()));
            title->setObjectName("projectRowTitle");
            title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            title->setToolTip(q(project->title()));
            auto* description = mutedLabel(q(project->description()));
            description->setObjectName("metadata");
            description->setWordWrap(false);
            description->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            description->setToolTip(q(project->description()));
            text->addWidget(title);
            text->addWidget(description);
            content->addLayout(text, 1);
            auto* progress = new QProgressBar;
            progress->setProperty("compactProgress", true);
            progress->setRange(0, 100);
            progress->setValue(0);
            ui::animateProgress(progress, percent);
            progress->setTextVisible(false);
            progress->setFixedSize(64, 8);
            progress->setToolTip(QString("%1 of %2 tasks completed").arg(done).arg(total));
            content->addWidget(progress);
            auto* amount = mutedLabel(QString::number(percent) + "%");
            amount->setWordWrap(false);
            amount->setObjectName("metadata");
            amount->setFixedWidth(30);
            content->addWidget(amount);
            QStringList names;
            for (int member : service_->team(project->teamId()).memberIds()) names << q(service_->user(member).fullName());
            auto* avatars = new ui::AvatarStack;
            avatars->setFixedWidth(64);
            avatars->setNames(names);
            content->addWidget(avatars);
            auto* taskCount = mutedLabel(QString("%1 tasks").arg(total));
            taskCount->setWordWrap(false);
            taskCount->setObjectName("metadata");
            taskCount->setFixedWidth(44);
            content->addWidget(taskCount);
            auto* more = button("⋮");
            more->setObjectName("rowContextButton");
            more->setFixedSize(24, 28);
            more->setAccessibleName("Project actions");
            auto* menu = new QMenu(more);
            const int id = project->id();
            auto select = [this, id] {
                navigateTo(ProjectsPage);
                for (int row = 0; row < projectsTable_->rowCount(); ++row)
                    if (projectsTable_->item(row, 0)->data(Qt::UserRole).toInt() == id) projectsTable_->selectRow(row);
            };
            menu->addAction("Open project", this, [select] { select(); });
            menu->addAction("Edit project", this, [this, select] { select(); showProjectDialog(true); });
            menu->addAction("Archive project", this, [this, select] { select(); archiveSelectedProject(); });
            more->setMenu(menu);
            content->addWidget(more);
            projectRows_->addWidget(row);
        }
        if (projectCount == 0) {
            auto* empty = mutedLabel("No active projects yet. Team admins can create one from Projects.");
            empty->setMinimumHeight(88);
            projectRows_->addWidget(empty);
        }
        auto relative = [](const std::string& value) {
            const auto date = QDateTime::fromString(q(value), "yyyy-MM-dd HH:mm:ss");
            if (!date.isValid()) return q(value);
            const qint64 seconds = date.secsTo(QDateTime::currentDateTime());
            if (seconds < 60) return QString("Just now");
            if (seconds < 3600) return QString("%1 min ago").arg(seconds / 60);
            if (seconds < 86400) return QString("%1 hr ago").arg(seconds / 3600);
            return date.toString("MMM d");
        };
        const auto activities = service_->activitiesForUser(userId);
        int activityCount = 0;
        for (auto it = activities.rbegin(); it != activities.rend() && activityCount < 4; ++it) {
            const auto* activity = *it;
            if (activity->teamId() == 0 && activity->action() == "Login") continue;
            ++activityCount;
            auto* row = new QWidget;
            auto* content = new QHBoxLayout(row);
            content->setContentsMargins(0, 3, 0, 3);
            content->setSpacing(12);
            const auto action = q(activity->action());
            const QString symbol = action.contains("file", Qt::CaseInsensitive) ? "files" :
                action.contains("task", Qt::CaseInsensitive) ? "tasks" :
                action.contains("team", Qt::CaseInsensitive) ? "teams" : "folder";
            const QString tone = symbol == "files" ? "red" : symbol == "tasks" ? "green" : "blue";
            content->addWidget(new ui::IconTile(symbol, tone, 34), 0, Qt::AlignTop);
            auto* text = new QVBoxLayout;
            text->setSpacing(4);
            auto* description = new QLabel(action);
            description->setWordWrap(true);
            description->setObjectName("activityTitle");
            description->setToolTip(action);
            auto* metadata = mutedLabel((activity->teamId() > 0 ? q(service_->team(activity->teamId()).name()) : "TeamSync") +
                                        " · " + relative(activity->dateTime()));
            metadata->setObjectName("metadata");
            text->addWidget(description);
            text->addWidget(metadata);
            content->addLayout(text, 1);
            activityRows_->addWidget(row);
        }
        if (activityCount == 0) activityRows_->addWidget(mutedLabel("Team activity will appear here as you work."));
        int updateCount = 0;
        for (auto it = service_->messages().rbegin(); it != service_->messages().rend() && updateCount < 3; ++it) {
            try {
                if (!service_->team(it->teamId()).hasMember(userId)) continue;
                const QString name = q(service_->user(it->senderId()).fullName());
                ++updateCount;
                auto* row = new QWidget;
                auto* content = new QHBoxLayout(row);
                content->setContentsMargins(0, 3, 0, 3);
                content->setSpacing(12);
                auto* avatar = new QLabel;
                avatar->setPixmap(ui::avatarIcon(name, 32).pixmap(32, 32));
                content->addWidget(avatar, 0, Qt::AlignTop);
                auto* text = new QVBoxLayout;
                text->setSpacing(4);
                auto* top = new QHBoxLayout;
                auto* sender = new QLabel(name);
                sender->setObjectName("activityTitle");
                top->addWidget(sender, 1);
                auto* time = mutedLabel(relative(it->dateTime()));
                time->setObjectName("metadata");
                time->setWordWrap(false);
                top->addWidget(time);
                text->addLayout(top);
                auto* message = mutedLabel(q(it->text()).left(170));
                message->setWordWrap(true);
                message->setToolTip(q(it->text()));
                text->addWidget(message);
                content->addLayout(text, 1);
                updateRows_->addWidget(row);
            } catch (const std::exception&) {}
        }
        if (updateCount == 0) updateRows_->addWidget(mutedLabel("No team messages yet. Open chat to start a conversation."));
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
            filesTable_->setItem(row, 5, new QTableWidgetItem(QLocale().formattedDataSize(static_cast<qint64>(value->recordedSize()))));
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
                    auto font = notificationsTable_->item(row, column)->font();
                    font.setWeight(QFont::DemiBold);
                    notificationsTable_->item(row, column)->setFont(font);
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
    notificationButton_->setText({});
    if (auto* dot = notificationButton_->findChild<QLabel*>("notificationDot")) dot->setVisible(unread > 0);
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
        const QString codeToCopy = q(selected.joinCode());
        connect(copyCode, &QPushButton::clicked, &dialog, [this, codeToCopy] {
            QApplication::clipboard()->setText(codeToCopy);
            showStatus("Join code copied");
        });
        if (selected.isAdmin(currentUserId())) {
            auto* promote = button("Promote member");
            auto* demote = button("Change to member");
            auto* remove = button("Remove member");
            remove->setObjectName("dangerButton");
            actions->addWidget(promote);
            actions->addWidget(demote);
            actions->addWidget(remove);
            connect(promote, &QPushButton::clicked, &dialog, [&] {
                const int memberId = selectedId(members);
                if (memberId > 0 && perform([&] { service_->promoteMember(currentUserId(), teamId, memberId); },
                                              "Member promoted")) dialog.accept();
            });
            connect(demote, &QPushButton::clicked, &dialog, [&] {
                const int memberId = selectedId(members);
                if (memberId <= 0) return;
                if (QMessageBox::question(&dialog, "Change team role",
                        "Change the selected team admin back to a regular member?") == QMessageBox::Yes &&
                    perform([&] { service_->demoteMember(currentUserId(), teamId, memberId); },
                            "Member role updated")) dialog.accept();
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
    files->setObjectName("completionFiles");
    layout->addWidget(files, 1);
    auto* choose = button("Add files...");
    layout->addWidget(choose, 0, Qt::AlignLeft);
    QStringList selectedFiles = pendingTaskFiles_;
    pendingTaskFiles_.clear();
    const auto updateFiles = [&] {
        files->clear();
        if (selectedFiles.empty()) files->addItem("Drop files here or choose Add files.");
        else for (const auto& path : selectedFiles) files->addItem(path);
    };
    updateFiles();
    ui::acceptFileDrops(files->viewport(), [&](const QStringList& paths) {
        for (const auto& path : paths) if (!selectedFiles.contains(path)) selectedFiles << path;
        updateFiles();
    });
    auto* removeFile = button("Remove selected file");
    layout->addWidget(removeFile, 0, Qt::AlignLeft);
    connect(removeFile, &QPushButton::clicked, &dialog, [&] {
        if (files->currentRow() >= 0 && files->currentRow() < selectedFiles.size()) selectedFiles.removeAt(files->currentRow());
        updateFiles();
    });
    connect(choose, &QPushButton::clicked, &dialog, [&] {
        const auto picked = QFileDialog::getOpenFileNames(&dialog, "Choose completion files");
        for (const auto& path : picked) if (!selectedFiles.contains(path)) selectedFiles.push_back(path);
        updateFiles();
    });
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok);
    buttons->button(QDialogButtonBox::Ok)->setText("Mark completed");
    buttons->button(QDialogButtonBox::Ok)->setProperty("primary", true);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        std::vector<std::filesystem::path> paths;
        for (const auto& path : selectedFiles) paths.emplace_back(path.toStdWString());
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
                service_->shareFile(currentUserId(), teamId, std::filesystem::path(path.toStdWString()),
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
        refreshSessionUi(false);
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
            }, "Password changed")) {
            sessionPassword_ = replacement->text();
            auto settings = appSettings();
            if (!guestSession_ && settings.value("session/username").toString() == q(currentUser_->username())) {
                rememberLogin(q(currentUser_->username()), replacement->text());
            }
            dialog.accept();
        }
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
