#include "Dashboard.h"

#include <algorithm>
#include <iomanip>
#include <ostream>
#include <set>
#include <sstream>

namespace teamsync {

DashboardStats Dashboard::calculate(const TeamSyncService& service, const int userId) {
    DashboardStats stats;
    const auto& current = service.user(userId);
    stats.userName = current.fullName();
    stats.role = toString(current.role());
    const auto joined = service.teamsForUser(userId);
    stats.joinedTeams = static_cast<int>(joined.size());
    stats.projects = static_cast<int>(service.projectsForUser(userId).size());

    const auto assigned = service.tasksForUser(userId);
    for (const auto* value : assigned) {
        switch (value->status()) {
            case TaskStatus::Pending: ++stats.pendingTasks; break;
            case TaskStatus::InProgress: ++stats.inProgressTasks; break;
            case TaskStatus::Completed: ++stats.completedTasks; break;
            case TaskStatus::Overdue: ++stats.overdueTasks; break;
        }
    }
    stats.completionPercentage = assigned.empty() ? 0.0 :
        100.0 * stats.completedTasks / assigned.size();

    std::set<int> members;
    double contributionSum = 0.0;
    int contributionTeams = 0;
    for (const auto* parent : joined) {
        members.insert(parent->memberIds().begin(), parent->memberIds().end());
        const auto contributions = service.contributionsForTeam(userId, parent->id());
        const auto position = std::find_if(contributions.begin(), contributions.end(),
            [userId](const ContributionStats& value) { return value.userId == userId; });
        if (position != contributions.end()) {
            contributionSum += position->score;
            ++contributionTeams;
        }
    }
    stats.totalTeamMembers = static_cast<int>(members.size());
    stats.personalContributionScore = contributionTeams == 0 ? 0.0 : contributionSum / contributionTeams;

    stats.unreadNotifications = static_cast<int>(std::count_if(
        service.notifications().begin(), service.notifications().end(),
        [userId](const Notification& value) { return value.userId() == userId && !value.isRead(); }));

    for (auto it = service.messages().rbegin(); it != service.messages().rend() &&
         stats.recentMessages.size() < 5; ++it) {
        try {
            if (!service.team(it->teamId()).hasMember(userId)) continue;
            std::ostringstream line;
            line << it->dateTime() << " | " << service.user(it->senderId()).fullName()
                 << ": " << it->text();
            stats.recentMessages.push_back(line.str());
        } catch (...) {
            // A dangling/corrupted reference is skipped from the dashboard.
        }
    }
    for (auto it = service.activities().rbegin(); it != service.activities().rend() &&
         stats.recentActivities.size() < 5; ++it) {
        bool visible = it->userId() == userId;
        try { if (it->teamId()) visible = service.team(it->teamId()).hasMember(userId); } catch (...) { visible = false; }
        if (visible) stats.recentActivities.push_back(it->dateTime() + " | " + it->action());
    }
    return stats;
}

void Dashboard::display(const DashboardStats& stats, std::ostream& out) {
    out << "\n================== DASHBOARD ==================\n"
        << "Welcome, " << stats.userName << " (" << stats.role << ")\n"
        << "------------------------------------------------\n"
        << std::left << std::setw(24) << "Joined teams" << stats.joinedTeams << '\n'
        << std::setw(24) << "Accessible projects" << stats.projects << '\n'
        << std::setw(24) << "Pending tasks" << stats.pendingTasks << '\n'
        << std::setw(24) << "In-progress tasks" << stats.inProgressTasks << '\n'
        << std::setw(24) << "Completed tasks" << stats.completedTasks << '\n'
        << std::setw(24) << "Overdue tasks" << stats.overdueTasks << '\n'
        << std::setw(24) << "Unread notifications" << stats.unreadNotifications << '\n'
        << std::setw(24) << "Distinct team members" << stats.totalTeamMembers << '\n'
        << std::setw(24) << "Task completion" << std::fixed << std::setprecision(1)
        << stats.completionPercentage << "%\n"
        << std::setw(24) << "Contribution estimate" << stats.personalContributionScore << "/100\n"
        << "\nRecent messages:\n";
    if (stats.recentMessages.empty()) out << "  No messages yet.\n";
    for (const auto& line : stats.recentMessages) out << "  - " << line << '\n';
    out << "\nRecent team activity:\n";
    if (stats.recentActivities.empty()) out << "  No activity yet.\n";
    for (const auto& line : stats.recentActivities) out << "  - " << line << '\n';
    out << "\nNote: contribution is an estimated TeamSync score, not a perfect measure of work.\n";
}

} // namespace teamsync
