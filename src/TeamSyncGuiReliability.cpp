#include "TeamSyncGui.h"
#include "FileDropSupport.h"
#include "LanSyncManager.h"
#include "PremiumWidgets.h"
#include "WorkspaceRecovery.h"

#include <QApplication>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace teamsync {
namespace {
QString text(const std::string& value) { return QString::fromUtf8(value.c_str()); }
QString bytes(std::uint64_t value) { return QLocale().formattedDataSize(static_cast<qint64>(value)); }
QLabel* explanation(const QString& value) { auto* label = new QLabel(value); label->setWordWrap(true); return label; }
}

void TeamSyncGui::showTransferCenter() {
    QDialog dialog(this);
    dialog.setObjectName("transferCenterDialog");
    dialog.setWindowTitle("Transfers & synchronization");
    dialog.resize(620, 410);
    auto* layout = new QVBoxLayout(&dialog);
    auto* status = explanation(""); status->setObjectName("transferStatus");
    auto* file = explanation(""); file->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* progress = new QProgressBar; progress->setObjectName("transferByteProgress"); progress->setRange(0, 1000);
    auto* detail = explanation("");
    layout->addWidget(status); layout->addWidget(file); layout->addWidget(progress); layout->addWidget(detail);
    layout->addWidget(explanation("Only changed files are transferred, in verified resumable chunks. Cancel keeps completed chunks for Retry. Offline edits stay on this computer until synchronization succeeds."));
    auto* actions = new QHBoxLayout;
    auto* retry = new ui::AnimatedButton("Retry / synchronize now"); retry->setObjectName("retryTransferButton"); retry->setProperty("primary", true);
    auto* cancel = new ui::AnimatedButton("Cancel transfer"); cancel->setObjectName("cancelTransferButton");
    auto* recovery = new ui::AnimatedButton("Recovery history");
    actions->addWidget(retry); actions->addWidget(cancel); actions->addWidget(recovery);
    layout->addLayout(actions);
    auto* hostVersion = new ui::AnimatedButton("Keep a recovery copy of my edits, then use the host version");
    hostVersion->setObjectName("resolveWithHostButton");
    layout->addWidget(hostVersion);
    layout->addWidget(explanation("For a conflict, neither side is silently overwritten. You can keep working locally, or preserve your local version in recovery history and explicitly accept the host version."));
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(close);
    connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const auto refresh = [&] {
        const auto value = lan_->progress();
        const bool busy = networkBusy_ || value.active;
        status->setText(value.active ? text(value.phase) : networkOnline_ ? "Ready" : "Synchronization paused");
        file->setText(value.file.empty() ? "No file is transferring right now." : text(value.file));
        progress->setRange(0, value.active && !value.total ? 0 : 1000);
        progress->setValue(value.total ? static_cast<int>(1000.0 * value.transferred / value.total) : value.active ? 0 : 1000);
        QString information = bytes(value.transferred) + " / " + bytes(value.total);
        if (value.bytesPerSecond > 0) information += " · " + bytes(static_cast<std::uint64_t>(value.bytesPerSecond)) + "/s";
        if (!busy) {
            if (lan_->hasPendingChanges()) information += "\nLocal changes are waiting to synchronize.";
            if (!lastNetworkError_.isEmpty()) information += "\n" + lastNetworkError_;
        }
        detail->setText(information);
        cancel->setEnabled(busy);
        retry->setEnabled(!busy && lan_->mode() == LanSyncManager::Mode::Client);
        hostVersion->setEnabled(!busy && lan_->mode() == LanSyncManager::Mode::Client && lan_->hasPendingChanges());
        recovery->setEnabled(!busy);
    };
    connect(cancel, &QPushButton::clicked, &dialog, [this] { lan_->cancelTransfer(); });
    connect(retry, &QPushButton::clicked, &dialog, [&] { pullNetworkChanges(true); refresh(); });
    connect(recovery, &QPushButton::clicked, &dialog, [this] { showRecoveryDialog(); });
    connect(hostVersion, &QPushButton::clicked, &dialog, [&] {
        if (QMessageBox::question(&dialog, "Resolve synchronization conflict",
            "Preserve your current local workspace in recovery history, then replace this client's working copy with the host's version? The host will not be changed.") != QMessageBox::Yes) return;
        bool changed = false; QString problem;
        if (!runNetworkOperation([&] { return lan_->adoptHostAfterPreservingLocal(changed, problem); }, "Preserving local edits and receiving host version…")) {
            lastNetworkError_ = problem; QMessageBox::warning(&dialog, "Conflict remains unresolved", problem);
        } else {
            networkOnline_ = true; lastNetworkError_.clear(); reloadNetworkData();
            showStatus("Host version received. Your previous local version remains in recovery history.");
        }
        refresh();
    });
    QTimer timer; timer.setInterval(200); connect(&timer, &QTimer::timeout, &dialog, refresh); timer.start();
    refresh(); dialog.exec();
}

