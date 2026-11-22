#include "TaskBoardWidget.h"

#include "PremiumWidgets.h"

#include <QApplication>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollArea>
#include <QSet>
#include <QStyle>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>

#include <algorithm>

namespace teamsync::ui {
namespace {

constexpr auto taskMime = "application/x-teamsync-task-board";

bool validColumn(TaskStatus status) {
    return status == TaskStatus::Pending || status == TaskStatus::InProgress || status == TaskStatus::Completed;
}

bool dark() { return qApp->property("darkTheme").toBool(); }
QColor muted() { return QColor(dark() ? "#9CACBF" : "#64748B"); }
QColor foreground() { return QColor(dark() ? "#ECF2FA" : "#102038"); }

QString label(TaskStatus status) {
    switch (status) {
    case TaskStatus::Pending: return "Pending";
    case TaskStatus::InProgress: return "In progress";
    case TaskStatus::Completed: return "Completed";
    default: return "Pending";
    }
}

QColor statusColor(TaskStatus status) {
    if (status == TaskStatus::InProgress) return QColor("#147DFA");
    if (status == TaskStatus::Completed) return QColor("#16A069");
    return QColor("#94A3B8");
}

class BoardCard final : public QWidget {
public:
    BoardCard(TaskBoardWidget* board, const TaskBoardWidget::Card& card, QWidget* parent)
        : QWidget(parent), board_(board), card_(card) {
        setObjectName("taskBoardCard" + QString::number(card.id));
        setMinimumHeight(174);
        setMaximumHeight(174);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::PointingHandCursor);
        setAcceptDrops(true);
        setAccessibleName(card.title + ", " + label(card.status) + (card.overdue ? ", overdue" : QString()));
        setAccessibleDescription("Press Enter to edit. Press Shift+F10 for task actions. "
                                 "Alt+Left and Alt+Right move between columns. Drop files to attach them when completing.");
        setToolTip(card.title + "\n" + card.description + "\n\nDrag to move, or right-click for task actions.\n"
                   "Drop files here to open the completion form with attachments.");
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const bool selected = board_->selectedTaskId() == card_.id;
        const QRectF bounds = rect().adjusted(1, 1, -1, -1);
        const QColor background(dark() ? "#1A2431" : "#FFFFFF");
        painter.setBrush(background);
        painter.setPen(QPen(selected || hasFocus() || fileHover_ ? accentColor() :
                            QColor(dark() ? "#324153" : "#DFE8F2"), selected || hasFocus() || fileHover_ ? 2 : 1));
        painter.drawRoundedRect(bounds, 10, 10);
        QFont small = font();
        small.setPixelSize(11);
        painter.setFont(small);
        painter.setPen(muted());
        painter.drawText(QRect(15, 12, width() - 48, 18), Qt::AlignVCenter,
                         fontMetrics().elidedText(card_.project, Qt::ElideRight, width() - 50));
        QFont menuFont = font();
        menuFont.setPixelSize(20);
        painter.setFont(menuFont);
        painter.drawText(QRect(width() - 34, 8, 24, 26), Qt::AlignCenter, QString::fromUtf8("\xE2\x8B\xAE"));

        QFont title = font();
        title.setPixelSize(14);
        title.setWeight(QFont::DemiBold);
        painter.setFont(title);
        painter.setPen(foreground());
        painter.drawText(QRect(15, 38, width() - 30, 40), Qt::AlignLeft | Qt::TextWordWrap,
                         QFontMetrics(title).elidedText(card_.title, Qt::ElideRight, 2 * (width() - 38)));
        painter.setFont(small);
        painter.setPen(muted());
        painter.drawText(QRect(15, 82, width() - 30, 18), Qt::AlignVCenter,
                         QFontMetrics(small).elidedText(card_.description.simplified(), Qt::ElideRight, width() - 30));

        const QString priority = QString::fromStdString(toString(card_.priority));
        QColor tone = card_.priority >= TaskPriority::High ? QColor("#E25461") :
                      card_.priority == TaskPriority::Medium ? QColor("#C98B10") : QColor("#159769");
        QColor badge = tone;
        badge.setAlpha(dark() ? 42 : 23);
        const int badgeWidth = QFontMetrics(small).horizontalAdvance(priority) + 18;
        painter.setPen(Qt::NoPen);
        painter.setBrush(badge);
        painter.drawRoundedRect(QRectF(15, 108, badgeWidth, 21), 8, 8);
        painter.setPen(tone);
        painter.drawText(QRect(15, 108, badgeWidth, 21), Qt::AlignCenter, priority);
        painter.setPen(card_.overdue ? QColor("#E25461") : muted());
        const QString due = (card_.overdue ? "Overdue " : "Due ") + card_.dueDate.toString("MMM d");
        painter.drawText(QRect(badgeWidth + 24, 108, width() - badgeWidth - 40, 21), Qt::AlignRight | Qt::AlignVCenter, due);

