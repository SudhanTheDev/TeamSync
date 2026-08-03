#include "ReportGenerator.h"

#include "Exceptions.h"
#include "Utils.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>

namespace teamsync {
namespace {

std::string heading(const Team& team, const std::string& title) {
    std::ostringstream out;
    out << "TEAMSYNC - " << title << "\n"
        << "Team: " << team.name() << " (#" << team.id() << ")\n"
        << "Generated: " << util::timestampNow() << "\n"
        << std::string(96, '=') << "\n";
    return out.str();
}

std::string safeFilePart(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return std::isalnum(c) ? static_cast<char>(std::tolower(c)) : '_';
    });
    return value;
}

} // namespace

std::string toString(const ReportType type) {
    switch (type) {
        case ReportType::TaskCompletion: return "Task Completion Report";
        case ReportType::PendingTasks: return "Pending Tasks Report";
        case ReportType::OverdueTasks: return "Overdue Tasks Report";
        case ReportType::TeamActivity: return "Team Activity Report";
        case ReportType::MemberContribution: return "Member Contribution Report";
        case ReportType::ProjectStatus: return "Project Status Report";
        case ReportType::SharedFiles: return "Shared Files Report";
        case ReportType::TeamMemberList: return "Team Member List";
    }
    return "Unknown Report";
}

std::string ReportGenerator::generate(const ReportType type, const int requestingUserId,
                                      const int teamId) const {
    // Every report helper performs membership validation through a service query.
    switch (type) {
        case ReportType::TaskCompletion:
            return taskReport(requestingUserId, teamId, TaskStatus::Completed);
        case ReportType::PendingTasks:
            return taskReport(requestingUserId, teamId, TaskStatus::Pending);
        case ReportType::OverdueTasks:
            return taskReport(requestingUserId, teamId, TaskStatus::Overdue);
        case ReportType::TeamActivity: return activityReport(requestingUserId, teamId);
        case ReportType::MemberContribution: return contributionReport(requestingUserId, teamId);
        case ReportType::ProjectStatus: return projectReport(requestingUserId, teamId);
        case ReportType::SharedFiles: return fileReport(requestingUserId, teamId);
        case ReportType::TeamMemberList: return memberReport(requestingUserId, teamId);
    }
    throw ValidationError("Unknown report type.");
}

std::filesystem::path ReportGenerator::exportText(const ReportType type,
                                                  const int requestingUserId,
                                                  const int teamId,
                                                  const std::string& customFileName) const {
    const auto content = generate(type, requestingUserId, teamId);
    std::string fileName = util::trim(customFileName);
    if (fileName.empty()) {
        fileName = "team_" + std::to_string(teamId) + '_' + safeFilePart(toString(type)) + ".txt";
    }
    if (std::filesystem::path(fileName).extension() != ".txt") fileName += ".txt";
    const auto target = service_.fileManager().reportsDirectory() /
                        std::filesystem::path(fileName).filename();
    std::ofstream output(target, std::ios::trunc);
    if (!output) throw PersistenceError("Cannot create report file: " + target.string());
    output << content;
    if (!output) throw PersistenceError("Failed while writing report file: " + target.string());
    return target;
}

std::string ReportGenerator::taskReport(const int requestingUserId, const int teamId,
                                        const std::optional<TaskStatus> filter) const {
    const auto& parent = service_.team(teamId);
    if (!parent.hasMember(requestingUserId)) throw AuthorizationError("You do not belong to this team.");
    std::ostringstream out;
    const std::string title = filter ? toString(*filter) + " Tasks Report" : "All Tasks Report";
    out << heading(parent, title)
        << std::left << std::setw(8) << "ID" << std::setw(28) << "Title"
        << std::setw(16) << "Priority" << std::setw(16) << "Status"
        << std::setw(14) << "Due" << "Assignee\n"
        << std::string(96, '-') << '\n';
    int count = 0;
    for (const auto* value : service_.tasksForTeam(teamId)) {
        if (filter && value->status() != *filter) continue;
        out << std::left << std::setw(8) << value->id()
            << std::setw(28) << value->title().substr(0, 27)
            << std::setw(16) << toString(value->priority())
            << std::setw(16) << toString(value->status())
            << std::setw(14) << value->dueDate().toString()
            << service_.user(value->assignedUserId()).fullName() << '\n';
        ++count;
    }
    out << std::string(96, '-') << "\nTotal records: " << count << '\n';
    return out.str();
}

