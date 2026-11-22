#include "TeamSyncGui.h"

#include "PremiumWidgets.h"
#include "TaskBoardWidget.h"
#include "ThemeManager.h"

#include <QAbstractItemModel>
#include <QAction>
#include <QApplication>
#include <QButtonGroup>
#include <QComboBox>
#include <QDate>
#include <QEvent>
#include <QFrame>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSet>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace teamsync {
namespace {

QString text(const std::string& value) { return QString::fromUtf8(value.c_str()); }

std::string utf8(const QString& value) {
    const auto bytes = value.toUtf8();
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

QDate date(const Date& value) { return QDate(value.year(), value.month(), value.day()); }

class SearchIconPaletteWatcher final : public QObject {
public:
    SearchIconPaletteWatcher(QLineEdit* field, QAction* icon) : QObject(field), icon_(icon) {
        qApp->installEventFilter(this);
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == qApp && event->type() == QEvent::ApplicationPaletteChange)
            icon_->setIcon(ThemeManager::icon("search", qApp->property("darkTheme").toBool()));
        return QObject::eventFilter(watched, event);
    }
private:
    QAction* icon_;
};

QPushButton* filterButton(const QString& label, const QString& name, QWidget* parent) {
    auto* result = new ui::AnimatedButton(label, parent);
    result->setObjectName(name);
    result->setProperty("filterTab", true);
    result->setCheckable(true);
    result->setAutoDefault(false);
    result->setCursor(Qt::PointingHandCursor);
    result->setFocusPolicy(Qt::StrongFocus);
    result->setMinimumHeight(32);
    return result;
}

void prepareSearch(QLineEdit* field, const QString& placeholder) {
    field->setPlaceholderText(placeholder);
    field->setAccessibleName(placeholder);
    field->setClearButtonEnabled(true);
    field->setProperty("filterSearch", true);
    field->setMinimumWidth(130);
    field->setMaximumWidth(250);
    auto* icon = field->addAction(ThemeManager::icon("search", qApp->property("darkTheme").toBool()), QLineEdit::LeadingPosition);
    new SearchIconPaletteWatcher(field, icon);
}

class WorkTableEmptyState final : public QLabel {
public:
    WorkTableEmptyState(QTableWidget* table, const QString& message)
        : QLabel(message, table->viewport()), table_(table) {
        setObjectName("emptyState");
        setAlignment(Qt::AlignCenter);
        setWordWrap(true);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        table->viewport()->installEventFilter(this);
        const auto update = [this] {
            setVisible(table_->rowCount() == 0);
            setGeometry(table_->viewport()->rect().adjusted(24, 20, -24, -20));
        };
        QObject::connect(table->model(), &QAbstractItemModel::rowsInserted, this, update);
        QObject::connect(table->model(), &QAbstractItemModel::rowsRemoved, this, update);
        QObject::connect(table->model(), &QAbstractItemModel::modelReset, this, update);
        update();
    }
protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == table_->viewport() && event->type() == QEvent::Resize)
            setGeometry(table_->viewport()->rect().adjusted(24, 20, -24, -20));
        return QLabel::eventFilter(watched, event);
    }
private:
    QTableWidget* table_;
};

QTableWidget* workTable(const QStringList& headings, QFrame* panel, const QString& emptyText) {
    auto* result = new QTableWidget(0, headings.size(), panel);
    result->setHorizontalHeaderLabels(headings);
    result->setProperty("premiumTable", true);
    result->setFrameShape(QFrame::NoFrame);
    result->setShowGrid(false);
    result->setAlternatingRowColors(false);
    result->setWordWrap(false);
    result->setTextElideMode(Qt::ElideRight);
    result->setSelectionBehavior(QAbstractItemView::SelectRows);
    result->setSelectionMode(QAbstractItemView::SingleSelection);
    result->setEditTriggers(QAbstractItemView::NoEditTriggers);
    result->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    result->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    result->setCornerButtonEnabled(false);
    result->verticalHeader()->hide();
    result->verticalHeader()->setDefaultSectionSize(40);
    result->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    result->horizontalHeader()->setMinimumSectionSize(30);
    result->horizontalHeader()->setFixedHeight(36);
    result->horizontalHeader()->setHighlightSections(false);
    result->horizontalHeader()->setStretchLastSection(false);
    result->setItemDelegate(new ui::ReferenceItemDelegate(result));
    new WorkTableEmptyState(result, emptyText);
    return result;
}

