#pragma once

#include "Task.h"

#include <QDate>
#include <QList>
#include <QPoint>
#include <QStringList>
#include <QWidget>

#include <functional>

class QMimeData;

namespace teamsync::ui {

// This widget only presents tasks and requests actions. The service remains the
// authority for permissions, persistence, synchronization, and workflow changes.
class TaskBoardWidget final : public QWidget {
public:
    struct Card {
        int id = 0;
        QString title;
        QString project;
        QString description;
        TaskPriority priority = TaskPriority::Medium;
        QString assignee;
        QDate dueDate;
        TaskStatus status = TaskStatus::Pending;
        bool overdue = false;
    };

    explicit TaskBoardWidget(QWidget* parent = nullptr);
    void setTasks(const QList<Card>& tasks);
    void setSelectedTask(int id);
    int selectedTaskId() const { return selectedTaskId_; }
    int taskCount(TaskStatus status) const;
    bool containsTask(int id) const;

    // Valid requests are delivered on the next event-loop turn, so a service
    // refresh can safely rebuild cards after a drop or menu action.
    bool requestMove(int id, TaskStatus status);
    bool requestFileDrop(int id, const QStringList& paths);
    void requestContextMenu(int id, const QPoint& globalPosition);
    void requestOpen(int id);
    static QStringList localFiles(const QMimeData* mime);

    std::function<void(int)> onSelected;
    std::function<void(int)> onOpenRequested;
    std::function<void(int, TaskStatus)> onStatusRequested;
    std::function<void(int, const QStringList&)> onFilesDropped;
    std::function<void(int, const QPoint&)> onContextRequested;

    QByteArray dragPayload(int id) const;
    int draggedTask(const QMimeData* mime) const;

private:
    QList<Card> tasks_;
    QList<QWidget*> columns_;
    int selectedTaskId_ = 0;
    QByteArray dragToken_;
    bool initialized_ = false;
};

} // namespace teamsync::ui
