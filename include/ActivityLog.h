#pragma once

#include <iosfwd>
#include <string>

namespace teamsync {

class ActivityLog {
public:
    ActivityLog() = default;
    ActivityLog(int id, int userId, int teamId, std::string action, std::string dateTime);

    int id() const { return id_; }
    int userId() const { return userId_; }
    int teamId() const { return teamId_; }
    const std::string& action() const { return action_; }
    const std::string& dateTime() const { return dateTime_; }

    static int generateId();
    static void observeId(int id);
    friend std::ostream& operator<<(std::ostream& out, const ActivityLog& activity);

private:
    int id_ = 0;
    int userId_ = 0;
    int teamId_ = 0;
    std::string action_;
    std::string dateTime_;
    static int nextId_;
};

} // namespace teamsync