QTableWidgetItem* cell(const QString& value, const QString& tooltip = {}) {
    auto* result = new QTableWidgetItem(value);
    result->setToolTip(tooltip.isEmpty() ? value : tooltip);
    return result;
}

void kind(QTableWidgetItem* item, const ui::CellKind value) {
    item->setData(ui::KindRole, static_cast<int>(value));
}

QPushButton* rowMenuButton(QTableWidget* table, const int row, QMenu* menu) {
    auto* more = new ui::AnimatedButton(QString::fromUtf8("\xE2\x8B\xAE"), table);
    more->setObjectName("rowContextButton");
    more->setProperty("iconOnly", true);
    more->setAccessibleName("More actions for this row");
    more->setCursor(Qt::PointingHandCursor);
    more->setAutoDefault(false);
    more->setFocusPolicy(Qt::StrongFocus);
    more->setFixedSize(30, 30);
    QObject::connect(more, &QPushButton::pressed, table, [table, row] { table->setCurrentCell(row, 1); });
    // Select before Qt opens the menu, so an action always targets this row.
    more->setMenu(menu);
    auto* container = new QWidget(table);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 4, 0);
    layout->setAlignment(Qt::AlignCenter);
    layout->addWidget(more);
    table->setCellWidget(row, table->columnCount() - 1, container);
    return more;
}

void restoreCurrentRow(QTableWidget* table, const int id) {
    if (id <= 0) return;
    for (int row = 0; row < table->rowCount(); ++row) {
        if (table->item(row, 0)->data(Qt::UserRole).toInt() == id) {
            // Table -> board selection feedback must not move keyboard focus
            // out of the checkbox column that the user just selected.
            if (table->currentRow() == row) return;
            table->setCurrentCell(row, 1);
            return;
        }
    }
}

} // namespace

QWidget* TeamSyncGui::buildProjectsPage() {
    auto* page = new QWidget;
    page->setObjectName("projectsPage");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    auto* filters = new QHBoxLayout;
    filters->setSpacing(8);
    auto* group = new QButtonGroup(page);
    group->setExclusive(true);
    for (int index = 0; index < 3; ++index) {
        const QString label = QStringList{"All", "Active", "Completed"}.at(index);
        auto* filter = filterButton(label + " (0)", "projectFilter" + QString::number(index), page);
        group->addButton(filter, index);
        filters->addWidget(filter);
        filter->setChecked(index == 0);
        connect(filter, &QPushButton::clicked, this, [this, index] {
            projectsTable_->setProperty("scope", index);
            refreshProjects();
        });
    }
    filters->addStretch();
    projectSearch_ = new QLineEdit(page);
    projectSearch_->setObjectName("projectSearch");
    prepareSearch(projectSearch_, "Search projects...");
    filters->addWidget(projectSearch_);
    layout->addLayout(filters);

    auto* panel = new QFrame(page);
    panel->setObjectName("tablePanel");
    ui::softShadow(panel, 20, 4);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    projectsTable_ = workTable({"ID", "Name", "Description", "Progress", "Team", "Status", ""}, panel,
        "No projects here yet. Create a project for your team, or try another filter.");
    projectsTable_->setObjectName("projectsTable");
    projectsTable_->setProperty("scope", 0);
    projectsTable_->setColumnHidden(0, true);
    auto* header = projectsTable_->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Fixed);
    header->setSectionResizeMode(2, QHeaderView::Stretch);
    projectsTable_->setColumnWidth(1, 190);
    projectsTable_->setColumnWidth(3, 132);
    projectsTable_->setColumnWidth(4, 104);
    projectsTable_->setColumnWidth(5, 110);
    projectsTable_->setColumnWidth(6, 36);
    panelLayout->addWidget(projectsTable_);
    layout->addWidget(panel, 1);

    auto* footer = new QLabel(page);
    footer->setObjectName("projectTableSummary");
    footer->setProperty("secondary", true);
    layout->addWidget(footer);
    connect(projectSearch_, &QLineEdit::textChanged, this, [this] { refreshProjects(); });
    connect(projectsTable_, &QTableWidget::itemDoubleClicked, this, [this] { showProjectDialog(true); });
    projectsTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(projectsTable_, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        const auto row = projectsTable_->indexAt(point).row();
        if (row < 0) return;
        projectsTable_->setCurrentCell(row, 1);
        if (auto* menu = projectsTable_->findChild<QMenu*>("projectContextMenu")) menu->popup(projectsTable_->viewport()->mapToGlobal(point));
    });
    auto* menu = new QMenu(projectsTable_);
    menu->setObjectName("projectContextMenu");
    menu->addAction("Edit project", this, [this] { showProjectDialog(true); });
    menu->addSeparator();
    menu->addAction("Archive project", this, [this] { archiveSelectedProject(); });
    return page;
}

