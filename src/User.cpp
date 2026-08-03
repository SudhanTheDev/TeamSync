#include "User.h"

#include "Exceptions.h"

#include <algorithm>
#include <iostream>
#include <utility>

namespace teamsync {

int User::nextId_ = 1001;

std::string toString(const UserRole role) {
    return role == UserRole::Admin ? "Admin" : "Team Member";
}

UserRole userRoleFromString(const std::string& value) {
    if (value == "Admin") return UserRole::Admin;
    if (value == "Team Member" || value == "Member") return UserRole::TeamMember;
    throw ValidationError("Unknown user role: " + value);
}

User::User(const int id, std::string fullName, std::string username, std::string passwordHash,
           std::string email, std::vector<std::string> skills, std::string dateCreated,
           std::vector<std::string> securityAnswerHashes)
    : Person(id, std::move(fullName), std::move(email)), username_(std::move(username)),
      passwordHash_(std::move(passwordHash)), skills_(std::move(skills)),
      dateCreated_(std::move(dateCreated)),
      securityAnswerHashes_(std::move(securityAnswerHashes)) {
    observeId(id);
}

bool User::securityQuestionsConfigured() const {
    return securityAnswerHashes_.size() == 3 &&
        std::all_of(securityAnswerHashes_.begin(), securityAnswerHashes_.end(),
                    [](const std::string& value) { return !value.empty(); });
}

int User::generateId() { return nextId_++; }

void User::observeId(const int id) { nextId_ = std::max(nextId_, id + 1); }

namespace {
void printProfile(const User& user) {
    std::cout << "\nUser ID: " << user.id() << "\nName: " << user.fullName()
              << "\nUsername: " << user.username() << "\nEmail: " << user.email()
              << "\nRole: " << toString(user.role()) << "\nSkills: ";
    if (user.skills().empty()) std::cout << "None listed";
    for (std::size_t i = 0; i < user.skills().size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << user.skills()[i];
    }
    std::cout << "\nCreated: " << user.dateCreated() << '\n';
}
} // namespace

void Admin::displayProfile() const { printProfile(*this); }
void Member::displayProfile() const { printProfile(*this); }

std::unique_ptr<User> makeUser(const UserRole role, const int id, const std::string& fullName,
                               const std::string& username, const std::string& passwordHash,
                               const std::string& email, const std::vector<std::string>& skills,
                               const std::string& dateCreated,
                               const std::vector<std::string>& securityAnswerHashes) {
    if (role == UserRole::Admin) {
        return std::make_unique<Admin>(id, fullName, username, passwordHash, email, skills,
                                       dateCreated, securityAnswerHashes);
    }
    return std::make_unique<Member>(id, fullName, username, passwordHash, email, skills,
                                    dateCreated, securityAnswerHashes);
}

} // namespace teamsync
