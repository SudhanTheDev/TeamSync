#pragma once

#include "Person.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace teamsync {

enum class UserRole { Admin, TeamMember };

std::string toString(UserRole role);
UserRole userRoleFromString(const std::string& value);

class User : public Person {
public:
    User() = default;
    User(int id, std::string fullName, std::string username, std::string passwordHash,
         std::string email, std::vector<std::string> skills, std::string dateCreated,
         std::vector<std::string> securityAnswerHashes = {});
    ~User() override = default;

    const std::string& username() const { return username_; }
    const std::string& passwordHash() const { return passwordHash_; }
    const std::vector<std::string>& skills() const { return skills_; }
    const std::string& dateCreated() const { return dateCreated_; }
    const std::vector<std::string>& securityAnswerHashes() const { return securityAnswerHashes_; }
    bool securityQuestionsConfigured() const;
    void setPasswordHash(const std::string& value) { passwordHash_ = value; }
    void setSkills(std::vector<std::string> value) { skills_ = std::move(value); }
    void setSecurityAnswerHashes(std::vector<std::string> value) {
        securityAnswerHashes_ = std::move(value);
    }

    virtual UserRole role() const = 0;
    virtual bool canManageTeams() const = 0;
    static int generateId();
    static void observeId(int id);

protected:
    std::string username_;
    std::string passwordHash_;
    std::vector<std::string> skills_;
    std::string dateCreated_;
    std::vector<std::string> securityAnswerHashes_;

private:
    static int nextId_;
};

class Admin final : public User {
public:
    using User::User;
    UserRole role() const override { return UserRole::Admin; }
    bool canManageTeams() const override { return true; }
    void displayProfile() const override;
};

class Member final : public User {
public:
    using User::User;
    UserRole role() const override { return UserRole::TeamMember; }
    bool canManageTeams() const override { return false; }
    void displayProfile() const override;
};

std::unique_ptr<User> makeUser(UserRole role, int id, const std::string& fullName,
                               const std::string& username, const std::string& passwordHash,
                               const std::string& email, const std::vector<std::string>& skills,
                               const std::string& dateCreated,
                               const std::vector<std::string>& securityAnswerHashes = {});

} // namespace teamsync
