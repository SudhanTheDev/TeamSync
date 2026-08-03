#pragma once

#include "TeamSyncService.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace teamsync {

struct DashboardStats {
    std::string userName;
    std::string role;
    int joinedTeams = 0;
    int projects = 0;
    int pendingTasks = 0;
    int inProgressTasks = 0;
    int completedTasks = 0;
    int overdueTasks = 0;
    double completionPercentage = 0.0;
    double personalContributionScore = 0.0;
    int totalTeamMembers = 0;
    int unreadNotifications = 0;
    std::vector<std::string> recentMessages;
    std::vector<std::string> recentActivities;
};

class Dashboard {
public:
    static DashboardStats calculate(const TeamSyncService& service, int userId);
    static void display(const DashboardStats& stats, std::ostream& out);
};

} // namespace teamsync
