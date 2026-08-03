#pragma once

#include "Date.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace teamsync {

enum class ProjectStatus { NotStarted, InProgress, Completed, Archived };
std::string toString(ProjectStatus status);
ProjectStatus projectStatusFromString(const std::string& value);

class Project {
public:
    Project() = default;
    Project(int id, int teamId, std::string title, std::string description, Date startDate,
            Date dueDate, ProjectStatus status, int createdBy, std::vector<int> taskIds = {});

    int id() const { return id_; }
    int teamId() const { return teamId_; }
    const std::string& title() const { return title_; }
    const std::string& description() const { return description_; }
    const Date& startDate() const { return startDate_; }
    const Date& dueDate() const { return dueDate_; }
    ProjectStatus status() const { return status_; }
    int createdBy() const { return createdBy_; }
    const std::vector<int>& taskIds() const { return taskIds_; }

    void setTitle(const std::string& value);
    void setDescription(const std::string& value) { description_ = value; }
    void setDates(const Date& start, const Date& due);
    void setStatus(ProjectStatus value) { status_ = value; }
    void addTask(int taskId);
    void removeTask(int taskId);

    static int generateId();
    static void observeId(int id);
    friend std::ostream& operator<<(std::ostream& out, const Project& project);

private:
    int id_ = 0;
    int teamId_ = 0;
    std::string title_;
    std::string description_;
    Date startDate_;
    Date dueDate_;
    ProjectStatus status_ = ProjectStatus::NotStarted;
    int createdBy_ = 0;
    std::vector<int> taskIds_;
    static int nextId_;
};

} // namespace teamsync
