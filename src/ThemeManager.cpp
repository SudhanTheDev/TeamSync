#include "ThemeManager.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QHash>
#include <QLibrary>
#include <QPainter>
#include <QPalette>
#include <QStyleHints>
#include <QSvgRenderer>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace teamsync {
namespace {

struct ThemeColors {
    QString background, surface, secondary, text, muted, border;
    QString accent, hover, selection, selectionText, disabled;
    QString danger, dangerSurface, success, successSurface, warning, warningSurface;
};

ThemeColors colors(const bool dark) {
    if (dark) {
        return {"#0F151D", "#192029", "#222D3C", "#EDF3FC", "#96A7BD", "#2B3748",
                "#63ADFF", "#8AC2FF", "#18457F", "#D0E6FF", "#63748D",
                "#F49C9F", "#402B36", "#64D6A3", "#153D34", "#F0BA64", "#443725"};
    }
    return {"#F4F8FC", "#FEFEFE", "#F0F5FD", "#091335", "#71819E", "#E3EBF7",
            "#087BFF", "#0065D9", "#D8E8FC", "#0763D2", "#91A0B6",
            "#E45059", "#FFF0F1", "#169563", "#E1F7EC", "#C9861D", "#FFF2D7"};
}

QIcon renderSvg(const QByteArray& source, const QColor& normal, const QColor& disabled) {
    QIcon result;
    for (const auto mode : {QIcon::Normal, QIcon::Disabled}) {
        QByteArray svg = source;
        svg.replace("currentColor", (mode == QIcon::Disabled ? disabled : normal).name().toUtf8());
        QSvgRenderer renderer(svg);
        if (!renderer.isValid()) return {};
        for (const int size : {16, 20, 24, 32, 48, 64}) {
            for (const int scale : {1, 2}) {
                QPixmap pixmap(size * scale, size * scale);
                pixmap.fill(Qt::transparent);
                QPainter painter(&pixmap);
                painter.setRenderHint(QPainter::Antialiasing);
                renderer.render(&painter);
                painter.end();
                pixmap.setDevicePixelRatio(scale);
                result.addPixmap(pixmap, mode);
            }
        }
    }
    return result;
}

} // namespace

bool ThemeManager::systemPrefersDark() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    return QApplication::palette().color(QPalette::Window).lightness() < 128;
#endif
}

QIcon ThemeManager::icon(const QString& name, const bool dark, const bool accent, const bool onAccent) {
    static QHash<QString, QIcon> cache;
    const QString key = name + (dark ? "/dark" : "/light") + (accent ? "/accent" : "/normal") + (onAccent ? "/on-accent" : "");
    const auto found = cache.constFind(key);
    if (found != cache.cend()) return found.value();
    QFile file(":/icons/" + name + ".svg");
    if (!file.open(QIODevice::ReadOnly)) return {};
    const auto theme = colors(dark);
    const QIcon result = renderSvg(file.readAll(), QColor(onAccent ? "#FFFFFF" : (accent ? theme.accent : theme.muted)), QColor(theme.disabled));
    cache.insert(key, result);
    return result;
}

QIcon ThemeManager::applicationIcon() {
    return QIcon(":/teamsync/app.svg");
}

