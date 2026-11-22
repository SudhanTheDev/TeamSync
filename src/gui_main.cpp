#include "TeamSyncGui.h"
#include "LanSyncManager.h"
#include "AppSettings.h"
#include "TaskBoardWidget.h"
#include "WorkspaceRecovery.h"

#include <QApplication>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontInfo>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QMenu>
#include <QPalette>
#include <QPushButton>
#include <QProgressBar>
#include <QSpinBox>
#include <QSaveFile>
#include <QStackedWidget>
#include <QTableWidget>
#include <QStyleFactory>
#include <QSettings>
#include <QString>
#include <QTextEdit>
#include <QTimer>
#include <QUrl>

#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <algorithm>
#include <string>

namespace {

QString normalized(QString text) { return text.remove('&').simplified().toLower(); }

QPushButton* pageButton(QWidget& window, QString name) {
    const auto canonical = [](QString label) {
        label = normalized(label);
        if (label == "files") return QString("shared files");
        if (label == "my profile") return QString("profile");
        if (label == "help" || label == "tutorial" || label == "tutorial credits" || label == "help tutorial") return QString("help");
        return label;
    };
    name = canonical(name);
    for (auto* button : window.findChildren<QPushButton*>()) {
        const QString label = button->property("pageName").toString().isEmpty()
            ? button->text() : button->property("pageName").toString();
        if (button->isCheckable() && canonical(label) == name) return button;
    }
    return nullptr;
}

void activatePageButton(QPushButton* button) {
    if (!button) return;
    const QString page = normalized(button->property("pageName").toString());
    if (button->menu() && !page.isEmpty()) {
        for (auto* action : button->menu()->actions()) {
            if (normalized(action->text()) == page) { action->trigger(); return; }
        }
    }
    button->click();
}

bool signedIn(QWidget& window) {
    auto* stack = window.findChild<QStackedWidget*>("rootStack");
    return stack && stack->currentWidget() && stack->currentWidget()->objectName() == "shellPage";
}

void seedReviewWorkspace(const std::filesystem::path& root) {
    using namespace teamsync;
    TeamSyncService service(root);
    if (!service.users().empty()) throw std::runtime_error("UI review seeding requires an empty data directory.");
    const int admin = service.registerUser("UI Review Admin", "ui-review", "review-pass", "review-pass", "",
        UserRole::Admin, {"C++", "Documentation"}, {"School", "Nickname", "Town"}).id();
    const int member = service.registerUser("UI Review Member", "ui-member", "member-pass", "member-pass", "",
        UserRole::TeamMember, {"Research"}, {"School", "Nickname", "Town"}).id();
    const int designer = service.registerUser("Layout Reviewer", "ui-designer", "designer-pass", "designer-pass", "",
        UserRole::TeamMember, {"Design"}, {"School", "Nickname", "Town"}).id();
    const int researcher = service.registerUser("Workflow Reviewer", "ui-researcher", "research-pass", "research-pass", "",
        UserRole::TeamMember, {"Research"}, {"School", "Nickname", "Town"}).id();
    const int team = service.createTeam(admin, "Interface review team", "Isolated automated verification workspace").id();
    service.joinTeam(member, service.team(team).joinCode());
    service.joinTeam(designer, service.team(team).joinCode());
    service.joinTeam(researcher, service.team(team).joinCode());
    const auto date = [](const QDate& value) { return Date(value.day(), value.month(), value.year()); };
    const int project = service.createProject(admin, team, "Desktop interface review",
        "Check navigation, accessibility, and both themes.", Date::today(), date(QDate::currentDate().addDays(14))).id();
    const int task = service.createTask(admin, project, TaskType::Development, "Review desktop layouts",
        "Verify compact and maximized layouts.", admin, TaskPriority::High, date(QDate::currentDate().addDays(2)), "C++").id();
    service.startTask(admin, task);
    const int finished = service.createTask(admin, project, TaskType::Documentation, "Check recovery instructions",
        "Confirm the security-question guidance.", admin, TaskPriority::Medium, date(QDate::currentDate().addDays(5)), "Documentation").id();
    service.markTaskComplete(admin, finished, "Recovery guidance reviewed.");
    service.createTask(admin, project, TaskType::Research, "Check collaboration workflow", "Verify host and join controls.",
        member, TaskPriority::Medium, date(QDate::currentDate().addDays(7)), "Research");
    const int networkProject = service.createProject(admin, team, "Workspace connections",
        "Hosting, nearby discovery, and synchronization review.", Date::today(), date(QDate::currentDate().addDays(21))).id();
    const int networkTask = service.createTask(admin, networkProject, TaskType::Development, "Review nearby workspaces",
        "Verify the connection picker and selected workspace.", admin, TaskPriority::Medium,
        date(QDate::currentDate().addDays(4)), "C++").id();
    service.startTask(admin, networkTask);
    const int guideProject = service.createProject(admin, team, "User guide review",
        "Help, recovery, and project credit information.", Date::today(), date(QDate::currentDate().addDays(10))).id();
    service.createTask(admin, guideProject, TaskType::Documentation, "Review tutorial and credits",
        "Check navigation instructions and external links.", admin, TaskPriority::Low,
        date(QDate::currentDate().addDays(6)), "Documentation");
    service.sendMessage(member, team, "The review workspace is ready. Please check both themes.");
    service.sendMessage(designer, team, "Please review the project rows and status colors at desktop size.");
    service.sendMessage(researcher, team, "Connection and task workflows are included in this isolated review.");
    QSaveFile fixture(QString::fromStdWString((root / "ui-review-notes.txt").wstring()));
    if (fixture.open(QIODevice::WriteOnly)) {
        fixture.write("TeamSync isolated UI verification attachment.\n");
        fixture.commit();
        service.shareFile(admin, team, root / "ui-review-notes.txt", "Review notes", "Shared-files verification fixture.");
    }
    service.saveAll();
}

using ReviewCheck = std::function<void(const QString&, bool, const QString&)>;

// Exercise the real Widgets and the existing service in an explicitly isolated
// review workspace. Dialogs and destructive confirmations are inspected then
// cancelled; Start and Reopen intentionally update only the review fixture.
void reviewWorkPages(teamsync::TeamSyncGui& window, const std::filesystem::path& root,
                     const QString& username, const ReviewCheck& check) {
    using namespace teamsync;
    TeamSyncService source(root);
    const auto* user = source.findUserByUsername(username.toStdString());
    check("workflow review has matching account", user != nullptr, {});
    if (!user) return;
    const int userId = user->id();
    auto* projects = window.findChild<QTableWidget*>("projectsTable");
    auto* tasks = window.findChild<QTableWidget*>("tasksTable");
    auto* projectQuery = window.findChild<QLineEdit*>("projectSearch");
    auto* taskQuery = window.findChild<QLineEdit*>("taskSearch");
    auto* taskStatus = window.findChild<QComboBox*>("taskStatusFilter");
    check("reference work-page widgets exist", projects && tasks && projectQuery && taskQuery && taskStatus, {});
    if (!projects || !tasks || !projectQuery || !taskQuery || !taskStatus) return;
    const auto click = [&](const QString& name) {
        auto* target = window.findChild<QPushButton*>(name);
        check("workflow control " + name, target != nullptr, {});
        if (target) target->click();
    };
    const auto navigate = [&](const QString& name) {
        auto* target = pageButton(window, name);
        check("workflow page " + name, target != nullptr, {});
        activatePageButton(target);
    };
    const auto modal = [&](const QString& name, const QString& expectedTitle, const std::function<void()>& action) {
        bool inspected = false;
        QTimer inspect;
        inspect.setSingleShot(true);
        QObject::connect(&inspect, &QTimer::timeout, &window, [&] {
            inspected = true;
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check(name, dialog && dialog->windowTitle() == expectedTitle,
                  dialog ? dialog->windowTitle() : "No modal dialog");
            if (dialog) dialog->reject();
        });
        inspect.start(80);
        action();
        if (!inspected) check(name, false, "Action did not open its dialog");
    };
    const auto action = [&](QMenu* menu, const QString& name) -> QAction* {
        if (menu) for (auto* value : menu->actions()) if (value->text() == name) return value;
        check("row action " + name, false, "Missing menu action");
        return nullptr;
    };
    const auto rowWithId = [](QTableWidget* table, const int id) {
        for (int row = 0; row < table->rowCount(); ++row)
            if (table->item(row, 0)->data(Qt::UserRole).toInt() == id) return row;
        return -1;
    };
    const auto checkRowMenu = [&](QTableWidget* table, const QString& name) {
        auto* container = table->cellWidget(0, 6);
        auto* more = container ? container->findChild<QPushButton*>("rowContextButton") : nullptr;
        check(name + " control exists", more && more->menu(), {});
        if (!more || !more->menu()) return;
        bool inspected = false;
        QEventLoop loop;
        QTimer inspect;
        inspect.setSingleShot(true);
        QObject::connect(&inspect, &QTimer::timeout, &window, [&] {
            inspected = true;
            check(name + " selects record before menu action", table->currentRow() == 0, {});
            more->menu()->close();
            loop.quit();
        });
        inspect.start(80);
        QMetaObject::invokeMethod(more, "pressed", Qt::DirectConnection);
        if (!inspected) loop.exec();
    };

    navigate("Projects");
    const auto allProjects = source.projectsForUser(userId);
    const int projectCounts[] = {static_cast<int>(allProjects.size()),
        static_cast<int>(std::count_if(allProjects.begin(), allProjects.end(), [](const Project* value) {
            return value->status() != ProjectStatus::Archived && value->status() != ProjectStatus::Completed;
        })),
        static_cast<int>(std::count_if(allProjects.begin(), allProjects.end(), [](const Project* value) {
            return value->status() == ProjectStatus::Completed;
        }))};
    for (int scope = 0; scope < 3; ++scope) {
        click("projectFilter" + QString::number(scope));
        check("project scope " + QString::number(scope) + " shows actual records",
              projects->rowCount() == projectCounts[scope], QString::number(projects->rowCount()));
    }
    click("projectFilter0");
    projectQuery->setText("__no_matching_review_project__");
    check("project search has working empty state", projects->rowCount() == 0, {});
    projectQuery->clear();
    check("clearing project search restores records", projects->rowCount() == projectCounts[0], {});
    modal("header New project routes correctly", "Create project", [&] { click("pageHeaderAction"); });
    if (projects->rowCount()) {
        checkRowMenu(projects, "project row menu");
        auto* menu = projects->findChild<QMenu*>("projectContextMenu");
        if (auto* edit = action(menu, "Edit project")) modal("project edit action routes correctly", "Edit project", [edit] { edit->trigger(); });
        const int before = projects->rowCount();
        if (auto* archive = action(menu, "Archive project")) modal("project archive confirms first", "Archive project", [archive] { archive->trigger(); });
        check("cancelled project archive preserves rows", projects->rowCount() == before, {});
    }

    navigate("Tasks");
    const auto allTasks = source.searchTasks(userId, "");
    const auto dueSoon = [](const Task* value) {
        const auto& due = value->dueDate();
        const int days = QDate::currentDate().daysTo(QDate(due.year(), due.month(), due.day()));
        return value->status() != TaskStatus::Completed && days >= 0 && days <= 7;
    };
    const int taskCounts[] = {static_cast<int>(allTasks.size()),
        static_cast<int>(std::count_if(allTasks.begin(), allTasks.end(), [userId](const Task* value) {
            return value->createdBy() == userId || value->assignedUserId() == userId;
        })),
        static_cast<int>(std::count_if(allTasks.begin(), allTasks.end(), [userId](const Task* value) { return value->assignedUserId() == userId; })),
        static_cast<int>(std::count_if(allTasks.begin(), allTasks.end(), dueSoon))};
    for (int scope = 0; scope < 4; ++scope) {
        click("taskScope" + QString::number(scope));
        check("task scope " + QString::number(scope) + " shows actual records",
              tasks->rowCount() == taskCounts[scope], QString::number(tasks->rowCount()));
    }
    click("taskScope0");
    for (int status = 1; status <= 4; ++status) {
        taskStatus->setCurrentIndex(status);
        const int expected = static_cast<int>(std::count_if(allTasks.begin(), allTasks.end(), [status](const Task* value) {
            return value->status() == static_cast<TaskStatus>(status - 1);
        }));
        check("task status filter " + QString::number(status), tasks->rowCount() == expected, QString::number(tasks->rowCount()));
    }
    taskStatus->setCurrentIndex(0);
    taskQuery->setText("__no_matching_review_task__");
    check("task search has working empty state", tasks->rowCount() == 0, {});
    taskQuery->clear();
    check("clearing task search restores records", tasks->rowCount() == taskCounts[0], {});
    modal("header New task routes correctly", "Create task", [&] { click("pageHeaderAction"); });
    if (tasks->rowCount()) {
        auto* id = tasks->item(0, 0);
        id->setCheckState(Qt::Unchecked);
        tasks->scrollToItem(id);
        const QPoint point = tasks->visualItemRect(id).center();
        const auto mouseClickCheckbox = [&] {
            const QPointF global = tasks->viewport()->mapToGlobal(point);
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(point), global,
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(point), global,
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(tasks->viewport(), &press);
            QApplication::sendEvent(tasks->viewport(), &release);
        };
        mouseClickCheckbox();
        check("mouse click on visible task checkbox checks row", id->checkState() == Qt::Checked, {});
        auto* summary = window.findChild<QLabel*>("taskTableSummary");
        check("task checkbox retains ID and updates selection count", id->data(Qt::UserRole).toInt() > 0 &&
              summary && summary->text().contains("1 selected"), {});
        tasks->setCurrentCell(0, 0);
        tasks->setFocus(Qt::OtherFocusReason);
        QKeyEvent spacePress(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ");
        QKeyEvent spaceRelease(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier, " ");
        QApplication::sendEvent(tasks, &spacePress);
        QApplication::sendEvent(tasks, &spaceRelease);
        check("Space key toggles task checkbox", id->checkState() == Qt::Unchecked, {});
        mouseClickCheckbox();
        check("task checkbox can be checked again by mouse", id->checkState() == Qt::Checked, {});
        for (auto* button : window.findChildren<QPushButton*>()) if (button->property("taskClearSelection").toBool()) button->click();
        check("clear task selection unchecks rows", tasks->item(0, 0)->checkState() == Qt::Unchecked, {});
        checkRowMenu(tasks, "task row menu");
        auto* menu = tasks->findChild<QMenu*>("taskContextMenu");
        const QList<QPair<QString, QString>> dialogs{{"Mark complete", "Complete task"}, {"Edit task", "Edit task"},
            {"Completion details", "Completion details"}, {"Comments", "Task comments"},
            {"Recommend member", "Smart member recommendation"}, {"Delete task", "Delete task"}};
        const int before = tasks->rowCount();
        for (const auto& pair : dialogs) {
            if (auto* item = action(menu, pair.first)) modal("task action routes: " + pair.first, pair.second, [item] { item->trigger(); });
        }
        check("cancelled task dialogs preserve rows", tasks->rowCount() == before, {});
        for (const auto* value : allTasks) {
            if (value->status() != TaskStatus::Pending) continue;
            const int row = rowWithId(tasks, value->id());
            if (row >= 0) {
                tasks->setCurrentCell(row, 1);
                if (auto* start = action(menu, "Start task")) start->trigger();
                const int updated = rowWithId(tasks, value->id());
                check("Start task updates correct review record", updated >= 0 && tasks->item(updated, 5)->text() == "In Progress", {});
            }
            break;
        }
        for (const auto* value : allTasks) {
            if (value->status() != TaskStatus::Completed) continue;
            const int row = rowWithId(tasks, value->id());
            if (row >= 0) {
                tasks->setCurrentCell(row, 1);
                if (auto* reopen = action(menu, "Reopen task")) reopen->trigger();
                const int updated = rowWithId(tasks, value->id());
                check("Reopen task updates correct review record", updated >= 0 && tasks->item(updated, 5)->text() == "Pending", {});
            }
            break;
        }
    }

    navigate("Overview");
    auto* search = window.findChild<QLineEdit*>("referenceSearch");
    check("global search input exists", search != nullptr, {});
    if (search && !allTasks.empty()) {
        search->setText(QString::fromUtf8(allTasks.front()->title().c_str()));
        int openedId = 0;
        bool inspected = false;
        QTimer openSearch;
        openSearch.setSingleShot(true);
        QObject::connect(&openSearch, &QTimer::timeout, &window, [&] {
            inspected = true;
            auto* dialog = window.findChild<QDialog*>("globalSearchDialog");
            auto* results = window.findChild<QTableWidget*>("globalSearchResults");
            check("global search finds real workspace records", dialog && results && results->rowCount() > 0, {});
            if (dialog && results && results->rowCount() > 0) {
                openedId = results->item(0, 0)->data(Qt::UserRole).toInt();
                results->setCurrentCell(0, 0);
                if (auto* box = dialog->findChild<QDialogButtonBox*>()) box->button(QDialogButtonBox::Open)->click();
            } else if (dialog) dialog->reject();
        });
        openSearch.start(80);
        QMetaObject::invokeMethod(search, "returnPressed", Qt::DirectConnection);
        check("global search dialog opened", inspected, {});
        check("opening task search result selects its task", openedId > 0 && tasks->currentRow() >= 0 &&
              tasks->item(tasks->currentRow(), 0)->data(Qt::UserRole).toInt() == openedId && tasks->isVisible(), {});
    }
}

// Only called with --automation-seed and an explicitly isolated workspace.
// Unlike the read-only visual checks, these workflows intentionally complete
// a fixture task and create a fixture backup through the actual Widgets.
void reviewReliability(teamsync::TeamSyncGui& window, const std::filesystem::path& root,
                       const QString& username, const ReviewCheck& check) {
    using namespace teamsync;
    TeamSyncService initial(root);
    const auto* account = initial.findUserByUsername(username.toStdString());
    check("reliability fixture account exists", account != nullptr, {});
    if (!account) return;
    const int userId = account->id();
    const auto click = [&](const QString& name) {
        auto* button = window.findChild<QPushButton*>(name);
        check("reliability control " + name, button && button->isEnabled(), {});
        if (button && button->isEnabled()) button->click();
    };
    const auto navigate = [&](const QString& name) {
        auto* target = pageButton(window, name);
        check("reliability page " + name, target != nullptr, {});
        activatePageButton(target);
    };
    const auto drain = [](int milliseconds = 100) {
        QEventLoop loop;
        QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
        loop.exec();
    };
    const auto modal = [&](const QString& name, const QString& expectedTitle,
                           const std::function<void()>& action,
                           const std::function<void(QDialog*)>& inspect) {
        bool inspected = false;
        QTimer timer;
        timer.setSingleShot(true);
        QObject::connect(&timer, &QTimer::timeout, &window, [&] {
            inspected = true;
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            const bool expected = dialog && dialog->windowTitle() == expectedTitle;
            check(name, expected, dialog ? dialog->windowTitle() : "No modal dialog");
            if (expected) inspect(dialog);
            else if (dialog) dialog->reject();
        });
        timer.start(80);
        action();
        if (!inspected) drain(220); // Board drop actions are deliberately queued.
        timer.stop();
        if (!inspected) check(name + " inspected", false, "Dialog did not open");
    };

    navigate("Tasks");
    auto* tasks = window.findChild<QTableWidget*>("tasksTable");
    auto* views = window.findChild<QStackedWidget*>("taskViews");
    auto* board = dynamic_cast<ui::TaskBoardWidget*>(window.findChild<QWidget*>("taskBoard"));
    auto* query = window.findChild<QLineEdit*>("taskSearch");
    auto* status = window.findChild<QComboBox*>("taskStatusFilter");
    check("task board and existing table share the task page", tasks && views && board && query && status, {});
    if (!tasks || !views || !board || !query || !status) return;
    click("taskScope0");
    status->setCurrentIndex(0);
    query->clear();
    const auto all = initial.searchTasks(userId, "");
    const auto boardCount = [&] {
        return board->taskCount(TaskStatus::Pending) + board->taskCount(TaskStatus::InProgress) + board->taskCount(TaskStatus::Completed);
    };
    check("table remains the default task view", views->currentIndex() == 0 && tasks->isVisible(), {});
    click("taskBoardView");
    check("Board selector displays the real task board", views->currentWidget() == board && board->isVisible() && !tasks->isVisible(), {});
    check("board initial count matches accessible task records", boardCount() == static_cast<int>(all.size()), QString::number(boardCount()));
    query->setText("__no_reliability_task__");
    check("board follows task search empty state", boardCount() == 0 && tasks->rowCount() == 0, {});
    query->clear();
    check("board clearing search restores tasks", boardCount() == static_cast<int>(all.size()), {});
    for (int value = 1; value <= 4; ++value) {
        status->setCurrentIndex(value);
        const int expected = static_cast<int>(std::count_if(all.begin(), all.end(), [value](const Task* task) {
            return task->status() == static_cast<TaskStatus>(value - 1);
        }));
        check("board follows status filter " + QString::number(value), boardCount() == expected && tasks->rowCount() == expected, {});
    }
    status->setCurrentIndex(0);
    click("taskScope2");
    const int assigned = static_cast<int>(std::count_if(all.begin(), all.end(), [userId](const Task* task) {
        return task->assignedUserId() == userId;
    }));
    check("board follows assigned-to-me scope", boardCount() == assigned && tasks->rowCount() == assigned, {});
    click("taskScope0");

    int taskId = 0;
    for (const auto* task : all) if (task->workflowStatus() == TaskStatus::InProgress) { taskId = task->id(); break; }
    check("reliability fixture has a task in progress", taskId > 0, {});
    if (taskId > 0) {
        board->setSelectedTask(taskId);
        check("board selection targets existing task actions", tasks->currentRow() >= 0 &&
              tasks->item(tasks->currentRow(), 0)->data(Qt::UserRole).toInt() == taskId, {});
        check("board requests Pending move", board->requestMove(taskId, TaskStatus::Pending), {});
        drain();
        TeamSyncService pending(root);
        check("Pending move persists through existing service", pending.task(taskId).workflowStatus() == TaskStatus::Pending &&
              board->containsTask(taskId), {});
        check("board requests In-progress move", board->requestMove(taskId, TaskStatus::InProgress), {});
        drain();
        TeamSyncService started(root);
        check("In-progress move persists through existing service", started.task(taskId).workflowStatus() == TaskStatus::InProgress, {});

        const QString attachment = QDir(QString::fromStdWString(root.wstring())).filePath(
            QString::fromUtf8("finished-\xE0\xA4\xA8\xE0\xA5\x87\xE0\xA4\xAA\xE0\xA4\xBE\xE0\xA4\xB2.txt"));
        const QByteArray content("TeamSync GUI reliability fixture: finished contents.\n");
        QSaveFile output(attachment);
        const bool opened = output.open(QIODevice::WriteOnly);
        bool saved = false;
        if (opened) saved = output.write(content) == content.size() && output.commit();
        check("isolated Unicode attachment fixture created", saved, {});
        if (saved) {
            auto* card = board->findChild<QWidget*>("taskBoardCard" + QString::number(taskId));
            check("native file-drop target exists on board", card != nullptr, {});
            if (card) {
                const QString note = "Completed through the task board with a dropped Unicode file.";
                QMimeData mime;
                mime.setUrls({QUrl::fromLocalFile(attachment)});
                modal("board file drop opens completion form", "Complete task", [&] {
                    QDragEnterEvent enter(QPoint(30, 60), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(card, &enter);
                    check("board accepts real local-file drag as Copy", enter.isAccepted() && enter.dropAction() == Qt::CopyAction, {});
                    QDropEvent drop(QPointF(30, 60), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
                    QApplication::sendEvent(card, &drop);
                    check("board accepts real local-file drop as Copy", drop.isAccepted() && drop.dropAction() == Qt::CopyAction, {});
                }, [&](QDialog* dialog) {
                    auto* files = dialog->findChild<QListWidget*>("completionFiles");
                    auto* notes = dialog->findChild<QTextEdit*>();
                    auto* box = dialog->findChild<QDialogButtonBox*>();
                    check("completion form is prefilled with dropped file", files && files->count() == 1 &&
                          QFileInfo(files->item(0)->text()).canonicalFilePath() == QFileInfo(attachment).canonicalFilePath(), {});
                    check("completion form retains note and confirmation controls", notes && box && box->button(QDialogButtonBox::Ok), {});
                    if (notes && box && box->button(QDialogButtonBox::Ok)) {
                        notes->setPlainText(note);
                        box->button(QDialogButtonBox::Ok)->click();
                    } else dialog->reject();
                });
                TeamSyncService completed(root);
                check("completion through board persists status and note", completed.task(taskId).workflowStatus() == TaskStatus::Completed &&
                      completed.task(taskId).completionNote() == note.toStdString(), {});
                const auto attachments = completed.attachmentsForTask(userId, taskId);
                check("completion through board persists attachment metadata", attachments.size() == 1, QString::number(attachments.size()));
                if (!attachments.empty()) {
                    const auto stored = completed.attachmentPath(userId, attachments.front()->id());
                    QFile original(attachment);
                    QFile copy(QString::fromStdWString(stored.wstring()));
                    const bool originalsOpen = original.open(QIODevice::ReadOnly) && copy.open(QIODevice::ReadOnly);
                    check("dropped attachment is copied byte-for-byte and original remains", originalsOpen && original.readAll() == content && copy.readAll() == content, {});
                }
                check("completed task remains present in completed board column", board->containsTask(taskId) &&
                      board->taskCount(TaskStatus::Completed) >= 2, {});
            }
        }
    }
    click("taskTableView");
    check("Table selector restores the existing task table", views->currentIndex() == 0 && tasks->isVisible(), {});

    navigate("Connection");
    modal("Transfers & sync opens from Connection page", "Transfers & synchronization",
          [&] { click("transferCenterButton"); }, [&](QDialog* dialog) {
        auto* progress = dialog->findChild<QProgressBar*>("transferByteProgress");
        auto* retry = dialog->findChild<QPushButton*>("retryTransferButton");
        auto* cancel = dialog->findChild<QPushButton*>("cancelTransferButton");
        auto* resolve = dialog->findChild<QPushButton*>("resolveWithHostButton");
        check("transfer center shows real byte-progress widget", progress && progress->maximum() == 1000, {});
        check("local idle workspace has no retry, cancel, or conflict-overwrite action", retry && cancel && resolve &&
              !retry->isEnabled() && !cancel->isEnabled() && !resolve->isEnabled(), {});
        dialog->reject();
    });

    navigate("Settings");
    WorkspaceRecovery recovery(root);
    const auto before = recovery.listBackups();
    check("startup creates a dated automatic workspace backup", std::any_of(before.begin(), before.end(), [](const BackupInfo& item) {
        return item.automatic && !item.createdAt.empty() && item.fileCount > 0;
    }), QString::number(before.size()));
    const std::string reason = "GUI reliability verification backup";
    modal("Backups & recovery opens from Settings page", "Backups & recovery",
          [&] { click("settingsRecoveryButton"); }, [&](QDialog* dialog) {
        auto* history = dialog->findChild<QTableWidget*>("recoveryHistoryTable");
        auto* create = dialog->findChild<QPushButton*>("createBackupButton");
        auto* restore = dialog->findChild<QPushButton*>("restoreBackupButton");
        check("recovery history displays actual backup records", history && history->rowCount() == static_cast<int>(before.size()), {});
        check("recovery create-backup control exists", create && create->isEnabled(), {});
        if (create) {
            bool labelOpened = false;
            QTimer label;
            label.setSingleShot(true);
            QObject::connect(&label, &QTimer::timeout, dialog, [&] {
                auto* input = qobject_cast<QInputDialog*>(QApplication::activeModalWidget());
                labelOpened = input && input->windowTitle() == "Create backup";
                check("Create backup requests a personal label", labelOpened, {});
                if (labelOpened) { input->setTextValue(QString::fromStdString(reason)); input->accept(); }
                else if (auto* unexpected = qobject_cast<QDialog*>(QApplication::activeModalWidget())) unexpected->reject();
            });
            label.start(60);
            create->click();
            label.stop();
            check("manual backup dialog was inspected", labelOpened, {});
            check("manual backup immediately appears in recovery table", history && history->rowCount() == static_cast<int>(before.size()) + 1, {});
        }
        if (restore && history && history->rowCount() && account->role() == UserRole::Admin) {
            history->setCurrentCell(0, 0);
            bool confirmation = false;
            QTimer confirm;
            confirm.setSingleShot(true);
            QObject::connect(&confirm, &QTimer::timeout, dialog, [&] {
                auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
                confirmation = message && message->windowTitle() == "Restore workspace";
                check("Restore requires explicit destructive confirmation", confirmation, {});
                if (message) message->done(QMessageBox::Cancel);
            });
            confirm.start(60);
            restore->click();
            confirm.stop();
            check("cancelled Restore leaves current session and history intact", confirmation && signedIn(window) && history->rowCount() > 0, {});
        } else if (account->role() != UserRole::Admin) {
            check("workspace restore is disabled for a regular member", restore && !restore->isEnabled(), {});
        }
        dialog->reject();
    });
    const auto after = recovery.listBackups();
    check("GUI-created manual backup is persisted with label", after.size() == before.size() + 1 &&
          std::any_of(after.begin(), after.end(), [&](const BackupInfo& item) { return !item.automatic && item.reason == reason; }), {});
    check("reliability workflow preserves signed-in session", signedIn(window), {});
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication application(argc, argv);
    application.setApplicationName("TeamSync");
    application.setApplicationDisplayName("TeamSync");
    application.setOrganizationName("TeamSync");
    application.setStyle(QStyleFactory::create("Fusion"));
    application.setFont(QFont("Segoe UI", 10));

    std::filesystem::path root = ".";
    bool smokeTest = false;
    int smokeMilliseconds = 700;
    bool automationHostUi = false;
    bool automationJoinUi = false;
    bool automationJoinAfterLogin = false;
    bool automationConfirmYes = false;
    bool automationConnectionPage = false;
    bool automationTutorialPage = false;
    bool automationChatPanel = false;
    bool automationNotificationsPanel = false;
    bool automationLogin = false;
    bool automationOpenRegistration = false;
    bool automationOpenRecovery = false;
    bool automationSeed = false;
    bool explicitDataDirectory = false;
    bool automationMaximize = false;
    bool automationDiscover = false;
    bool automationSelectFirst = false;
    bool automationWorkflowChecks = false;
    bool automationReliabilityChecks = false;
    bool automationPasswordEye = false;
    QString automationGuest;
    QString automationPage;
    QString automationClick;
    QString automationSettingsDirectory;
    QString automationCaptureDirectory;
    QString automationReport;
    QSize automationSize;
    QString automationUsername;
    QString automationPassword;
    QString automationTheme;
    QString automationJoinAddress;
    quint16 automationJoinPort = 45454;
    QString automationJoinCode;
    QString screenshotPath;
    teamsync::LanLaunchOptions network;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--data-dir" && index + 1 < argc) {
            root = argv[++index];
            explicitDataDirectory = true;
        }
        else if (argument == "--host" && index + 1 < argc) {
            network.mode = teamsync::LanLaunchOptions::Mode::Host;
            bool valid = false;
            const auto port = QString::fromLocal8Bit(argv[++index]).toUShort(&valid);
            network.port = valid && port > 0 ? port : 45454;
        }
        else if (argument == "--host-code" && index + 1 < argc) {
            network.pairingCode = QString::fromLocal8Bit(argv[++index]);
        }
        else if (argument == "--automation-host-ui") automationHostUi = true;
        else if (argument == "--automation-join-after-login") automationJoinAfterLogin = true;
        else if (argument == "--automation-confirm-yes") automationConfirmYes = true;
        else if (argument == "--automation-connection-page") automationConnectionPage = true;
        else if (argument == "--automation-tutorial-page") automationTutorialPage = true;
        else if (argument == "--automation-chat-panel") automationChatPanel = true;
        else if (argument == "--automation-notifications-panel") automationNotificationsPanel = true;
        else if (argument == "--automation-open-registration") automationOpenRegistration = true;
        else if (argument == "--automation-open-recovery") automationOpenRecovery = true;
        else if (argument == "--automation-seed") automationSeed = true;
        else if (argument == "--automation-maximize") automationMaximize = true;
        else if (argument == "--automation-discover") automationDiscover = true;
        else if (argument == "--automation-select-first") automationSelectFirst = true;
        else if (argument == "--automation-workflow-checks") automationWorkflowChecks = true;
        else if (argument == "--automation-reliability-checks") automationReliabilityChecks = true;
        else if (argument == "--automation-password-eye") automationPasswordEye = true;
        else if (argument == "--automation-click" && index + 1 < argc) automationClick = QString::fromLocal8Bit(argv[++index]);
        else if (argument == "--automation-guest" && index + 1 < argc) automationGuest = QString::fromLocal8Bit(argv[++index]);
        else if (argument == "--automation-page" && index + 1 < argc) automationPage = QString::fromLocal8Bit(argv[++index]);
        else if (argument == "--automation-settings-dir" && index + 1 < argc) automationSettingsDirectory = QString::fromLocal8Bit(argv[++index]);
        else if (argument == "--automation-capture-all" && index + 1 < argc) automationCaptureDirectory = QString::fromLocal8Bit(argv[++index]);
        else if (argument == "--automation-report" && index + 1 < argc) automationReport = QString::fromLocal8Bit(argv[++index]);
        else if (argument == "--automation-size" && index + 2 < argc) {
            const int width = QString::fromLocal8Bit(argv[++index]).toInt();
            const int height = QString::fromLocal8Bit(argv[++index]).toInt();
            automationSize = QSize(width, height);
        }
        else if (argument == "--automation-login" && index + 2 < argc) {
            automationLogin = true;
            automationUsername = QString::fromLocal8Bit(argv[++index]);
            automationPassword = QString::fromLocal8Bit(argv[++index]);
        }
        else if (argument == "--automation-theme" && index + 1 < argc) {
            automationTheme = QString::fromLocal8Bit(argv[++index]).toLower();
        }
        else if (argument == "--automation-join-ui" && index + 3 < argc) {
            automationJoinUi = true;
            automationJoinAddress = QString::fromLocal8Bit(argv[++index]);
            bool valid = false;
            const auto port = QString::fromLocal8Bit(argv[++index]).toUShort(&valid);
            automationJoinPort = valid && port > 0 ? port : 45454;
            automationJoinCode = QString::fromLocal8Bit(argv[++index]);
        }
        else if (argument == "--join" && index + 3 < argc) {
            network.mode = teamsync::LanLaunchOptions::Mode::Client;
            network.address = QString::fromLocal8Bit(argv[++index]);
            bool valid = false;
            const auto port = QString::fromLocal8Bit(argv[++index]).toUShort(&valid);
            network.port = valid && port > 0 ? port : 45454;
            network.pairingCode = QString::fromLocal8Bit(argv[++index]);
        }
        else if (argument == "--smoke-test") smokeTest = true;
        else if (argument == "--smoke-ms" && index + 1 < argc) {
            smokeTest = true;
            smokeMilliseconds = std::max(100, QString::fromLocal8Bit(argv[++index]).toInt());
        }
        else if (argument == "--screenshot" && index + 1 < argc) {
            screenshotPath = QString::fromLocal8Bit(argv[++index]);
        }
    }

    if (network.mode == teamsync::LanLaunchOptions::Mode::Client) {
        const std::string identity = network.address.toStdString() + ':' + std::to_string(network.port);
        root /= "network_cache";
        root /= std::to_string(std::hash<std::string>{}(identity));
    }
    // Review sessions use separate settings, so screenshots and tests cannot replace
    // the user's remembered login or theme preference.
    const bool automation = automationLogin || automationHostUi || automationJoinUi || automationSeed ||
        automationOpenRegistration || automationOpenRecovery || !automationGuest.isEmpty() ||
        !automationPage.isEmpty() || !automationTheme.isEmpty() || !automationCaptureDirectory.isEmpty() ||
        !screenshotPath.isEmpty() || !automationReport.isEmpty() || smokeTest || automationDiscover || !automationClick.isEmpty() ||
        automationWorkflowChecks || automationReliabilityChecks || automationPasswordEye;
    if (automationSettingsDirectory.isEmpty() && automation) {
        automationSettingsDirectory = QString::fromStdWString((std::filesystem::absolute(root) / "ui-review-settings").wstring());
    }
    if (!automationSettingsDirectory.isEmpty()) {
        QDir().mkpath(automationSettingsDirectory);
        application.setProperty("teamsyncSettingsDir", automationSettingsDirectory);
    }
    if (automationSeed) {
        if (!explicitDataDirectory) return 2;
        try { seedReviewWorkspace(root); }
        catch (const std::exception& error) { qCritical("UI review fixture creation failed: %s", error.what()); return 2; }
    }
    if (automationTheme == "light" || automationTheme == "dark" || automationTheme == "system") {
        auto settings = teamsync::appSettings();
        settings.setValue("appearance/theme", automationTheme);
        if (automationTheme != "system") settings.setValue("appearance/darkTheme", automationTheme == "dark");
    }
    teamsync::TeamSyncGui window(root, network);
    if (automationSize.isValid()) window.resize(automationSize);
    if (automationMaximize) window.showMaximized();
    else window.show();
    QJsonArray checks;
    QJsonArray scenes;
    int failures = 0;
    std::function<void()> checkpointReport;
    const auto check = [&](const QString& name, const bool passed, const QString& detail = QString()) {
        checks.append(QJsonObject{{"check", name}, {"passed", passed}, {"detail", detail}});
        if (!passed) ++failures;
        if (checkpointReport) checkpointReport();
    };
    const auto measureScene = [&](const QString& name, QWidget* target) {
        QJsonArray widgets;
        const QStringList measuredNames = {"sidebar", "pageHeader", "referenceSearch", "metricCard", "authCard", "loginBrand", "brandPanel", "loginUsername",
            "loginPassword", "loginButton", "overviewMyTasks", "overviewProjects", "projectsTable", "tasksTable"};
        for (const auto& objectName : measuredNames) {
            if (auto* widget = window.findChild<QWidget*>(objectName); widget && widget->isVisible()) {
                const QPoint position = widget->mapTo(&window, QPoint());
                widgets.append(QJsonObject{{"object", objectName}, {"x", position.x()}, {"y", position.y()},
                    {"width", widget->width()}, {"height", widget->height()},
                    {"fontPixels", widget->fontInfo().pixelSize()}});
            }
        }
        QJsonArray views;
        for (auto* view : target->findChildren<QAbstractItemView*>()) {
            if (view->isVisible() && view->model()) {
                views.append(QJsonObject{{"object", view->objectName()}, {"rows", view->model()->rowCount()},
                    {"columns", view->model()->columnCount()}});
            }
        }
        scenes.append(QJsonObject{{"name", name}, {"windowWidth", window.width()}, {"windowHeight", window.height()},
            {"devicePixelRatio", window.devicePixelRatioF()}, {"background", window.palette().color(QPalette::Window).name()},
            {"widgets", widgets}, {"views", views}});
    };
    const auto writeReport = [&] {
        if (automationReport.isEmpty()) return;
        QDir().mkpath(QFileInfo(automationReport).absolutePath());
        QSaveFile report(automationReport);
        if (report.open(QIODevice::WriteOnly)) {
            report.write(QJsonDocument(QJsonObject{{"passed", failures == 0}, {"failures", failures}, {"checks", checks}, {"scenes", scenes}})
                         .toJson(QJsonDocument::Indented));
            report.commit();
        }
    };
    checkpointReport = writeReport;
    QObject::connect(&application, &QApplication::aboutToQuit, &application, writeReport);
    if (automationConfirmYes) {
        auto* confirmations = new QTimer(&application);
        confirmations->setInterval(100);
        QObject::connect(confirmations, &QTimer::timeout, &application, [&] {
            if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
                if (auto* yes = message->button(QMessageBox::Yes)) yes->click();
            }
        });
        confirmations->start();
    }
    if (automationPasswordEye) {
        QTimer::singleShot(170, &application, [&] {
            auto* password = window.findChild<QLineEdit*>("loginPassword");
            auto* reveal = window.findChild<QAction*>("passwordVisibilityAction");
            check("password visibility control exists", password && reveal);
            if (!password || !reveal) return;
            check("password initially masked", password->echoMode() == QLineEdit::Password);
            reveal->trigger();
            check("password eye reveals value", password->echoMode() == QLineEdit::Normal);
            reveal->trigger();
            check("password eye restores masking", password->echoMode() == QLineEdit::Password);
        });
    }
    if (automationWorkflowChecks) {
        if (!explicitDataDirectory || automationUsername.isEmpty()) return 2;
        QTimer::singleShot(1000, &application, [&] {
            check("workflow review signed in", signedIn(window));
            try { reviewWorkPages(window, root, automationUsername, check); }
            catch (const std::exception& error) { check("workflow review completed", false, QString::fromUtf8(error.what())); }
            writeReport();
            application.exit(failures ? 3 : 0);
        });
    }
    if (automationReliabilityChecks) {
        // Completing tasks and creating backups must never run against a normal
        // workspace merely because a caller supplied a data-directory flag.
        if (!explicitDataDirectory || !automationSeed || automationUsername.isEmpty() ||
            network.mode != teamsync::LanLaunchOptions::Mode::Local) return 2;
        QTimer::singleShot(20000, &application, [&] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            check("reliability workflow finishes within bounded test time", false,
                  dialog ? "Blocked by dialog: " + dialog->windowTitle() : "No modal dialog");
            if (dialog) dialog->reject();
            writeReport();
            application.exit(3);
        });
        QTimer::singleShot(1000, &application, [&] {
            check("reliability review signed in", signedIn(window));
            try { reviewReliability(window, root, automationUsername, check); }
            catch (const std::exception& error) { check("reliability review completed", false, QString::fromUtf8(error.what())); }
            writeReport();
            application.exit(failures ? 3 : 0);
        });
    }
    if (automationHostUi) {
        QTimer::singleShot(150, &application, [&] {
            auto* host = window.findChild<QPushButton*>("hostConnectionButton");
            if (!host) return;
            QTimer::singleShot(80, &application, [&] {
                if (auto* port = window.findChild<QSpinBox*>("hostPortSpin")) port->setValue(45682);
                for (auto* dialog : window.findChildren<QDialog*>()) {
                    if (auto* box = dialog->findChild<QDialogButtonBox*>()) {
                        if (auto* ok = box->button(QDialogButtonBox::Ok)) ok->click();
                    }
                }
            });
            host->click();
        });
    }
    if (automationJoinUi) {
        QTimer::singleShot(automationJoinAfterLogin ? 1000 : 150, &application, [&] {
            auto* join = window.findChild<QPushButton*>("joinConnectionButton");
            if (!join) return;
            QTimer::singleShot(80, &application, [&] {
                for (auto* checkbox : window.findChildren<QCheckBox*>()) {
                    if (checkbox->text().contains("manually", Qt::CaseInsensitive)) checkbox->setChecked(true);
                }
                if (auto* address = window.findChild<QLineEdit*>("joinHostAddress")) address->setText(automationJoinAddress);
                if (auto* port = window.findChild<QSpinBox*>("joinPortSpin")) port->setValue(automationJoinPort);
                if (auto* code = window.findChild<QLineEdit*>("joinPairingCode")) code->setText(automationJoinCode);
                for (auto* dialog : window.findChildren<QDialog*>()) {
                    if (auto* box = dialog->findChild<QDialogButtonBox*>()) {
                        if (auto* ok = box->button(QDialogButtonBox::Ok)) ok->click();
                    }
                }
            });
            join->click();
        });
    }
    if (automationConnectionPage) {
        QTimer::singleShot(automationJoinUi ? 3400 : 700, &application, [&] {
            for (auto* navigation : window.findChildren<QPushButton*>()) {
                if (navigation->text() == "Connection") {
                    activatePageButton(navigation);
                    break;
                }
            }
        });
    }
    if (automationLogin) {
        QTimer::singleShot(automationJoinUi && !automationJoinAfterLogin ? 2500 : 250, &application, [&] {
            auto* username = window.findChild<QLineEdit*>("loginUsername");
            auto* password = window.findChild<QLineEdit*>("loginPassword");
            auto* login = window.findChild<QPushButton*>("loginButton");
            if (username && password && login) {
                username->setText(automationUsername);
                password->setText(automationPassword);
                login->click();
                check("manual login opens workspace", signedIn(window));
            }
        });
    }
    if (!automationGuest.isEmpty()) {
        QTimer::singleShot(250, &application, [&] {
            auto* guest = window.findChild<QPushButton*>("guestButton");
            if (!guest) {
                for (auto* candidate : window.findChildren<QPushButton*>()) {
                    if (normalized(candidate->text()) == "continue as guest") { guest = candidate; break; }
                }
            }
            check("guest control exists", guest != nullptr);
            if (!guest) return;
            QTimer::singleShot(60, &application, [&] {
                for (auto* dialog : window.findChildren<QInputDialog*>()) {
                    if (dialog->isVisible()) { dialog->setTextValue(automationGuest); dialog->accept(); }
                }
            });
            guest->click();
            check("guest opens workspace", signedIn(window));
        });
    }
    if (!automationPage.isEmpty()) {
        QTimer::singleShot(automationJoinUi ? 3400 : 700, &application, [&] {
            auto* navigation = pageButton(window, automationPage);
            check("page control " + automationPage, navigation != nullptr);
            activatePageButton(navigation);
        });
    }
    if (automationSelectFirst || !automationClick.isEmpty()) {
        QTimer::singleShot(automationJoinUi ? 3700 : 900, &application, [&] {
            if (automationSelectFirst) {
                for (auto* view : window.findChildren<QAbstractItemView*>()) {
                    if (view->isVisible() && view->model() && view->model()->rowCount() > 0) {
                        view->setCurrentIndex(view->model()->index(0, 0));
                        break;
                    }
                }
            }
            if (!automationClick.isEmpty()) {
                QPushButton* target = nullptr;
                QAction* menuAction = nullptr;
                for (auto* candidate : window.findChildren<QPushButton*>()) {
                    if (candidate->isVisible() && (normalized(candidate->text()) == normalized(automationClick) ||
                        candidate->objectName() == automationClick)) { target = candidate; break; }
                    if (candidate->isVisible() && candidate->menu()) {
                        for (auto* action : candidate->menu()->actions()) {
                            if (normalized(action->text()) == normalized(automationClick) || action->objectName() == automationClick) {
                                menuAction = action;
                                break;
                            }
                        }
                    }
                }
                check("action control " + automationClick, (target && target->isEnabled()) || (menuAction && menuAction->isEnabled()));
                if (target && target->isEnabled()) target->click();
                else if (menuAction && menuAction->isEnabled()) menuAction->trigger();
            }
        });
    }
    if (automationOpenRegistration || automationOpenRecovery) {
        QTimer::singleShot(180, &application, [&] {
            const QString target = automationOpenRegistration ? "Create a new account" : "Forgot password?";
            for (auto* candidate : window.findChildren<QPushButton*>()) {
                if (candidate->text() == target || candidate->objectName() ==
                    (automationOpenRegistration ? "registerButton" : "forgotPasswordButton")) {
                    candidate->click();
                    break;
                }
            }
        });
    }
    if (automationTutorialPage || automationChatPanel || automationNotificationsPanel) {
        QTimer::singleShot(700, &application, [&] {
            if (automationTutorialPage) {
                for (auto* navigation : window.findChildren<QPushButton*>()) {
                    if (navigation->objectName() == "navButton" &&
                        navigation->text().contains("Tutorial")) {
                        activatePageButton(navigation);
                        break;
                    }
                }
            }
            if (automationChatPanel) {
                if (auto* chat = window.findChild<QPushButton*>("floatingChatButton")) chat->click();
            }
            if (automationNotificationsPanel) {
                if (auto* notices = window.findChild<QPushButton*>("notificationButton")) notices->click();
            }
        });
    }
    if (automationDiscover) {
        QTimer::singleShot(200, &application, [&] {
            const auto found = teamsync::LanSyncManager::discoverNearby();
            check("nearby discovery finds host", !found.empty(), QString::number(found.size()) + " workspace(s)");
            writeReport();
            application.exit(failures ? 3 : 0);
        });
    }
    if (!automationCaptureDirectory.isEmpty()) {
        QDir().mkpath(automationCaptureDirectory);
        QTimer::singleShot(automationJoinUi ? 3600 : 900, &application, [&] {
            auto pages = std::make_shared<QStringList>();
            for (auto* button : window.findChildren<QPushButton*>()) {
                const QString label = button->property("pageName").toString().isEmpty()
                    ? button->text() : button->property("pageName").toString();
                if ((button->objectName() == "navButton" || button->property("pageIndex").isValid()) &&
                    button->isCheckable() && !pages->contains(label)) pages->append(label);
            }
            check("all-page review is signed in", signedIn(window));
            check("all expected pages available", pages->size() >= 11, QString::number(pages->size()));
            auto capture = [&, pages](const QString& name) {
                const QString path = QDir(automationCaptureDirectory).filePath(name + ".png");
                measureScene(name, &window);
                check("screenshot " + name, window.grab().save(path), path);
            };
            auto step = std::make_shared<std::function<void(int)>>();
            *step = [&, pages, capture, step](const int index) {
                if (index >= pages->size()) {
                    window.resize(window.minimumSize());
                    QTimer::singleShot(320, &application, [&, capture, step] {
                        capture("smallest-window");
                        check("minimum-size layout", window.width() >= window.minimumWidth() && window.height() >= window.minimumHeight());
                        window.showMaximized();
                        QTimer::singleShot(320, &application, [&, capture, step] {
                            capture("maximized-window");
                            check("native maximize", window.isMaximized());
                            writeReport();
                            *step = {};
                            application.exit(failures ? 3 : 0);
                        });
                    });
                    return;
                }
                const QString name = pages->at(index);
                auto* navigation = pageButton(window, name);
                check("navigation control " + name, navigation != nullptr);
                activatePageButton(navigation);
                QTimer::singleShot(320, &application, [&, name, capture, step, index] {
                    auto* stack = window.findChild<QStackedWidget*>("contentStack");
                    check("navigate " + name, signedIn(window) && stack && stack->currentWidget()->isVisible());
                    if (stack && stack->currentWidget()) {
                        QJsonArray tables;
                        for (auto* view : stack->currentWidget()->findChildren<QAbstractItemView*>()) {
                            if (view->isVisible() && view->model()) tables.append(QJsonObject{{"object", view->objectName()}, {"rows", view->model()->rowCount()}});
                        }
                        checks.append(QJsonObject{{"check", "data " + name}, {"passed", true}, {"views", tables}});
                    }
                    // Background review windows have no OS activation. Inspect
                    // their actual Qt tab-focus chain without stealing user focus.
                    QWidget* first = stack ? stack->currentWidget() : &window;
                    QWidget* focus = first;
                    bool validFocusTarget = false;
                    do {
                        focus = focus->nextInFocusChain();
                        if (focus->isVisible() && focus->isEnabled() &&
                            (focus->focusPolicy() & Qt::TabFocus) && window.isAncestorOf(focus)) {
                            validFocusTarget = true;
                            break;
                        }
                    } while (focus && focus != first);
                    check("keyboard focus chain " + name, validFocusTarget,
                          validFocusTarget ? focus->metaObject()->className() : "No enabled tab-focus target");
                    QString filename = normalized(name);
                    filename.replace(' ', '-');
                    capture(QString::number(index).rightJustified(2, '0') + '-' + filename);
                    (*step)(index + 1);
                });
            };
            (*step)(0);
        });
    } else if (!screenshotPath.isEmpty()) {
        QTimer::singleShot(automationJoinUi ? 4400 : 1800, &application, [&] {
            // Absolute automation timers may all become ready together on a
            // busy machine. Let navigation's relative transition finish before
            // capturing, rather than recording its previous-page overlay.
            QEventLoop settle;
            QTimer::singleShot(350, &settle, &QEventLoop::quit);
            settle.exec();
            if (automationClick == "taskBoardView") {
                auto* views = window.findChild<QStackedWidget*>("taskViews");
                auto* board = window.findChild<QWidget*>("taskBoard");
                check("requested task board is visible", views && board &&
                      views->currentWidget() == board && board->isVisible());
            }
            QWidget* target = &window;
            if (automationOpenRegistration) {
                if (auto* dialog = window.findChild<QDialog*>("registrationDialog")) target = dialog;
            } else if (automationOpenRecovery) {
                if (auto* dialog = window.findChild<QDialog*>("recoveryDialog")) target = dialog;
            }
            if (automationChatPanel) {
                if (auto* dialog = window.findChild<QDialog*>("chatPanel")) target = dialog;
            } else if (automationNotificationsPanel) {
                if (auto* dialog = window.findChild<QDialog*>("notificationsPanel")) target = dialog;
            }
            if (!automationClick.isEmpty() && QApplication::activeModalWidget()) target = QApplication::activeModalWidget();
            QDir().mkpath(QFileInfo(screenshotPath).absolutePath());
            measureScene("requested screenshot", target);
            check("requested screenshot saved", target->grab().save(screenshotPath), screenshotPath);
            if (automationLogin || !automationGuest.isEmpty()) check("session still active", signedIn(window));
            writeReport();
            application.exit(failures ? 3 : 0);
        });
    } else if (smokeTest) {
        QTimer::singleShot(smokeMilliseconds, &application, [&] {
            check("window shown", window.isVisible());
            if (automationLogin || !automationGuest.isEmpty()) check("session still active", signedIn(window));
            writeReport();
            application.exit(failures ? 3 : 0);
        });
    }
    return application.exec();
}
