#pragma once

#include "User.h"

#include <memory>
#include <array>
#include <string>
#include <vector>

namespace teamsync {

const std::array<std::string, 3>& securityQuestions();

class Authentication {
public:
    explicit Authentication(std::vector<std::unique_ptr<User>>& users) : users_(users) {}

    User& registerUser(const std::string& fullName, const std::string& username,
                       const std::string& password, const std::string& confirmation,
                       const std::string& email, UserRole role,
                       const std::vector<std::string>& skills,
                       const std::vector<std::string>& securityAnswers);
    User& login(const std::string& username, const std::string& password) const;
    void changePassword(User& user, const std::string& oldPassword,
                        const std::string& newPassword, const std::string& confirmation) const;
    void resetPassword(User& user, const std::vector<std::string>& securityAnswers,
                       const std::string& newPassword, const std::string& confirmation) const;
    void setSecurityAnswers(User& user, const std::string& currentPassword,
                            const std::vector<std::string>& securityAnswers) const;
    void editProfile(User& user, const std::string& fullName, const std::string& email,
                     const std::vector<std::string>& skills) const;
    bool usernameExists(const std::string& username) const;

private:
    std::vector<std::unique_ptr<User>>& users_;
};

} // namespace teamsync
