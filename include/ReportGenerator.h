#pragma once

#include "TeamSyncService.h"

#include <filesystem>
#include <optional>
#include <string>

namespace teamsync {

enum class ReportType {
    TaskCompletion = 1,
    PendingTasks,
    OverdueTasks,
    TeamActivity,
    MemberContribution,
    ProjectStatus,
    SharedFiles,
    TeamMemberList
};

std::string toString(ReportType type);

class ReportGenerator {
public:
    explicit ReportGenerator(const TeamSyncService& service) : service_(service) {}

    std::string generate(ReportType type, int requestingUserId, int teamId) const;
    std::filesystem::path exportText(ReportType type, int requestingUserId, int teamId,
                                     const std::string& customFileName = {}) const;

private:
    const TeamSyncService& service_;
    std::string taskReport(int requestingUserId, int teamId,
                           std::optional<TaskStatus> filter) const;
    std::string activityReport(int requestingUserId, int teamId) const;
    std::string contributionReport(int requestingUserId, int teamId) const;
    std::string projectReport(int requestingUserId, int teamId) const;
    std::string fileReport(int requestingUserId, int teamId) const;
    std::string memberReport(int requestingUserId, int teamId) const;
};

} // namespace teamsync
