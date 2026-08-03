#include "Team.h"

#include "Exceptions.h"
#include "Utils.h"

#include <algorithm>
#include <ostream>
#include <utility>

namespace teamsync {

int Team::nextId_ = 101;

std::string toString(const TeamStatus status) {
    return status == TeamStatus::Active ? "Active" : "Archived";
}

TeamStatus teamStatusFromString(const std::string& value) {
    if (value == "Active") return TeamStatus::Active;
    if (value == "Archived") return TeamStatus::Archived;
    throw ValidationError("Unknown team status: " + value);
}

Team::Team(const int id, std::string name, std::string description, const int creatorId,
           std::vector<int> memberIds, std::vector<int> adminIds, std::string dateCreated,
           std::string joinCode, const TeamStatus status)
    : id_(id), name_(std::move(name)), description_(std::move(description)),
      creatorId_(creatorId), memberIds_(std::move(memberIds)), adminIds_(std::move(adminIds)),
      dateCreated_(std::move(dateCreated)), joinCode_(std::move(joinCode)), status_(status) {
    setName(name_);
    if (!hasMember(creatorId_)) memberIds_.push_back(creatorId_);
    if (!isAdmin(creatorId_)) adminIds_.push_back(creatorId_);
    observeId(id_);
}

void Team::setName(const std::string& value) {
    const auto cleaned = util::trim(value);
    if (cleaned.empty()) throw ValidationError("Team name cannot be empty.");
    name_ = cleaned;
}

bool Team::hasMember(const int userId) const {
    return std::find(memberIds_.begin(), memberIds_.end(), userId) != memberIds_.end();
}

bool Team::isAdmin(const int userId) const {
    return std::find(adminIds_.begin(), adminIds_.end(), userId) != adminIds_.end();
}

void Team::addMember(const int userId) {
    if (hasMember(userId)) throw ValidationError("User already belongs to this team.");
    memberIds_.push_back(userId);
}

void Team::removeMember(const int userId) {
    if (!hasMember(userId)) throw NotFoundError("User is not a member of this team.");
    if (isAdmin(userId) && adminIds_.size() == 1) {
        throw ValidationError("A team must always have at least one admin.");
    }
    memberIds_.erase(std::remove(memberIds_.begin(), memberIds_.end(), userId), memberIds_.end());
    adminIds_.erase(std::remove(adminIds_.begin(), adminIds_.end(), userId), adminIds_.end());
}

void Team::addAdmin(const int userId) {
    if (!hasMember(userId)) throw ValidationError("Only team members can become admins.");
    if (!isAdmin(userId)) adminIds_.push_back(userId);
}

void Team::removeAdmin(const int userId) {
    if (!isAdmin(userId)) return;
    if (adminIds_.size() == 1) throw ValidationError("A team must always have at least one admin.");
    adminIds_.erase(std::remove(adminIds_.begin(), adminIds_.end(), userId), adminIds_.end());
}

int Team::generateId() { return nextId_++; }
void Team::observeId(const int id) { nextId_ = std::max(nextId_, id + 1); }

std::ostream& operator<<(std::ostream& out, const Team& team) {
    return out << '[' << team.id_ << "] " << team.name_ << " | " << toString(team.status_)
               << " | Members: " << team.memberIds_.size() << " | Join code: " << team.joinCode_;
}

} // namespace teamsync