QWidget* TeamSyncGui::buildTasksPage() {
    auto* page = new QWidget;
    page->setObjectName("tasksPage");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 8, 24, 24);
    layout->setSpacing(16);

    auto* filters = new QHBoxLayout;
    filters->setSpacing(6);
    auto* group = new QButtonGroup(page);
    group->setExclusive(true);
    for (int index = 0; index < 4; ++index) {
        const QString label = QStringList{"All", "My tasks", "Assigned to me", "Due soon"}.at(index);
        auto* filter = filterButton(label + " (0)", "taskScope" + QString::number(index), page);
        group->addButton(filter, index);
        filters->addWidget(filter);
        filter->setChecked(index == 0);
        if (index == 1) filter->setToolTip("Tasks you created or that are assigned to you");
        if (index == 2) filter->setToolTip("Tasks assigned to your account");
        if (index == 3) filter->setToolTip("Unfinished tasks due during the next seven days");
        connect(filter, &QPushButton::clicked, this, [this, index] {
            tasksTable_->setProperty("scope", index);
            refreshTasks();
        });
    }
    filters->addStretch();
    taskSearch_ = new QLineEdit(page);
    taskSearch_->setObjectName("taskSearch");
    prepareSearch(taskSearch_, "Search tasks...");
    filters->addWidget(taskSearch_, 1);
    taskFilter_ = new QComboBox(page);
    taskFilter_->setObjectName("taskStatusFilter");
    taskFilter_->setAccessibleName("Filter task status");
    taskFilter_->addItems({"All statuses", "Pending", "In progress", "Completed", "Overdue"});
    taskFilter_->setMinimumWidth(112);
    taskFilter_->setMaximumWidth(128);
    filters->addWidget(taskFilter_);
    layout->addLayout(filters);

    auto* viewOptions = new QHBoxLayout;
    viewOptions->setSpacing(6);
    auto* viewGroup = new QButtonGroup(page);
    viewGroup->setExclusive(true);
    for (int index = 0; index < 2; ++index) {
        auto* choice = filterButton(index == 0 ? "Table" : "Board",
                                    index == 0 ? "taskTableView" : "taskBoardView", page);
        choice->setAccessibleName(index == 0 ? "Show task table" : "Show task board");
        choice->setChecked(index == 0);
        viewGroup->addButton(choice, index);
        viewOptions->addWidget(choice);
        connect(choice, &QPushButton::clicked, this, [this, index] {
            taskViews_->setCurrentIndex(index);
            const int row = tasksTable_->currentRow();
            const int selected = row >= 0 && tasksTable_->item(row, 0)
                ? tasksTable_->item(row, 0)->data(Qt::UserRole).toInt() : 0;
            if (taskBoard_) taskBoard_->setSelectedTask(selected);
        });
    }
    viewOptions->addSpacing(8);
    auto* boardHint = new QLabel("Board: drag tasks between columns; drop files onto a task to finish with attachments.", page);
    boardHint->setObjectName("mutedLabel");
    boardHint->setWordWrap(true);
    viewOptions->addWidget(boardHint, 1);
    layout->addLayout(viewOptions);

    taskViews_ = new QStackedWidget(page);
    taskViews_->setObjectName("taskViews");

    auto* panel = new QFrame(page);
    panel->setObjectName("tablePanel");
    ui::softShadow(panel, 20, 4);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    tasksTable_ = workTable({"", "Task", "Project", "Priority", "Due date", "Status", ""}, panel,
        "No tasks match this view. Create a task in a project, or change your filters.");
    tasksTable_->setObjectName("tasksTable");
    tasksTable_->setProperty("scope", 0);
    auto* header = tasksTable_->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Fixed);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    tasksTable_->setColumnWidth(0, 36);
    tasksTable_->setColumnWidth(2, 144);
    tasksTable_->setColumnWidth(3, 88);
    tasksTable_->setColumnWidth(4, 124);
    tasksTable_->setColumnWidth(5, 116);
    tasksTable_->setColumnWidth(6, 36);
    panelLayout->addWidget(tasksTable_);
    taskViews_->addWidget(panel);
    taskBoard_ = new ui::TaskBoardWidget(page);
    taskViews_->addWidget(taskBoard_);
    layout->addWidget(taskViews_, 1);
    taskBoard_->onSelected = [this](const int id) { restoreCurrentRow(tasksTable_, id); };
    taskBoard_->onOpenRequested = [this](const int id) {
        restoreCurrentRow(tasksTable_, id);
        if (selectedId(tasksTable_) != id) return;
        showTaskDialog(true);
    };
    taskBoard_->onStatusRequested = [this](const int id, const TaskStatus status) {
        restoreCurrentRow(tasksTable_, id);
        if (selectedId(tasksTable_) != id) return;
        if (status == TaskStatus::Completed) completeSelectedTask();
        else if (status == TaskStatus::Pending) reopenSelectedTask();
        else if (status == TaskStatus::InProgress) {
            // Moving a completed task back into work is the same authorized
            // reopen/start operation exposed by the regular task actions.
            if (perform([this, id] {
                if (service_->task(id).workflowStatus() == TaskStatus::Completed)
                    service_->reopenTask(currentUserId(), id);
                service_->startTask(currentUserId(), id);
            }, "Task is now in progress")) {
                refreshTasks();
                refreshProjects();
            }
        }
    };
    taskBoard_->onFilesDropped = [this](const int id, const QStringList& paths) {
        restoreCurrentRow(tasksTable_, id);
        if (selectedId(tasksTable_) != id) return;
        pendingTaskFiles_ = paths;
        completeSelectedTask();
    };
    taskBoard_->onContextRequested = [this](const int id, const QPoint& position) {
        restoreCurrentRow(tasksTable_, id);
        if (selectedId(tasksTable_) != id) return;
        if (auto* menu = tasksTable_->findChild<QMenu*>("taskContextMenu")) menu->popup(position);
    };
    connect(tasksTable_, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (taskBoard_) taskBoard_->setSelectedTask(row >= 0 && tasksTable_->item(row, 0)
            ? tasksTable_->item(row, 0)->data(Qt::UserRole).toInt() : 0);
    });

    auto* footer = new QHBoxLayout;
    auto* summary = new QLabel(page);
    summary->setObjectName("taskTableSummary");
    summary->setProperty("secondary", true);
    footer->addWidget(summary);
    footer->addStretch();
    auto* clear = new ui::AnimatedButton("Clear selection", page);
    clear->setObjectName("linkButton");
    clear->setProperty("taskClearSelection", true);
    clear->setEnabled(false);
    clear->setAutoDefault(false);
    footer->addWidget(clear);
    layout->addLayout(footer);
    connect(clear, &QPushButton::clicked, this, [this] {
        for (int row = 0; row < tasksTable_->rowCount(); ++row) tasksTable_->item(row, 0)->setCheckState(Qt::Unchecked);
        tasksTable_->clearSelection();
        tasksTable_->setCurrentCell(-1, -1);
    });
    connect(tasksTable_, &QTableWidget::itemChanged, this, [this, summary, clear](QTableWidgetItem* changed) {
        if (!changed || changed->column() != 0) return;
        int checked = 0;
        for (int row = 0; row < tasksTable_->rowCount(); ++row) {
            if (tasksTable_->item(row, 0) && tasksTable_->item(row, 0)->checkState() == Qt::Checked) ++checked;
        }
        clear->setEnabled(checked > 0);
        summary->setText(QString::number(tasksTable_->rowCount()) + " tasks" +
            (checked ? "  \xC2\xB7  " + QString::number(checked) + " selected" : QString()));
    });
    connect(taskSearch_, &QLineEdit::textChanged, this, [this] { refreshTasks(); });
    connect(taskFilter_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { refreshTasks(); });
    connect(tasksTable_, &QTableWidget::itemDoubleClicked, this, [this] { showTaskDialog(true); });
    tasksTable_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tasksTable_, &QWidget::customContextMenuRequested, this, [this](const QPoint& point) {
        const int row = tasksTable_->indexAt(point).row();
        if (row < 0) return;
        tasksTable_->setCurrentCell(row, 1);
        if (auto* menu = tasksTable_->findChild<QMenu*>("taskContextMenu")) menu->popup(tasksTable_->viewport()->mapToGlobal(point));
    });
    auto* menu = new QMenu(tasksTable_);
    menu->setObjectName("taskContextMenu");
    menu->addAction("Start task", this, [this] { startSelectedTask(); });
    menu->addAction("Mark complete", this, [this] { completeSelectedTask(); });
    menu->addAction("Edit task", this, [this] { showTaskDialog(true); });
    menu->addSeparator();
    menu->addAction("Completion details", this, [this] { showCompletionDetails(); });
    menu->addAction("Comments", this, [this] { showTaskComments(); });
    menu->addAction("Recommend member", this, [this] { showRecommendations(); });
    menu->addAction("Reopen task", this, [this] { reopenSelectedTask(); });
    menu->addSeparator();
    menu->addAction("Delete task", this, [this] { deleteSelectedTask(); });
    return page;
}