std::string ReportGenerator::activityReport(const int requestingUserId, const int teamId) const {
    const auto& parent = service_.team(teamId);
    const auto values = service_.activitiesForUser(requestingUserId, teamId);
    std::ostringstream out;
    out << heading(parent, "Team Activity Report")
        << std::left << std::setw(22) << "Date/time" << std::setw(24) << "User" << "Action\n"
        << std::string(96, '-') << '\n';
    for (const auto* value : values) out << std::setw(22) << value->dateTime()
        << std::setw(24) << service_.user(value->userId()).fullName().substr(0, 23)
        << value->action() << '\n';
    out << std::string(96, '-') << "\nTotal records: " << values.size() << '\n';
    return out.str();
}

std::string ReportGenerator::contributionReport(const int requestingUserId, const int teamId) const {
    const auto& parent = service_.team(teamId);
    const auto values = service_.contributionsForTeam(requestingUserId, teamId);
    std::ostringstream out;
    out << heading(parent, "Member Contribution Report")
        << std::left << std::setw(24) << "Member" << std::right << std::setw(10) << "Assigned"
        << std::setw(11) << "Complete" << std::setw(12) << "Complete %"
        << std::setw(12) << "On-time %" << std::setw(10) << "Overdue"
        << std::setw(10) << "Score" << '\n' << std::string(96, '-') << '\n';
    out << std::fixed << std::setprecision(1);
    for (const auto& value : values) out << std::left << std::setw(24) << value.memberName.substr(0, 23)
        << std::right << std::setw(10) << value.assignedTasks << std::setw(11) << value.completedTasks
        << std::setw(12) << value.completionRate << std::setw(12) << value.onTimeRate
        << std::setw(10) << value.overdueTasks << std::setw(10) << value.score << '\n';
    out << "\nDisclaimer: contribution scores are TeamSync estimates, not perfect measurements of work.\n";
    return out.str();
}

std::string ReportGenerator::projectReport(const int requestingUserId, const int teamId) const {
    const auto& parent = service_.team(teamId);
    if (!parent.hasMember(requestingUserId)) throw AuthorizationError("You do not belong to this team.");
    auto values = const_cast<TeamSyncService&>(service_).projectsForTeam(teamId);
    std::sort(values.begin(), values.end(), [](const Project* left, const Project* right) {
        return left->dueDate() < right->dueDate();
    });
    std::ostringstream out;
    out << heading(parent, "Project Status Report")
        << std::left << std::setw(8) << "ID" << std::setw(30) << "Project"
        << std::setw(17) << "Status" << "Deadline\n"
        << std::string(96, '-') << '\n';
    for (const auto* value : values) out << std::setw(8) << value->id()
        << std::setw(30) << value->title().substr(0, 29) << std::setw(17) << toString(value->status())
        << value->dueDate().toString() << '\n';
    return out.str();
}

std::string ReportGenerator::fileReport(const int requestingUserId, const int teamId) const {
    const auto& parent = service_.team(teamId);
    const auto values = service_.filesForTeam(requestingUserId, teamId);
    std::ostringstream out;
    out << heading(parent, "Shared Files Report")
        << std::left << std::setw(8) << "ID" << std::setw(26) << "File"
        << std::setw(24) << "Uploader" << std::setw(13) << "Size" << "State\n"
        << std::string(96, '-') << '\n';
    for (const auto* value : values) out << std::setw(8) << value->id()
        << std::setw(26) << value->fileName().substr(0, 25)
        << std::setw(24) << service_.user(value->uploaderId()).fullName().substr(0, 23)
        << std::setw(13) << value->recordedSize() << (value->exists() ? "Available" : "Missing") << '\n';
    return out.str();
}

std::string ReportGenerator::memberReport(const int requestingUserId, const int teamId) const {
    const auto& parent = service_.team(teamId);
    if (!parent.hasMember(requestingUserId)) throw AuthorizationError("You do not belong to this team.");
    std::ostringstream out;
    out << heading(parent, "Team Member List")
        << std::left << std::setw(10) << "User ID" << std::setw(28) << "Name"
        << std::setw(22) << "Username" << std::setw(18) << "Role" << "Team role\n"
        << std::string(96, '-') << '\n';
    for (const int id : parent.memberIds()) {
        const auto& value = service_.user(id);
        out << std::setw(10) << value.id() << std::setw(28) << value.fullName().substr(0, 27)
            << std::setw(22) << value.username().substr(0, 21) << std::setw(18) << toString(value.role())
            << (parent.isAdmin(id) ? "Admin" : "Member") << '\n';
    }
    return out.str();
}

} // namespace teamsync
