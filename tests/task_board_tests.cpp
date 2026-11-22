#include "TaskBoardWidget.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QMimeData>
#include <QTemporaryDir>
#include <QUrl>

#include <iostream>
#include <stdexcept>

using teamsync::TaskStatus;
using teamsync::ui::TaskBoardWidget;

namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}

void flush() {
    QApplication::processEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QApplication::processEvents();
}

TaskBoardWidget::Card card(int id, TaskStatus status, bool overdue = false) {
    TaskBoardWidget::Card value;
    value.id = id;
    value.title = "Task " + QString::number(id);
    value.project = "Board test project";
    value.assignee = "Test member";
    value.description = "Actual card fixture used only by the isolated board test.";
    value.dueDate = QDate::currentDate().addDays(overdue ? -1 : 2);
    value.status = status;
    value.overdue = overdue;
    return value;
}
}

int main(int argc, char** argv) {
    QApplication application(argc, argv);
    application.setProperty("darkTheme", false);
    application.setProperty("animationsEnabled", false);
    try {
        TaskBoardWidget board;
        board.resize(960, 600);
        board.setTasks({card(1, TaskStatus::Pending), card(2, TaskStatus::InProgress, true),
                        card(3, TaskStatus::Completed), card(3, TaskStatus::Pending), card(-4, TaskStatus::Pending)});
        board.show();
        flush();
        check(board.taskCount(TaskStatus::Pending) == 1, "Pending count ignores duplicate/invalid task IDs");
        check(board.taskCount(TaskStatus::InProgress) == 1, "Overdue in-progress task keeps its workflow column");
        check(board.taskCount(TaskStatus::Completed) == 1, "Completed count");
        check(board.containsTask(1) && !board.containsTask(-4), "Only real positive task IDs are present");

        int selected = 0;
        board.onSelected = [&](int id) { selected = id; };
        board.setSelectedTask(2);
        check(selected == 2 && board.selectedTaskId() == 2, "Board selection notifies the task-action owner");
        board.setSelectedTask(999);
        check(board.selectedTaskId() == 2, "Unknown selection is ignored");

        int movedId = 0;
        TaskStatus requested = TaskStatus::Pending;
        board.onStatusRequested = [&](int id, TaskStatus status) { movedId = id; requested = status; };
        check(!board.requestMove(999, TaskStatus::Completed), "Unknown tasks cannot request workflow changes");
        check(!board.requestMove(2, TaskStatus::Overdue), "Overdue is not a movable workflow column");
        check(!board.requestMove(2, TaskStatus::InProgress), "Same-column drop is a no-op");
        check(board.requestMove(1, TaskStatus::Completed), "Valid completion move is accepted");
        check(movedId == 0 && board.taskCount(TaskStatus::Completed) == 1,
              "Moves request service actions asynchronously and never mutate displayed records optimistically");
        flush();
        check(movedId == 1 && requested == TaskStatus::Completed, "Completion request reaches the owner for confirmation");

        QMimeData internal;
        internal.setData("application/x-teamsync-task-board", board.dragPayload(1));
        check(board.draggedTask(&internal) == 1, "Same-board drag payload is accepted");
        TaskBoardWidget other;
        other.setTasks({card(1, TaskStatus::Pending)});
        check(other.draggedTask(&internal) == 0, "Cross-workspace drag payloads cannot target matching numeric IDs");
        QMimeData spoofed;
        spoofed.setData("application/x-teamsync-task-board", "spoof:1");
        check(board.draggedTask(&spoofed) == 0, "Forged task payload is rejected");
        auto* completedColumn = board.findChild<QWidget*>("taskBoardColumn2");
        check(completedColumn != nullptr, "Completed drop column exists");
        movedId = 0;
        QDragEnterEvent enter(QPoint(20, 50), Qt::MoveAction, &internal, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(completedColumn, &enter);
        check(enter.isAccepted(), "Column accepts an internal drag");
        QDropEvent drop(QPointF(20, 50), Qt::MoveAction, &internal, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(completedColumn, &drop);
        flush();
        check(drop.isAccepted() && movedId == 1 && requested == TaskStatus::Completed, "Column drop requests completion");

        int opened = 0;
        int contextId = 0;
        board.onOpenRequested = [&](int id) { opened = id; };
        board.onContextRequested = [&](int id, const QPoint&) { contextId = id; };
        auto* firstCard = board.findChild<QWidget*>("taskBoardCard1");
        check(firstCard != nullptr && firstCard->focusPolicy() == Qt::StrongFocus, "Cards can receive keyboard focus");
        QMimeData secondTask;
        secondTask.setData("application/x-teamsync-task-board", board.dragPayload(2));
        QDragEnterEvent cardEnter(QPoint(25, 65), Qt::MoveAction, &secondTask, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(firstCard, &cardEnter);
        check(cardEnter.isAccepted(), "Existing cards allow another task to be dropped into their column");
        QDropEvent cardDrop(QPointF(25, 65), Qt::MoveAction, &secondTask, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(firstCard, &cardDrop);
        flush();
        check(cardDrop.isAccepted() && movedId == 2 && requested == TaskStatus::Pending,
              "Dropping onto a card moves the dragged task, not the target task");
        QKeyEvent openKey(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QApplication::sendEvent(firstCard, &openKey);
        flush();
        check(opened == 1, "Enter exposes edit without requiring a mouse");
        QKeyEvent menuKey(QEvent::KeyPress, Qt::Key_F10, Qt::ShiftModifier);
        QApplication::sendEvent(firstCard, &menuKey);
        flush();
        check(contextId == 1, "Shift+F10 opens the same task-action menu");
        movedId = 0;
        QKeyEvent moveKey(QEvent::KeyPress, Qt::Key_Right, Qt::AltModifier);
        QApplication::sendEvent(firstCard, &moveKey);
        flush();
        check(movedId == 1 && requested == TaskStatus::InProgress, "Alt+Right is a keyboard equivalent to dragging a task");

        QTemporaryDir temporary;
        check(temporary.isValid(), "Isolated attachment fixture directory created");
        const QString path = temporary.filePath(QString::fromUtf8("finished-\xE0\xA4\xA8\xE0\xA5\x87\xE0\xA4\xAA\xE0\xA4\xBE\xE0\xA4\xB2.txt"));
        QFile file(path);
        check(file.open(QIODevice::WriteOnly), "Attachment fixture can be written");
        file.write("finished contents");
        file.close();
        QMimeData files;
        files.setUrls({QUrl::fromLocalFile(path), QUrl::fromLocalFile(path),
                       QUrl::fromLocalFile(temporary.path()), QUrl::fromLocalFile(temporary.filePath("missing.txt")),
                       QUrl("https://example.invalid/not-a-local-file")});
        const QString canonical = QFileInfo(path).canonicalFilePath();
        check(TaskBoardWidget::localFiles(&files) == QStringList{canonical},
              "External file validation retains Unicode files, deduplicates, and excludes directories/missing/remote URLs");
        int attachedId = 0;
        QStringList attached;
        board.onFilesDropped = [&](int id, const QStringList& values) { attachedId = id; attached = values; };
        check(!board.requestFileDrop(999, {path}), "Cannot attach files to unknown tasks");
        check(!board.requestFileDrop(1, {temporary.path()}), "Directory-only drops are rejected");
        check(board.requestFileDrop(1, {path, path}), "Valid file drop requests the completion owner");
        check(attachedId == 0, "File drop is deferred and does not finish a task itself");
        flush();
        check(attachedId == 1 && attached == QStringList{canonical}, "Completion callback receives validated paths once");
        check(board.taskCount(TaskStatus::Completed) == 1, "Attachment requests never mark tasks complete without service confirmation");
        attachedId = 0;
        QDragEnterEvent fileEnter(QPoint(30, 60), Qt::CopyAction | Qt::MoveAction, &files, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(firstCard, &fileEnter);
        check(fileEnter.isAccepted() && fileEnter.dropAction() == Qt::CopyAction, "File drag explicitly copies, never moves original files");
        QDropEvent fileDrop(QPointF(30, 60), Qt::CopyAction | Qt::MoveAction, &files, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(firstCard, &fileDrop);
        flush();
        check(fileDrop.isAccepted() && fileDrop.dropAction() == Qt::CopyAction && attachedId == 1,
              "Native card file-drop event opens completion with copy semantics");

        // A sync refresh may remove a task before a deferred action is delivered.
        attachedId = 0;
        check(board.requestFileDrop(1, {path}), "Queued attachment request accepted before refresh");
        board.setTasks({card(2, TaskStatus::InProgress, true)});
        flush();
        check(attachedId == 0, "Removed tasks cannot receive stale queued attachment actions");
        check(board.selectedTaskId() == 0, "Refresh clears selection when the task no longer matches filters");
        check(board.taskCount(TaskStatus::InProgress) == 1 && board.taskCount(TaskStatus::Pending) == 0,
              "Filtered board contents replace all columns");
        auto* preserved = board.findChild<QWidget*>("taskBoardCard2");
        board.setTasks({card(2, TaskStatus::InProgress, true)});
        check(board.findChild<QWidget*>("taskBoardCard2") == preserved,
              "Unchanged sync refresh preserves existing widgets and an in-progress drag");
        application.setProperty("darkTheme", true);
        board.update();
        check(!board.grab().isNull(), "Board renders with the dark palette");
        std::cout << "Task board tests passed: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Task board test failed after " << checks << " checks: " << error.what() << '\n';
        return 1;
    }
}