void TeamSyncGui::refreshProjects() {
    if (!currentUser_ || !projectsTable_) return;
    perform([this] {
        const auto all = service_->projectsForUser(currentUserId());
        int active = 0;
        int completed = 0;
        for (const auto* project : all) {
            if (project->status() == ProjectStatus::Completed) ++completed;
            else if (project->status() != ProjectStatus::Archived) ++active;
        }
        const QList<int> counts{static_cast<int>(all.size()), active, completed};
        const QStringList names{"All", "Active", "Completed"};
        for (int index = 0; index < 3; ++index) {
            if (auto* filter = findChild<QPushButton*>("projectFilter" + QString::number(index)))
                filter->setText(names[index] + " (" + QString::number(counts[index]) + ')');
        }
        const int selected = projectsTable_->currentRow() >= 0 && projectsTable_->item(projectsTable_->currentRow(), 0)
            ? projectsTable_->item(projectsTable_->currentRow(), 0)->data(Qt::UserRole).toInt() : 0;
        const QSignalBlocker block(projectsTable_);
        projectsTable_->setRowCount(0);
        const int scope = projectsTable_->property("scope").toInt();
        const auto values = service_->searchProjects(currentUserId(), utf8(projectSearch_->text()));
        auto* menu = projectsTable_->findChild<QMenu*>("projectContextMenu");
        for (const auto* value : values) {
            const bool finished = value->status() == ProjectStatus::Completed;
            if (scope == 1 && (finished || value->status() == ProjectStatus::Archived)) continue;
            if (scope == 2 && !finished) continue;
            const int row = projectsTable_->rowCount();
            projectsTable_->insertRow(row);
            auto* id = cell(QString::number(value->id()));
            id->setData(Qt::UserRole, value->id());
            projectsTable_->setItem(row, 0, id);
            auto* title = cell(text(value->title()));
            auto font = title->font();
            font.setWeight(QFont::DemiBold);
            title->setFont(font);
            projectsTable_->setItem(row, 1, title);
            projectsTable_->setItem(row, 2, cell(text(value->description())));
            int done = 0;
            int total = 0;
            for (const auto* task : service_->tasksForTeam(value->teamId())) {
                if (task->projectId() != value->id()) continue;
                ++total;
                if (task->status() == TaskStatus::Completed) ++done;
            }
            const int progress = total ? qRound(100.0 * done / total) : 0;
            auto* progressItem = cell(QString::number(progress) + '%', QString::number(done) + " of " + QString::number(total) + " tasks completed");
            kind(progressItem, ui::CellKind::Progress);
            progressItem->setData(ui::NumberRole, progress);
            projectsTable_->setItem(row, 3, progressItem);
            const auto& team = service_->team(value->teamId());
            QStringList memberNames;
            for (const int memberId : team.memberIds()) memberNames << text(service_->user(memberId).fullName());
            auto* members = cell(text(team.name()), text(team.name()) + "\n" + memberNames.join(", "));
            kind(members, ui::CellKind::Avatars);
            members->setData(ui::NamesRole, memberNames);
            projectsTable_->setItem(row, 4, members);
            auto* status = cell(finished ? "Completed" : value->status() == ProjectStatus::Archived ? "Archived" : "Active",
                                text(toString(value->status())));
            kind(status, ui::CellKind::Status);
            projectsTable_->setItem(row, 5, status);
            projectsTable_->setItem(row, 6, cell({}));
            rowMenuButton(projectsTable_, row, menu);
        }
        restoreCurrentRow(projectsTable_, selected);
        if (auto* summary = findChild<QLabel*>("projectTableSummary"))
            summary->setText(QString::number(projectsTable_->rowCount()) + (projectsTable_->rowCount() == 1 ? " project" : " projects"));
    });
}

