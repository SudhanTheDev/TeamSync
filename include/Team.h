#pragma once

#include <iosfwd>
#include <string>
#include <vector>

namespace teamsync {

enum class TeamStatus { Active, Archived };
std::string toString(TeamStatus status);
TeamStatus teamStatusFromString(const std::string& value);

class Team {
public:
    Team() = default;
    Team(int id, std::string name, std::string description, int creatorId,
         std::vector<int> memberIds, std::vector<int> adminIds, std::string dateCreated,
         std::string joinCode, TeamStatus status = TeamStatus::Active);

    int id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& description() const { return description_; }
    int creatorId() const { return creatorId_; }
    const std::vector<int>& memberIds() const { return memberIds_; }
    const std::vector<int>& adminIds() const { return adminIds_; }
    const std::string& dateCreated() const { return dateCreated_; }
    const std::string& joinCode() const { return joinCode_; }
    TeamStatus status() const { return status_; }

    void setName(const std::string& value);
    void setDescription(const std::string& value) { description_ = value; }
    void setStatus(TeamStatus value) { status_ = value; }
    bool hasMember(int userId) const;
    bool isAdmin(int userId) const;
    void addMember(int userId);
    void removeMember(int userId);
    void addAdmin(int userId);
    void removeAdmin(int userId);

    static int generateId();
    static void observeId(int id);

    friend std::ostream& operator<<(std::ostream& out, const Team& team);

private:
    int id_ = 0;
    std::string name_;
    std::string description_;
    int creatorId_ = 0;
    std::vector<int> memberIds_;
    std::vector<int> adminIds_;
    std::string dateCreated_;
    std::string joinCode_;
    TeamStatus status_ = TeamStatus::Active;
    static int nextId_;
};

} // namespace teamsync