        painter.drawPixmap(15, 140, avatarIcon(card_.assignee, 22).pixmap(22, 22));
        painter.setPen(muted());
        painter.drawText(QRect(44, 140, width() - 62, 22), Qt::AlignVCenter,
                         QFontMetrics(small).elidedText(card_.assignee, Qt::ElideRight, width() - 62));
        if (fileHover_) {
            QColor overlay = accentColor();
            overlay.setAlpha(dark() ? 225 : 242);
            painter.setBrush(overlay);
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(bounds, 10, 10);
            painter.setPen(Qt::white);
            painter.setFont(title);
            painter.drawText(rect().adjusted(18, 20, -18, -20), Qt::AlignCenter | Qt::TextWordWrap,
                             "Attach files\nand review completion");
        }
    }

    void mousePressEvent(QMouseEvent* event) override {
        board_->setSelectedTask(card_.id);
        setFocus(Qt::MouseFocusReason);
        dragStart_ = event->position().toPoint();
        if (event->button() == Qt::RightButton ||
            (event->button() == Qt::LeftButton && dragStart_.x() >= width() - 40 && dragStart_.y() <= 36)) {
            board_->requestContextMenu(card_.id, event->globalPosition().toPoint());
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override {
        if (!(event->buttons() & Qt::LeftButton) ||
            (event->position().toPoint() - dragStart_).manhattanLength() < QApplication::startDragDistance()) return;
        auto* drag = new QDrag(this);
        auto* mime = new QMimeData;
        mime->setData(taskMime, board_->dragPayload(card_.id));
        drag->setMimeData(mime);
        drag->setPixmap(grab());
        drag->setHotSpot(dragStart_);
        drag->exec(Qt::MoveAction);
    }

    void mouseDoubleClickEvent(QMouseEvent*) override { board_->requestOpen(card_.id); }

    void focusInEvent(QFocusEvent* event) override {
        board_->setSelectedTask(card_.id);
        update();
        QWidget::focusInEvent(event);
    }

    void focusOutEvent(QFocusEvent* event) override { update(); QWidget::focusOutEvent(event); }

    void keyPressEvent(QKeyEvent* event) override {
        if (event->key() == Qt::Key_Menu || (event->key() == Qt::Key_F10 && event->modifiers() & Qt::ShiftModifier)) {
            board_->requestContextMenu(card_.id, mapToGlobal(QPoint(width() - 20, 30)));
        } else if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) {
            board_->requestOpen(card_.id);
        } else if ((event->key() == Qt::Key_Left || event->key() == Qt::Key_Right) && event->modifiers() & Qt::AltModifier) {
            const int current = static_cast<int>(card_.status == TaskStatus::Overdue ? TaskStatus::Pending : card_.status);
            const int next = current + (event->key() == Qt::Key_Right ? 1 : -1);
            if (next >= 0 && next <= 2) board_->requestMove(card_.id, static_cast<TaskStatus>(next));
        } else {
            QWidget::keyPressEvent(event);
            return;
        }
        event->accept();
    }

    void dragEnterEvent(QDragEnterEvent* event) override {
        if (!TaskBoardWidget::localFiles(event->mimeData()).isEmpty()) {
            fileHover_ = true;
            update();
            event->setDropAction(Qt::CopyAction);
            event->accept();
        } else if (board_->draggedTask(event->mimeData()) > 0) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        } else event->ignore();
    }

    void dragMoveEvent(QDragMoveEvent* event) override {
        if (!TaskBoardWidget::localFiles(event->mimeData()).isEmpty()) { event->setDropAction(Qt::CopyAction); event->accept(); }
        else if (board_->draggedTask(event->mimeData()) > 0) { event->setDropAction(Qt::MoveAction); event->accept(); }
        else event->ignore();
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override { fileHover_ = false; update(); event->accept(); }

    void dropEvent(QDropEvent* event) override {
        fileHover_ = false;
        update();
        if (board_->requestFileDrop(card_.id, TaskBoardWidget::localFiles(event->mimeData()))) {
            event->setDropAction(Qt::CopyAction);
            event->accept();
        } else if (board_->requestMove(board_->draggedTask(event->mimeData()), card_.status)) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        } else event->ignore();
    }

private:
    TaskBoardWidget* board_;
    TaskBoardWidget::Card card_;
    QPoint dragStart_;
    bool fileHover_ = false;
};

class BoardColumn final : public QFrame {
public:
    BoardColumn(TaskBoardWidget* board, TaskStatus status, QWidget* parent)
        : QFrame(parent), board_(board), status_(status) {
        setObjectName("taskBoardColumn" + QString::number(static_cast<int>(status)));
        setMinimumWidth(244);
        setAcceptDrops(true);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 12, 10, 10);
        layout->setSpacing(10);
        auto* header = new QHBoxLayout;
        auto* dot = new QLabel;
        dot->setFixedSize(8, 8);
        dot->setStyleSheet("background:" + statusColor(status).name() + ";border-radius:4px;");
        header->addWidget(dot);
        title_ = new QLabel;
        title_->setObjectName("sectionTitle");
        header->addWidget(title_);
        header->addStretch();
        layout->addLayout(header);
        auto* scroll = new QScrollArea(this);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setStyleSheet("QScrollArea { background:transparent; } QScrollArea > QWidget > QWidget { background:transparent; }");
        auto* content = new QWidget;
        content->setAcceptDrops(false);
        cards_ = new QVBoxLayout(content);
        cards_->setContentsMargins(0, 0, 0, 0);
        cards_->setSpacing(10);
        cards_->setAlignment(Qt::AlignTop);
        scroll->setWidget(content);
        layout->addWidget(scroll, 1);
    }