void TeamSyncGui::showRecoveryDialog() {
    if (!currentUser_ || networkBusy_) return;
    QDialog dialog(this); dialog.setObjectName("recoveryHistoryDialog"); dialog.setWindowTitle("Backups & recovery"); dialog.resize(760, 500);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(explanation("Backups are local to this computer and include accounts, projects, messages, shared files, and task attachments. Daily backups retain the latest 14 automatic versions; manual and conflict copies are retained separately."));
    auto* category = new QComboBox; category->addItems({"Workspace backups", "Preserved conflict versions"}); layout->addWidget(category);
    auto* table = new QTableWidget(0, 4); table->setObjectName("recoveryHistoryTable");
    table->setHorizontalHeaderLabels({"Created", "Reason", "Files", "Size"});
    table->setSelectionBehavior(QAbstractItemView::SelectRows); table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->verticalHeader()->hide(); table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    layout->addWidget(table, 1);
    auto* actions = new QHBoxLayout;
    auto* create = new ui::AnimatedButton("Create backup"); create->setObjectName("createBackupButton"); create->setProperty("primary", true);
    auto* restore = new ui::AnimatedButton("Restore selected version"); restore->setObjectName("restoreBackupButton");
    auto* folder = new ui::AnimatedButton("Open recovery folder");
    actions->addWidget(create); actions->addWidget(restore); actions->addWidget(folder); layout->addLayout(actions);
    const bool canRestore = !guestSession_ && currentUser_->role() == UserRole::Admin && lan_->mode() == LanSyncManager::Mode::Local;
    restore->setToolTip(canRestore ? "A pre-restore backup is made automatically. You must sign in again after restoring." : "Only a workspace administrator in local mode can restore the whole workspace.");
    layout->addWidget(explanation("Restoring changes the whole working copy, so it is disabled while hosting or joined. For a joined conflict, use Transfers & sync → keep a recovery copy and use the host version. You can open recovery files to recover individual notes/files manually."));
    auto* close = new QDialogButtonBox(QDialogButtonBox::Close); layout->addWidget(close); connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    const auto refresh = [&] {
        try {
            WorkspaceRecovery recovery(activeRoot_);
            const auto records = category->currentIndex() ? recovery.listConflicts() : recovery.listBackups();
            table->setRowCount(0);
            for (const auto& item : records) {
                const int row = table->rowCount(); table->insertRow(row);
                auto* created = new QTableWidgetItem(text(item.createdAt)); created->setData(Qt::UserRole, text(item.id));
                table->setItem(row, 0, created); table->setItem(row, 1, new QTableWidgetItem(text(item.reason)));
                table->setItem(row, 2, new QTableWidgetItem(QString::number(item.fileCount))); table->setItem(row, 3, new QTableWidgetItem(bytes(item.bytes)));
            }
            restore->setEnabled(canRestore && table->rowCount() > 0);
        } catch (const std::exception& error) { QMessageBox::warning(&dialog, "Recovery history", text(error.what())); }
    };
    connect(category, qOverload<int>(&QComboBox::currentIndexChanged), &dialog, [&](int) { refresh(); });
    connect(create, &QPushButton::clicked, &dialog, [&] {
        bool accepted = false;
        const auto reason = QInputDialog::getText(&dialog, "Create backup", "Label (optional)", QLineEdit::Normal, {}, &accepted);
        if (!accepted) return;
        try {
            lan_->executeLocalMutation([&] { WorkspaceRecovery(activeRoot_).createBackup(reason.isEmpty() ? "Manual backup" : reason.toStdString()); });
            refresh(); showStatus("Backup created on this computer.");
        } catch (const std::exception& error) { QMessageBox::warning(&dialog, "Backup did not finish", text(error.what())); }
    });
    connect(folder, &QPushButton::clicked, &dialog, [&] {
        const auto path = WorkspaceRecovery(activeRoot_).recoveryDirectory();
        if (!std::filesystem::exists(path)) { QMessageBox::information(&dialog, "No recovery folder yet", "Create a backup first."); return; }
        QDesktopServices::openUrl(QUrl::fromLocalFile(QString::fromStdWString(path.wstring())));
    });
    connect(restore, &QPushButton::clicked, &dialog, [&] {
        const int row = table->currentRow(); if (!canRestore || row < 0) return;
        const auto id = table->item(row, 0)->data(Qt::UserRole).toString().toStdString();
        if (QMessageBox::warning(&dialog, "Restore workspace",
            "Replace this local workspace with the selected version? A pre-restore backup will be kept. You will need to sign in again.", QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
        try {
            lan_->executeLocalMutation([&] {
                WorkspaceRecovery recovery(activeRoot_);
                if (category->currentIndex()) recovery.restoreConflict(id); else recovery.restoreBackup(id);
            });
            currentUser_ = nullptr; sessionPassword_.clear(); guestSession_ = false; clearRememberedLogin();
            service_->reload(); rootStack_->setCurrentWidget(authPage_);
            if (chatDialog_) chatDialog_->hide(); if (notificationsDialog_) notificationsDialog_->hide();
            dialog.accept(); showStatus("Workspace restored. Sign in again to continue.");
        } catch (const std::exception& error) { QMessageBox::critical(&dialog, "Restore could not finish", text(error.what())); }
    });
    refresh(); dialog.exec();
}

void TeamSyncGui::shareDroppedFiles(const QStringList& paths) {
    if (!currentUser_ || paths.empty() || networkBusy_) return;
    const int team = filesTeam_->currentData().toInt();
    if (team <= 0) { QMessageBox::information(this, "Choose a team", "Choose a team before dropping files."); return; }
    QDialog dialog(this); dialog.setObjectName("dropSharedFilesDialog"); dialog.setWindowTitle("Share dropped files"); dialog.resize(600, 370);
    auto* layout = new QVBoxLayout(&dialog);
    layout->addWidget(explanation("These files will be copied into the selected team's workspace. Originals stay untouched. Files can be up to 2 GiB each."));
    auto* list = new QListWidget; list->addItems(paths); layout->addWidget(list, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok); buttons->button(QDialogButtonBox::Ok)->setText("Share files"); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    perform([&] { for (const auto& path : paths) service_->shareFile(currentUserId(), team,
        std::filesystem::path(path.toStdWString()), QFileInfo(path).fileName().toStdString(), ""); }, "Files copied and ready to synchronize");
    refreshFiles();
}
} // namespace teamsync