void ThemeManager::applyNativeTitleBar(QWidget* window, const bool dark) {
#ifdef Q_OS_WIN
    if (!window || !window->isWindow()) return;
    using SetWindowAttribute = HRESULT (WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static QLibrary dwm("dwmapi");
    static auto setAttribute = reinterpret_cast<SetWindowAttribute>(dwm.resolve("DwmSetWindowAttribute"));
    if (!setAttribute) return;
    const BOOL enabled = dark ? TRUE : FALSE;
    const HWND handle = reinterpret_cast<HWND>(window->winId());
    // Attribute 20 is supported by current Windows; 19 covers earlier Windows 10.
    if (FAILED(setAttribute(handle, 20, &enabled, sizeof(enabled))))
        setAttribute(handle, 19, &enabled, sizeof(enabled));
#else
    Q_UNUSED(window);
    Q_UNUSED(dark);
#endif
}

void ThemeManager::apply(QWidget* window, const bool dark) {
    const auto theme = colors(dark);
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(theme.background));
    palette.setColor(QPalette::WindowText, QColor(theme.text));
    palette.setColor(QPalette::Base, QColor(theme.surface));
    palette.setColor(QPalette::AlternateBase, QColor(theme.secondary));
    palette.setColor(QPalette::Text, QColor(theme.text));
    palette.setColor(QPalette::Button, QColor(theme.surface));
    palette.setColor(QPalette::ButtonText, QColor(theme.text));
    palette.setColor(QPalette::Highlight, QColor(theme.selection));
    palette.setColor(QPalette::HighlightedText, QColor(theme.selectionText));
    palette.setColor(QPalette::ToolTipBase, QColor(theme.surface));
    palette.setColor(QPalette::ToolTipText, QColor(theme.text));
    palette.setColor(QPalette::Link, QColor(theme.accent));
    palette.setColor(QPalette::LinkVisited, QColor(theme.accent));
    palette.setColor(QPalette::PlaceholderText, QColor(theme.muted));
    palette.setColor(QPalette::Mid, QColor(theme.border));
    palette.setColor(QPalette::Light, QColor(theme.secondary));
    palette.setColor(QPalette::Dark, QColor(theme.border));
    for (const auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, QColor(theme.disabled));
    }
    QApplication::setPalette(palette);

    QFont font;
    const auto families = QFontDatabase::families();
    if (families.contains("Segoe UI Variable Text")) font.setFamily("Segoe UI Variable Text");
    else if (families.contains("Segoe UI Variable")) font.setFamily("Segoe UI Variable");
    else if (families.contains("Segoe UI")) font.setFamily("Segoe UI");
    else if (families.contains("Noto Sans")) font.setFamily("Noto Sans");
    else font.setFamily(QApplication::font().family());
    font.setPixelSize(13);
    QApplication::setFont(font);

    QString style = R"QSS(
        QWidget { color:@TEXT@; font-family:"@FONT@"; font-size:13px; }
        QMainWindow, #rootStack, #authPage, #shellPage, #pageArea, #contentStack { background:@BG@; }
        QDialog, QMessageBox { background:@SURFACE@; }
        QLabel { background:transparent; }
        QLabel#pageTitle { font-size:26px; font-weight:650; }
        QLabel#authHeading, QLabel#welcomeTitle, QLabel#dialogHeroTitle { font-size:26px; font-weight:650; }
        QLabel#sectionTitle, QLabel#sectionHeader { font-size:16px; font-weight:650; }
        QLabel#mutedLabel, QLabel#pageSubtitle, QLabel#metricLabel, QLabel#metricAccent, QLabel#privacyLabel, QLabel#metadata, QLabel#authConnectionStatus { color:@MUTED@; }
        QLabel#pageSubtitle, QLabel#metadata, QLabel#privacyLabel { font-size:12px; }
        QLabel#navSection { color:@TEXT@; font-size:10px; font-weight:500; padding:8px 8px 4px 8px; }
        QLabel#windowBrand, QLabel#sidebarLogo { font-size:20px; font-weight:650; }
        QLabel#loginWordmark { font-size:38px; font-weight:700; letter-spacing:-1px; }
        QLabel#loginTagline { font-size:15px; color:@MUTED@; }
        QLabel#featureTitle { font-size:15px; font-weight:600; }
        QLabel#featureBody { font-size:13px; color:@MUTED@; }
        QLabel#featureDisc { border:none; border-radius:25px; background:@SELECT@; }
        QLabel#metricValue { font-size:24px; font-weight:650; }
        QLabel#metricLabel { font-size:12px; }
        QLabel#metricTrend { font-size:11px; color:@SUCCESS@; }
        QLabel#emptyState, QLabel#overviewEmptyState { color:@MUTED@; padding:12px; }
        QLabel#errorLabel, QLabel[validationError="true"] { color:@DANGER@; }
        QTextEdit#reportPreview { font-family:"Cascadia Mono","Consolas",monospace; font-size:12px; }
        QLabel#connectionBadge, QLabel#statusBadge { color:@MUTED@; background:@SECONDARY@; border:1px solid @BORDER@; border-radius:5px; padding:4px 8px; font-size:12px; }
        QLabel#statusBadge[tone="success"], QLabel#connectionBadge[tone="success"] { color:@SUCCESS@; background:@SUCCESSBG@; border-color:@SUCCESSBG@; }
        QLabel#statusBadge[tone="warning"], QLabel#connectionBadge[tone="warning"] { color:@WARNING@; background:@WARNINGBG@; border-color:@WARNINGBG@; }
        QLabel#statusBadge[tone="danger"], QLabel#connectionBadge[tone="danger"] { color:@DANGER@; background:@DANGERBG@; border-color:@DANGERBG@; }
        QPushButton#connectionBadge { color:@MUTED@; background:@SECONDARY@; border:1px solid @BORDER@; border-radius:6px; padding:4px 8px; font-size:12px; }
        QPushButton#connectionBadge:hover { color:@TEXT@; border-color:@MUTED@; }
        QPushButton#connectionBadge:focus { border-color:@ACCENT@; }
        QFrame#card, QFrame#metricCard, QFrame#dashboardPanel, QFrame#compactMetric, QFrame#tablePanel { background:@SURFACE@; border:1px solid @BORDER@; border-radius:10px; }
        QLabel#activityTitle, QLabel#projectRowTitle { font-size:13px; font-weight:600; }
        QLabel#metricTrend[tone="green"] { color:@SUCCESS@; font-size:11px; }
        QLabel#metricTrend[tone="amber"] { color:@WARNING@; font-size:11px; }
        QLabel#notificationDot { background:#f22f43; border:1px solid @SURFACE@; border-radius:4px; }
        QTableView#overviewMyTasks { border:none; background:@SURFACE@; border-radius:0; }
        QToolButton#profileChevron { border:none; background:transparent; padding:4px; }
        QProgressBar[compactProgress="true"] { min-height:6px; max-height:8px; border:none; border-radius:4px; }
        QFrame#authCard { background:@SURFACE@; border:1px solid @BORDER@; border-radius:12px; }
        QFrame#authDivider { border:none; background:@BORDER@; min-height:1px; max-height:1px; }
        QFrame#connectionPanel { background:@SECONDARY@; border:1px solid @SECONDARY@; border-radius:10px; }
        QFrame#sidebar { background:@SIDEBAR@; border-right:1px solid @BORDER@; }
        #sidebarFooter { border-top:1px solid @BORDER@; }
        QLabel#sidebarUser { border:none; color:@MUTED@; padding:6px 4px; font-size:12px; }
        #pageHeader { background:transparent; border:none; }
        #pageToolbar { background:transparent; }
        #materialInset { background:@SECONDARY@; border-radius:8px; padding:12px; }
        #tutorialStep { background:transparent; padding:8px 0; }
        #connectionPanelTitle { font-weight:600; }
        QLineEdit, QTextEdit, QPlainTextEdit, QComboBox, QAbstractSpinBox {
            background:@SURFACE@; color:@TEXT@; border:1px solid @BORDER@; border-radius:7px;
            padding:7px 10px; min-height:18px; selection-background-color:@SELECT@; selection-color:@SELECTTEXT@;
        }
        QLineEdit:hover, QComboBox:hover, QAbstractSpinBox:hover { border-color:@INPUTHOVER@; }
        QLineEdit QToolButton { padding:0; min-height:0; min-width:0; border:none; background:transparent; }
        QLineEdit#loginUsername, QLineEdit#loginPassword { min-height:22px; padding:8px 10px; }
        QPushButton#loginButton { min-height:24px; padding:8px 12px; }
        QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QComboBox:focus, QAbstractSpinBox:focus { border:1px solid @ACCENT@; background:@INPUTFOCUS@; }
        QLineEdit[validationError="true"] { border-color:@DANGER@; background:@DANGERBG@; }
        QLineEdit:disabled, QComboBox:disabled, QAbstractSpinBox:disabled { background:@SECONDARY@; color:@DISABLED@; }
        QLineEdit[readOnly="true"] { background:@SECONDARY@; }
        QComboBox { padding-right:24px; }
        QComboBox::drop-down { width:24px; border:none; }
        QComboBox QAbstractItemView { background:@SURFACE@; border:1px solid @BORDER@; selection-background-color:@SELECT@; selection-color:@SELECTTEXT@; outline:none; padding:4px; }
        QLineEdit#referenceSearch { background:@SURFACE@; border-radius:8px; padding:7px 10px; min-height:18px; }
        QPushButton, QToolButton { background:@SURFACE@; color:@TEXT@; border:1px solid @BORDER@; border-radius:7px; padding:7px 12px; min-height:18px; }
        QPushButton:hover, QToolButton:hover { background:@SECONDARY@; border-color:@INPUTHOVER@; }
        QPushButton:pressed, QToolButton:pressed { background:@SELECT@; }
        QPushButton:focus, QToolButton:focus { border:1px solid @ACCENT@; }
        QPushButton:disabled, QToolButton:disabled { color:@DISABLED@; background:@SECONDARY@; border-color:@BORDER@; }
        QPushButton[primary="true"] { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #087BFF,stop:1 #005CC8); color:#FFFFFF; border-color:#137CEE; font-weight:600; }
        QPushButton[primary="true"]:hover { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #3597FF,stop:1 #086DDD); border-color:#218AFF; }
        QPushButton[primary="true"]:pressed { background:#005CC8; }
        QPushButton[primary="true"]:focus { border:1px solid @FOCUSBORDER@; }
        QPushButton[primary="true"]:disabled { background:@SECONDARY@; color:@DISABLED@; border-color:@BORDER@; }
        QPushButton#tonalButton { background:@SECONDARY@; }
        QPushButton#linkButton, QPushButton#panelLink, QPushButton[link="true"] { background:transparent; color:@ACCENT@; border:1px solid transparent; padding:2px 0; }
        QPushButton#linkButton:hover, QPushButton#panelLink:hover, QPushButton[link="true"]:hover { color:@HOVER@; text-decoration:underline; }
        QPushButton#linkButton:focus, QPushButton[link="true"]:focus { border-color:@ACCENT@; }
        QPushButton#linkButton:disabled, QPushButton#panelLink:disabled, QPushButton[link="true"]:disabled { color:@DISABLED@; text-decoration:none; }
        QPushButton#navButton { text-align:left; background:transparent; border:1px solid transparent; border-left:3px solid transparent; border-radius:6px; padding:6px 10px; min-height:18px; }
        QPushButton#navButton:hover { background:@SECONDARY@; }
        QPushButton#navButton:checked { background:@SELECT@; border-left:3px solid @ACCENT@; color:@SELECTTEXT@; font-weight:600; }
        QPushButton#navButton:focus { border:1px solid @ACCENT@; border-left:3px solid @ACCENT@; }
        QPushButton#dangerButton, QPushButton#logoutButton { color:@DANGER@; }
        QPushButton#dangerButton:hover, QPushButton#logoutButton:hover { background:@DANGERBG@; border-color:@DANGER@; }
        QPushButton#themeButton, QPushButton#notificationButton { padding:5px 8px; }
        QPushButton#profileMenu, QPushButton[profileMenu="true"] { text-align:left; border:none; background:transparent; padding:8px 6px; min-height:34px; }
        QPushButton#profileMenu:hover, QPushButton[profileMenu="true"]:hover { background:@SECONDARY@; }
        QPushButton[filterTab="true"] { background:@SURFACE@; border-color:@BORDER@; color:@MUTED@; padding:6px 12px; border-radius:6px; }
        QPushButton[filterTab="true"]:checked { background:@SELECT@; border-color:@SELECT@; color:@SELECTTEXT@; }
        QPushButton[filterTab="true"]:hover { background:@SECONDARY@; color:@TEXT@; }
        QPushButton#localConnectionButton, QPushButton#hostConnectionButton, QPushButton#joinConnectionButton { border-radius:8px; padding:7px 12px; }
        QPushButton#localConnectionButton:checked, QPushButton#hostConnectionButton:checked, QPushButton#joinConnectionButton:checked { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #087BFF,stop:1 #005CC8); border-color:#137CEE; color:#FFFFFF; }
        QPushButton[connectionChoice="true"] { border-radius:8px; padding:7px 12px; }
        QPushButton[connectionChoice="true"]:checked { background:qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #087BFF,stop:1 #005CC8); border-color:#137CEE; color:#FFFFFF; }
        QPushButton[iconOnly="true"], QToolButton[iconOnly="true"] { padding:4px; min-height:18px; }
        QPushButton#rowContextButton, QPushButton[rowContextButton="true"] { padding:0; min-height:16px; border:1px solid transparent; background:transparent; }
        QPushButton#rowContextButton:hover, QPushButton[rowContextButton="true"]:hover { border-color:@BORDER@; background:@SECONDARY@; }
        QPushButton#rowContextButton::menu-indicator, QPushButton[rowContextButton="true"]::menu-indicator, QPushButton[profileMenu="true"]::menu-indicator { image:none; width:0; }
        QPushButton#notificationButton[hasUnread="true"] { color:@ACCENT@; border-color:@ACCENT@; background:@SELECT@; }
        QPushButton#floatingChatButton { background:@SURFACE@; border-color:@BORDER@; border-radius:8px; padding:6px 10px; }
        QCheckBox, QRadioButton { spacing:7px; background:transparent; padding:3px 0; }
        QCheckBox:disabled, QRadioButton:disabled { color:@DISABLED@; }
        QCheckBox::indicator { width:14px; height:14px; border:1px solid @MUTED@; border-radius:3px; background:@SURFACE@; }
        QCheckBox::indicator:checked { background:#087BFF; border-color:#087BFF; image:url(:/teamsync/check-white.svg); }
        QCheckBox::indicator:disabled { background:@SECONDARY@; border-color:@BORDER@; }
        QCheckBox::indicator:focus { border-color:@ACCENT@; }
        QTableView::indicator { width:14px; height:14px; border:1px solid @MUTED@; border-radius:3px; background:@SURFACE@; }
        QTableView::indicator:checked { background:#087BFF; border-color:#087BFF; image:url(:/teamsync/check-white.svg); }
        QTableView, QTableWidget, QListView, QListWidget, QTreeView, QTreeWidget {
            background:@SURFACE@; alternate-background-color:@SURFACE@; color:@TEXT@;
            border:1px solid @BORDER@; border-radius:9px; outline:none; gridline-color:@BORDER@;
            selection-background-color:@SELECT@; selection-color:@SELECTTEXT@;
        }
        QFrame#tablePanel QTableView { border:none; border-radius:0; }
        QTableView::item, QTableWidget::item { padding:6px 8px; border-bottom:1px solid @BORDER@; }
        QTableView:focus, QListView:focus, QTreeView:focus { border-color:@ACCENT@; }
        QTableView::item:selected, QTableWidget::item:selected { background:@SELECT@; color:@SELECTTEXT@; }
        QTableView::item:hover, QTableWidget::item:hover { background:@ROWHOVER@; }
        QHeaderView { background:@HEADERBG@; }
        QHeaderView::section { background:@HEADERBG@; color:@MUTED@; border:none; border-bottom:1px solid @BORDER@; padding:9px 10px; font-size:12px; font-weight:500; }
        QTableCornerButton::section { background:@SECONDARY@; border:none; border-bottom:1px solid @BORDER@; }
        QListView::item, QListWidget::item { padding:9px 10px; border-bottom:1px solid @BORDER@; }
        QListView::item:selected, QListWidget::item:selected { background:@SELECT@; color:@SELECTTEXT@; }
        QListView::item:hover, QListWidget::item:hover { background:@SECONDARY@; }
        QTreeView::item { padding:4px; }
        QGroupBox { border:1px solid @BORDER@; border-radius:9px; margin-top:14px; padding:14px; font-weight:600; }
        QGroupBox::title { subcontrol-origin:margin; left:12px; padding:0 4px; color:@TEXT@; }
        QProgressBar { border:none; background:@SECONDARY@; border-radius:4px; min-height:8px; text-align:center; color:@TEXT@; }
        QProgressBar::chunk { background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #137FFF,stop:1 #5FAEFF); border-radius:4px; }
        QTabWidget::pane { border:1px solid @BORDER@; background:@SURFACE@; border-radius:9px; }
        QTabBar::tab { background:@SURFACE@; color:@MUTED@; border:1px solid @BORDER@; border-radius:6px; padding:7px 14px; margin-right:6px; }
        QTabBar::tab:selected { background:@SELECT@; color:@SELECTTEXT@; border-color:@SELECT@; }
        QTabBar::tab:focus { border-color:@ACCENT@; }
        QScrollArea, QScrollArea > QWidget > QWidget { background:transparent; border:none; }
        QScrollBar:vertical { background:transparent; width:12px; margin:2px; }
        QScrollBar::handle:vertical { background:@BORDER@; border-radius:4px; min-height:28px; }
        QScrollBar:horizontal { background:transparent; height:12px; margin:2px; }
        QScrollBar::handle:horizontal { background:@BORDER@; border-radius:4px; min-width:28px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
        QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0; }
        QScrollBar::add-page, QScrollBar::sub-page { background:transparent; }
        QSplitter::handle { background:@BORDER@; }
        QStatusBar { background:@BG@; border-top:1px solid @BORDER@; color:@MUTED@; font-size:11px; }
        QStatusBar[statusError="true"], QStatusBar[tone="danger"] { color:@DANGER@; }
        QStatusBar::item { border:none; }
        QDialogButtonBox QPushButton { min-width:80px; }
        QMenuBar { background:@SURFACE@; color:@TEXT@; }
        QMenuBar::item:selected { background:@SELECT@; }
        QMenu { background:@SURFACE@; color:@TEXT@; border:1px solid @BORDER@; padding:4px; }
        QMenu::item { padding:6px 24px 6px 12px; border-radius:3px; }
        QMenu::item:selected, QMenu::item:focus { background:@SELECT@; color:@SELECTTEXT@; }
        QMenu::item:disabled { color:@DISABLED@; }
        QMenu::separator { background:@BORDER@; height:1px; margin:4px 8px; }
        QToolTip { background:@SURFACE@; color:@TEXT@; border:1px solid @BORDER@; border-radius:4px; padding:5px 7px; }
        QDialog#connectionToast { background:@SURFACE@; border:1px solid @BORDER@; border-radius:10px; }
        QDialog#connectionToast[tone="success"] { border-left:3px solid @SUCCESS@; }
        QDialog#connectionToast[tone="danger"] { border-left:3px solid @DANGER@; }
    )QSS";
    const QHash<QString, QString> replacements{
        {"@FONT@", font.family()}, {"@SIDEBAR@", dark ? "#151B24" : "#F0F5FA"},
        {"@INPUTHOVER@", dark ? "#435A78" : "#BCD5F5"},
        {"@INPUTFOCUS@", dark ? "#1C293B" : "#FBFDFF"},
        {"@HEADERBG@", dark ? "#202B3A" : "#F2F7FD"},
        {"@ROWHOVER@", dark ? "#223148" : "#F5F9FF"},
        {"@FOCUSBORDER@", dark ? "#B4D9FF" : "#004C9E"},
        {"@BG@", theme.background}, {"@SURFACE@", theme.surface}, {"@SECONDARY@", theme.secondary},
        {"@TEXT@", theme.text}, {"@MUTED@", theme.muted}, {"@BORDER@", theme.border},
        {"@ACCENT@", theme.accent}, {"@HOVER@", theme.hover}, {"@SELECT@", theme.selection},
        {"@SELECTTEXT@", theme.selectionText}, {"@DISABLED@", theme.disabled},
        {"@DANGER@", theme.danger}, {"@DANGERBG@", theme.dangerSurface},
        {"@SUCCESS@", theme.success}, {"@SUCCESSBG@", theme.successSurface},
        {"@WARNING@", theme.warning}, {"@WARNINGBG@", theme.warningSurface}
    };
    for (auto iterator = replacements.cbegin(); iterator != replacements.cend(); ++iterator) {
        style.replace(iterator.key(), iterator.value());
    }
    // Application scope also styles child dialogs and standard context menus.
    if (window) window->setStyleSheet({});
    qApp->setStyleSheet(style);
    applyNativeTitleBar(window, dark);
}

} // namespace teamsync