    void setTasks(const QList<TaskBoardWidget::Card>& cards) {
        while (auto* item = cards_->takeAt(0)) {
            if (auto* widget = item->widget()) { widget->hide(); widget->deleteLater(); }
            delete item;
        }
        title_->setText(label(status_) + "  (" + QString::number(cards.size()) + ')');
        for (const auto& card : cards) cards_->addWidget(new BoardCard(board_, card, this));
        if (cards.empty()) {
            auto* empty = new QLabel("No tasks\nDrop a task here to move it.");
            empty->setObjectName("mutedLabel");
            empty->setAlignment(Qt::AlignCenter);
            empty->setWordWrap(true);
            empty->setMinimumHeight(112);
            empty->setAttribute(Qt::WA_TransparentForMouseEvents);
            cards_->addWidget(empty);
        }
    }

protected:
    void paintEvent(QPaintEvent* event) override {
        QFrame::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setBrush(QColor(dark() ? "#141E29" : "#EDF4FB"));
        painter.setPen(QPen(hover_ ? accentColor() : QColor(dark() ? "#263649" : "#DFE9F4"), hover_ ? 2 : 1));
        painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 12, 12);
    }

    void dragEnterEvent(QDragEnterEvent* event) override {
        if (board_->draggedTask(event->mimeData()) > 0) {
            hover_ = true;
            update();
            event->setDropAction(Qt::MoveAction);
            event->accept();
        } else event->ignore();
    }
    void dragMoveEvent(QDragMoveEvent* event) override {
        if (board_->draggedTask(event->mimeData()) > 0) { event->setDropAction(Qt::MoveAction); event->accept(); }
        else event->ignore();
    }
    void dragLeaveEvent(QDragLeaveEvent* event) override { hover_ = false; update(); event->accept(); }
    void dropEvent(QDropEvent* event) override {
        hover_ = false;
        update();
        if (board_->requestMove(board_->draggedTask(event->mimeData()), status_)) {
            event->setDropAction(Qt::MoveAction);
            event->accept();
        } else event->ignore();
    }
private:
    TaskBoardWidget* board_;
    TaskStatus status_;
    QLabel* title_;
    QVBoxLayout* cards_;
    bool hover_ = false;
};

} // namespace

TaskBoardWidget::TaskBoardWidget(QWidget* parent) : QWidget(parent), dragToken_(QUuid::createUuid().toByteArray()) {
    setObjectName("taskBoard");
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background:transparent; } QScrollArea > QWidget > QWidget { background:transparent; }");
    auto* content = new QWidget;
    auto* columns = new QHBoxLayout(content);
    columns->setContentsMargins(0, 0, 0, 0);
    columns->setSpacing(14);
    for (TaskStatus status : {TaskStatus::Pending, TaskStatus::InProgress, TaskStatus::Completed}) {
        auto* column = new BoardColumn(this, status, content);
        columns_.append(column);
        columns->addWidget(column, 1);
    }
    scroll->setWidget(content);
    layout->addWidget(scroll);
    setTasks({});
}

