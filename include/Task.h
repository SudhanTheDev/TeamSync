#pragma once

#include "Date.h"

#include <iosfwd>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace teamsync {

enum class TaskPriority { Low = 1, Medium = 2, High = 3, Urgent = 4 };
enum class TaskStatus { Pending, InProgress, Completed, Overdue };
enum class TaskType { General, Development, Research, Documentation };

std::string toString(TaskPriority priority);
std::string toString(TaskStatus status);
std::string toString(TaskType type);
TaskPriority taskPriorityFromString(const std::string& value);
TaskStatus taskStatusFromString(const std::string& value);
TaskType taskTypeFromString(const std::string& value);

class Task {
public:
    Task(int id, int projectId, int teamId, std::string title, std::string description,
         int createdBy, int assignedUserId, TaskPriority priority, TaskStatus status,
         Date creationDate, Date dueDate, std::optional<Date> completionDate,
         std::string requiredSkill, std::vector<std::string> comments = {},
         std::string completionNote = {});
    virtual ~Task() = default;

    int id() const { return id_; }
    int projectId() const { return projectId_; }
    int teamId() const { return teamId_; }
    const std::string& title() const { return title_; }
    const std::string& description() const { return description_; }
    int createdBy() const { return createdBy_; }
    int assignedUserId() const { return assignedUserId_; }
    TaskPriority priority() const { return priority_; }
    TaskStatus status() const;
    const Date& creationDate() const { return creationDate_; }
    const Date& dueDate() const { return dueDate_; }
    const std::optional<Date>& completionDate() const { return completionDate_; }
    const std::string& requiredSkill() const { return requiredSkill_; }
    const std::vector<std::string>& comments() const { return comments_; }
    const std::string& completionNote() const { return completionNote_; }

    void setTitle(const std::string& value);
    void setDescription(const std::string& value) { description_ = value; }
    void assignTo(int userId) { assignedUserId_ = userId; }
    void setPriority(TaskPriority value) { priority_ = value; }
    void setDueDate(const Date& value);
    void setRequiredSkill(const std::string& value) { requiredSkill_ = value; }
    void start();
    void markCompleted(const std::string& note = {}, const Date& date = Date::today());
    void reopen();
    void addComment(const std::string& comment);

    virtual TaskType type() const = 0;
    virtual void displayDetails(std::ostream& out) const;
    virtual double calculateWeight() const;

    bool operator<(const Task& other) const;
    bool operator==(const Task& other) const { return id_ == other.id_; }
    friend std::ostream& operator<<(std::ostream& out, const Task& task);

    static int generateId();
    static void observeId(int id);

protected:
    int id_;
    int projectId_;
    int teamId_;
    std::string title_;
    std::string description_;
    int createdBy_;
    int assignedUserId_;
    TaskPriority priority_;
    TaskStatus status_;
    Date creationDate_;
    Date dueDate_;
    std::optional<Date> completionDate_;
    std::string requiredSkill_;
    std::vector<std::string> comments_;
    std::string completionNote_;

private:
    static int nextId_;
};

class GeneralTask final : public Task {
public:
    using Task::Task;
    TaskType type() const override { return TaskType::General; }
    double calculateWeight() const override;
};

class DevelopmentTask final : public Task {
public:
    using Task::Task;
    TaskType type() const override { return TaskType::Development; }
    double calculateWeight() const override;
};

class ResearchTask final : public Task {
public:
    using Task::Task;
    TaskType type() const override { return TaskType::Research; }
    double calculateWeight() const override;
};

class DocumentationTask final : public Task {
public:
    using Task::Task;
    TaskType type() const override { return TaskType::Documentation; }
    double calculateWeight() const override;
};

std::unique_ptr<Task> makeTask(TaskType type, int id, int projectId, int teamId,
                               const std::string& title, const std::string& description,
                               int createdBy, int assignedUserId, TaskPriority priority,
                               TaskStatus status, const Date& creationDate, const Date& dueDate,
                               std::optional<Date> completionDate,
                               const std::string& requiredSkill,
                               const std::vector<std::string>& comments = {},
                               const std::string& completionNote = {});

} // namespace teamsync
