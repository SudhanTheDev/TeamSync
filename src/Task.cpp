#include "Task.h"

#include "Exceptions.h"
#include "Utils.h"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <utility>

namespace teamsync {

int Task::nextId_ = 10001;

std::string toString(const TaskPriority priority) {
    switch (priority) {
        case TaskPriority::Low: return "Low";
        case TaskPriority::Medium: return "Medium";
        case TaskPriority::High: return "High";
        case TaskPriority::Urgent: return "Urgent";
    }
    return "Unknown";
}

std::string toString(const TaskStatus status) {
    switch (status) {
        case TaskStatus::Pending: return "Pending";
        case TaskStatus::InProgress: return "In Progress";
        case TaskStatus::Completed: return "Completed";
        case TaskStatus::Overdue: return "Overdue";
    }
    return "Unknown";
}

std::string toString(const TaskType type) {
    switch (type) {
        case TaskType::General: return "General";
        case TaskType::Development: return "Development";
        case TaskType::Research: return "Research";
        case TaskType::Documentation: return "Documentation";
    }
    return "Unknown";
}

TaskPriority taskPriorityFromString(const std::string& value) {
    if (value == "Low") return TaskPriority::Low;
    if (value == "Medium") return TaskPriority::Medium;
    if (value == "High") return TaskPriority::High;
    if (value == "Urgent") return TaskPriority::Urgent;
    throw ValidationError("Unknown task priority: " + value);
}

TaskStatus taskStatusFromString(const std::string& value) {
    if (value == "Pending") return TaskStatus::Pending;
    if (value == "In Progress") return TaskStatus::InProgress;
    if (value == "Completed") return TaskStatus::Completed;
    if (value == "Overdue") return TaskStatus::Overdue;
    throw ValidationError("Unknown task status: " + value);
}

TaskType taskTypeFromString(const std::string& value) {
    if (value == "General") return TaskType::General;
    if (value == "Development") return TaskType::Development;
    if (value == "Research") return TaskType::Research;
    if (value == "Documentation") return TaskType::Documentation;
    throw ValidationError("Unknown task type: " + value);
}

Task::Task(const int id, const int projectId, const int teamId, std::string title,
           std::string description, const int createdBy, const int assignedUserId,
           const TaskPriority priority, const TaskStatus status, const Date creationDate,
           const Date dueDate, std::optional<Date> completionDate,
           std::string requiredSkill, std::vector<std::string> comments,
           std::string completionNote)
    : id_(id), projectId_(projectId), teamId_(teamId), title_(std::move(title)),
      description_(std::move(description)), createdBy_(createdBy), assignedUserId_(assignedUserId),
      priority_(priority), status_(status), creationDate_(creationDate), dueDate_(dueDate),
      completionDate_(completionDate), requiredSkill_(std::move(requiredSkill)),
      comments_(std::move(comments)), completionNote_(std::move(completionNote)) {
    setTitle(title_);
    setDueDate(dueDate_);
    if (status_ == TaskStatus::Overdue) status_ = TaskStatus::InProgress;
    if (status_ != TaskStatus::Completed) completionDate_.reset();
    observeId(id_);
}

TaskStatus Task::status() const {
    if (status_ != TaskStatus::Completed && dueDate_ < Date::today()) return TaskStatus::Overdue;
    return status_;
}

void Task::setTitle(const std::string& value) {
    const auto cleaned = util::trim(value);
    if (cleaned.empty()) throw ValidationError("Task title cannot be empty.");
    title_ = cleaned;
}

void Task::setDueDate(const Date& value) {
    if (value < creationDate_) throw ValidationError("Task due date cannot be before its creation date.");
    dueDate_ = value;
}

void Task::start() {
    if (status_ == TaskStatus::Completed) throw ValidationError("Reopen the completed task before starting it again.");
    status_ = TaskStatus::InProgress;
}

void Task::markCompleted(const std::string& note, const Date& date) {
    status_ = TaskStatus::Completed;
    completionDate_ = date;
    completionNote_ = util::trim(note);
}

void Task::reopen() {
    completionDate_.reset();
    status_ = TaskStatus::Pending;
    completionNote_.clear();
}

void Task::addComment(const std::string& comment) {
    const auto cleaned = util::trim(comment);
    if (cleaned.empty()) throw ValidationError("Task comment cannot be empty.");
    comments_.push_back(util::timestampNow() + " - " + cleaned);
}

void Task::displayDetails(std::ostream& out) const {
    out << "Task #" << id_ << ": " << title_ << "\n"
        << "  Type: " << toString(type()) << " | Priority: " << toString(priority_)
        << " | Status: " << toString(status()) << "\n"
        << "  Project: " << projectId_ << " | Team: " << teamId_
        << " | Assigned user: " << assignedUserId_ << "\n"
        << "  Created: " << creationDate_ << " | Due: " << dueDate_ << "\n"
        << "  Required skill: " << (requiredSkill_.empty() ? "None" : requiredSkill_) << "\n"
        << "  Description: " << description_ << '\n';
    if (!completionNote_.empty()) out << "  Completion note: " << completionNote_ << '\n';
}

double Task::calculateWeight() const {
    return static_cast<int>(priority_) * (status() == TaskStatus::Overdue ? 1.25 : 1.0);
}

bool Task::operator<(const Task& other) const {
    if (dueDate_ != other.dueDate_) return dueDate_ < other.dueDate_;
    return static_cast<int>(priority_) > static_cast<int>(other.priority_);
}

std::ostream& operator<<(std::ostream& out, const Task& task) {
    task.displayDetails(out);
    return out;
}

int Task::generateId() { return nextId_++; }
void Task::observeId(const int id) { nextId_ = std::max(nextId_, id + 1); }

double GeneralTask::calculateWeight() const { return Task::calculateWeight(); }
double DevelopmentTask::calculateWeight() const { return Task::calculateWeight() * 1.20; }
double ResearchTask::calculateWeight() const { return Task::calculateWeight() * 1.10; }
double DocumentationTask::calculateWeight() const { return Task::calculateWeight() * 0.90; }

std::unique_ptr<Task> makeTask(const TaskType type, const int id, const int projectId,
                               const int teamId, const std::string& title,
                               const std::string& description, const int createdBy,
                               const int assignedUserId, const TaskPriority priority,
                               const TaskStatus status, const Date& creationDate,
                               const Date& dueDate, std::optional<Date> completionDate,
                               const std::string& requiredSkill,
                               const std::vector<std::string>& comments,
                               const std::string& completionNote) {
    switch (type) {
        case TaskType::Development:
            return std::make_unique<DevelopmentTask>(id, projectId, teamId, title, description,
                createdBy, assignedUserId, priority, status, creationDate, dueDate,
                completionDate, requiredSkill, comments, completionNote);
        case TaskType::Research:
            return std::make_unique<ResearchTask>(id, projectId, teamId, title, description,
                createdBy, assignedUserId, priority, status, creationDate, dueDate,
                completionDate, requiredSkill, comments, completionNote);
        case TaskType::Documentation:
            return std::make_unique<DocumentationTask>(id, projectId, teamId, title, description,
                createdBy, assignedUserId, priority, status, creationDate, dueDate,
                completionDate, requiredSkill, comments, completionNote);
        case TaskType::General:
            return std::make_unique<GeneralTask>(id, projectId, teamId, title, description,
                createdBy, assignedUserId, priority, status, creationDate, dueDate,
                completionDate, requiredSkill, comments, completionNote);
    }
    throw ValidationError("Cannot create an unknown task type.");
}

} // namespace teamsync