void TaskBoardWidget::setTasks(const QList<Card>& tasks) {
    QList<Card> next;
    QSet<int> seen;
    for (auto task : tasks) {
        if (task.id <= 0 || seen.contains(task.id)) continue;
        seen.insert(task.id);
        if (!validColumn(task.status)) { task.status = TaskStatus::Pending; task.overdue = true; }
        next.append(task);
    }
    const auto equal = [](const Card& left, const Card& right) {
        return left.id == right.id && left.title == right.title && left.project == right.project &&
               left.description == right.description && left.priority == right.priority &&
               left.assignee == right.assignee && left.dueDate == right.dueDate &&
               left.status == right.status && left.overdue == right.overdue;
    };
    // Polling/sync refreshes normally have no changes. Keep live cards, scroll
    // positions, focus, and an in-progress drag intact in that common case.
    if (initialized_ && next.size() == tasks_.size() && std::equal(next.begin(), next.end(), tasks_.begin(), equal)) return;
    const auto* focused = QApplication::focusWidget();
    const bool restoreFocus = focused && isAncestorOf(focused) && focused->objectName().startsWith("taskBoardCard");
    tasks_ = next;
    initialized_ = true;
    if (!containsTask(selectedTaskId_)) selectedTaskId_ = 0;
    for (int index = 0; index < columns_.size(); ++index) {
        QList<Card> values;
        for (const auto& task : tasks_) if (static_cast<int>(task.status) == index) values.append(task);
        static_cast<BoardColumn*>(columns_[index])->setTasks(values);
    }
    if (restoreFocus && selectedTaskId_ > 0) {
        // Old cards are hidden/deferred-deleted; only focus the replacement.
        for (auto* card : findChildren<QWidget*>("taskBoardCard" + QString::number(selectedTaskId_)))
            if (!card->isHidden()) card->setFocus(Qt::OtherFocusReason);
    }
}

void TaskBoardWidget::setSelectedTask(int id) {
    if (id != 0 && !containsTask(id)) return;
    const bool changed = selectedTaskId_ != id;
    selectedTaskId_ = id;
    for (auto* card : findChildren<QWidget*>()) if (card->objectName().startsWith("taskBoardCard")) card->update();
    if (changed && id > 0 && onSelected) onSelected(id);
}

int TaskBoardWidget::taskCount(TaskStatus status) const {
    return static_cast<int>(std::count_if(tasks_.begin(), tasks_.end(), [status](const Card& card) { return card.status == status; }));
}

bool TaskBoardWidget::containsTask(int id) const {
    return id > 0 && std::any_of(tasks_.begin(), tasks_.end(), [id](const Card& card) { return card.id == id; });
}

bool TaskBoardWidget::requestMove(int id, TaskStatus status) {
    if (!validColumn(status) || !containsTask(id) || !onStatusRequested) return false;
    const auto found = std::find_if(tasks_.begin(), tasks_.end(), [id](const Card& card) { return card.id == id; });
    if (found->status == status) return false;
    setSelectedTask(id);
    QTimer::singleShot(0, this, [this, id, status] { if (containsTask(id) && onStatusRequested) onStatusRequested(id, status); });
    return true;
}

QStringList TaskBoardWidget::localFiles(const QMimeData* mime) {
    QStringList files;
    if (!mime || !mime->hasUrls()) return files;
    for (const auto& url : mime->urls()) {
        if (!url.isLocalFile()) continue;
        const QFileInfo info(url.toLocalFile());
        if (!info.isFile() || !info.isReadable()) continue;
        const auto path = info.canonicalFilePath();
        if (!path.isEmpty() && !files.contains(path)) files.append(path);
    }
    return files;
}

bool TaskBoardWidget::requestFileDrop(int id, const QStringList& paths) {
    if (!containsTask(id) || !onFilesDropped) return false;
    QMimeData mime;
    QList<QUrl> urls;
    for (const auto& path : paths) urls.append(QUrl::fromLocalFile(path));
    mime.setUrls(urls);
    const auto files = localFiles(&mime);
    if (files.isEmpty()) return false;
    setSelectedTask(id);
    QTimer::singleShot(0, this, [this, id, files] { if (containsTask(id) && onFilesDropped) onFilesDropped(id, files); });
    return true;
}

void TaskBoardWidget::requestContextMenu(int id, const QPoint& position) {
    if (!containsTask(id) || !onContextRequested) return;
    setSelectedTask(id);
    QTimer::singleShot(0, this, [this, id, position] { if (containsTask(id) && onContextRequested) onContextRequested(id, position); });
}

void TaskBoardWidget::requestOpen(int id) {
    if (!containsTask(id) || !onOpenRequested) return;
    setSelectedTask(id);
    QTimer::singleShot(0, this, [this, id] { if (containsTask(id) && onOpenRequested) onOpenRequested(id); });
}

QByteArray TaskBoardWidget::dragPayload(int id) const { return containsTask(id) ? dragToken_ + ':' + QByteArray::number(id) : QByteArray(); }

int TaskBoardWidget::draggedTask(const QMimeData* mime) const {
    if (!mime || !mime->hasFormat(taskMime)) return 0;
    const auto value = mime->data(taskMime);
    const auto prefix = dragToken_ + ':';
    if (!value.startsWith(prefix)) return 0;
    bool valid = false;
    const int id = value.mid(prefix.size()).toInt(&valid);
    return valid && containsTask(id) ? id : 0;
}

} // namespace teamsync::ui
