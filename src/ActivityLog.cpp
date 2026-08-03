#include "ActivityLog.h"

#include "Exceptions.h"
#include "Utils.h"

#include <algorithm>
#include <ostream>
#include <utility>

namespace teamsync {

int ActivityLog::nextId_ = 50001;

ActivityLog::ActivityLog(const int id, const int userId, const int teamId, std::string action,
                         std::string dateTime)
    : id_(id), userId_(userId), teamId_(teamId), action_(util::trim(action)),
      dateTime_(std::move(dateTime)) {
    if (action_.empty()) throw ValidationError("Activity description cannot be empty.");
    observeId(id_);
}

int ActivityLog::generateId() { return nextId_++; }
void ActivityLog::observeId(const int id) { nextId_ = std::max(nextId_, id + 1); }

std::ostream& operator<<(std::ostream& out, const ActivityLog& activity) {
    return out << '[' << activity.dateTime_ << "] User " << activity.userId_
               << (activity.teamId_ ? " | Team " + std::to_string(activity.teamId_) : "")
               << " | " << activity.action_;
}

} // namespace teamsync
