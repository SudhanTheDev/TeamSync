#include "Project.h"

#include "Exceptions.h"
#include "Utils.h"

#include <algorithm>
#include <ostream>
#include <utility>

namespace teamsync {

int Project::nextId_ = 501;

std::string toString(const ProjectStatus status) {
    switch (status) {
        case ProjectStatus::NotStarted: return "Not Started";
        case ProjectStatus::InProgress: return "In Progress";
        case ProjectStatus::Completed: return "Completed";
        case ProjectStatus::Archived: return "Archived";
    }
    return "Unknown";
}

ProjectStatus projectStatusFromString(const std::string& value) {
    if (value == "Not Started") return ProjectStatus::NotStarted;
    if (value == "In Progress") return ProjectStatus::InProgress;
    if (value == "Completed") return ProjectStatus::Completed;
    if (value == "Archived") return ProjectStatus::Archived;
    throw ValidationError("Unknown project status: " + value);
}

Project::Project(const int id, const int teamId, std::string title, std::string description,
                 Date startDate, Date dueDate, const ProjectStatus status, const int createdBy,
                 std::vector<int> taskIds)
    : id_(id), teamId_(teamId), title_(std::move(title)), description_(std::move(description)),
      startDate_(startDate), dueDate_(dueDate), status_(status), createdBy_(createdBy),
      taskIds_(std::move(taskIds)) {
    setTitle(title_);
    setDates(startDate_, dueDate_);
    observeId(id_);
}

void Project::setTitle(const std::string& value) {
    const auto cleaned = util::trim(value);
    if (cleaned.empty()) throw ValidationError("Project title cannot be empty.");
    title_ = cleaned;
}

void Project::setDates(const Date& start, const Date& due) {
    if (due < start) throw ValidationError("Project due date cannot be before its start date.");
    startDate_ = start;
    dueDate_ = due;
}

void Project::addTask(const int taskId) {
    if (std::find(taskIds_.begin(), taskIds_.end(), taskId) == taskIds_.end()) taskIds_.push_back(taskId);
}

void Project::removeTask(const int taskId) {
    taskIds_.erase(std::remove(taskIds_.begin(), taskIds_.end(), taskId), taskIds_.end());
}

int Project::generateId() { return nextId_++; }
void Project::observeId(const int id) { nextId_ = std::max(nextId_, id + 1); }

std::ostream& operator<<(std::ostream& out, const Project& project) {
    return out << '[' << project.id_ << "] " << project.title_ << " | "
               << toString(project.status_) << " | Due: " << project.dueDate_
               << " | Tasks: " << project.taskIds_.size();
}

} // namespace teamsync