void TeamSyncGui::refreshTasks() {
    if (!currentUser_ || !tasksTable_) return;
    perform([this] {
        const auto all = service_->searchTasks(currentUserId(), "");
        const auto dueSoon = [](const Task* value) {
            const int days = QDate::currentDate().daysTo(date(value->dueDate()));
            return value->status() != TaskStatus::Completed && days >= 0 && days <= 7;
        };
        const auto myTask = [this](const Task* value) {
            return value->createdBy() == currentUserId() || value->assignedUserId() == currentUserId();
        };
        const auto assigned = [this](const Task* value) { return value->assignedUserId() == currentUserId(); };
        const QList<int> counts{static_cast<int>(all.size()),
            static_cast<int>(std::count_if(all.begin(), all.end(), myTask)),
            static_cast<int>(std::count_if(all.begin(), all.end(), assigned)),
            static_cast<int>(std::count_if(all.begin(), all.end(), dueSoon))};
        const QStringList names{"All", "My tasks", "Assigned to me", "Due soon"};
        for (int index = 0; index < 4; ++index) {
            if (auto* filter = findChild<QPushButton*>("taskScope" + QString::number(index)))
                filter->setText(names[index] + " (" + QString::number(counts[index]) + ')');
        }
        QSet<int> checked;
        const int selected = tasksTable_->currentRow() >= 0 && tasksTable_->item(tasksTable_->currentRow(), 0)
            ? tasksTable_->item(tasksTable_->currentRow(), 0)->data(Qt::UserRole).toInt() : 0;
        for (int row = 0; row < tasksTable_->rowCount(); ++row) {
            if (tasksTable_->item(row, 0)->checkState() == Qt::Checked)
                checked.insert(tasksTable_->item(row, 0)->data(Qt::UserRole).toInt());
        }
        const QSignalBlocker block(tasksTable_);
        tasksTable_->setRowCount(0);
        const int scope = tasksTable_->property("scope").toInt();
        const int statusFilter = taskFilter_->currentIndex();
        auto* menu = tasksTable_->findChild<QMenu*>("taskContextMenu");
        QList<ui::TaskBoardWidget::Card> boardTasks;
        for (const auto* value : service_->searchTasks(currentUserId(), utf8(taskSearch_->text()))) {
            if (scope == 1 && !myTask(value)) continue;
            if (scope == 2 && !assigned(value)) continue;
            if (scope == 3 && !dueSoon(value)) continue;
            if (statusFilter > 0 && value->status() != static_cast<TaskStatus>(statusFilter - 1)) continue;
            const int row = tasksTable_->rowCount();
            tasksTable_->insertRow(row);
            auto* id = cell({}, "Select task: " + text(value->title()));
            id->setData(Qt::UserRole, value->id());
            id->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsUserCheckable);
            id->setCheckState(checked.contains(value->id()) ? Qt::Checked : Qt::Unchecked);
            tasksTable_->setItem(row, 0, id);
            auto* title = cell(text(value->title()), text(value->title()) + "\nAssigned to " +
                text(service_->user(value->assignedUserId()).fullName()) + "\n" + text(value->description()));
            auto font = title->font();
            font.setWeight(QFont::Medium);
            title->setFont(font);
            tasksTable_->setItem(row, 1, title);
            tasksTable_->setItem(row, 2, cell(text(service_->project(value->projectId()).title())));
            auto* priority = cell(text(toString(value->priority())));
            kind(priority, ui::CellKind::Priority);
            tasksTable_->setItem(row, 3, priority);
            tasksTable_->setItem(row, 4, cell(date(value->dueDate()).toString("MMM d, yyyy"), text(value->dueDate().toString())));
            auto* status = cell(text(toString(value->status())));
            kind(status, ui::CellKind::Status);
            tasksTable_->setItem(row, 5, status);
            tasksTable_->setItem(row, 6, cell({}));
            rowMenuButton(tasksTable_, row, menu);
            ui::TaskBoardWidget::Card boardTask;
            boardTask.id = value->id();
            boardTask.title = text(value->title());
            boardTask.project = text(service_->project(value->projectId()).title());
            boardTask.description = text(value->description());
            boardTask.priority = value->priority();
            boardTask.assignee = text(service_->user(value->assignedUserId()).fullName());
            boardTask.dueDate = date(value->dueDate());
            boardTask.status = value->workflowStatus();
            boardTask.overdue = value->status() == TaskStatus::Overdue;
            boardTasks.append(boardTask);
        }
        restoreCurrentRow(tasksTable_, selected);
        if (taskBoard_) {
            taskBoard_->setTasks(boardTasks);
            taskBoard_->setSelectedTask(selected);
        }
        int selectedCount = 0;
        for (int row = 0; row < tasksTable_->rowCount(); ++row)
            if (tasksTable_->item(row, 0)->checkState() == Qt::Checked) ++selectedCount;
        for (auto* control : findChildren<QPushButton*>()) {
            if (control->property("taskClearSelection").toBool()) control->setEnabled(selectedCount > 0);
        }
        if (auto* summary = findChild<QLabel*>("taskTableSummary")) {
            summary->setText(QString::number(tasksTable_->rowCount()) + (tasksTable_->rowCount() == 1 ? " task" : " tasks") +
                (selectedCount ? "  \xC2\xB7  " + QString::number(selectedCount) + " selected" : QString()));
        }
    });
}

} // namespace teamsync
