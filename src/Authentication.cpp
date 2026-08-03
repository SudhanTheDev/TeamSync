#include "Authentication.h"

#include "Date.h"
#include "Exceptions.h"
#include "Utils.h"

#include <algorithm>

namespace teamsync {
namespace {

std::vector<std::string> hashSecurityAnswers(const std::string& username,
                                             const std::vector<std::string>& answers) {
    if (answers.size() != securityQuestions().size()) {
        throw ValidationError("Answer all three security questions.");
    }
    std::vector<std::string> hashes;
    hashes.reserve(answers.size());
    for (std::size_t index = 0; index < answers.size(); ++index) {
        const auto normalized = util::lower(util::trim(answers[index]));
        if (normalized.size() < 2) {
            throw ValidationError("Each security answer must contain at least 2 characters.");
        }
        hashes.push_back(util::hashPassword(normalized,
            util::lower(username) + "#security-" + std::to_string(index + 1)));
    }
    return hashes;
}

} // namespace

const std::array<std::string, 3>& securityQuestions() {
    static const std::array<std::string, 3> questions = {
        "What was the name of your first school?",
        "What nickname did your family use for you?",
        "In which city or town were you born?"
    };
    return questions;
}

bool Authentication::usernameExists(const std::string& username) const {
    const auto normalized = util::lower(util::trim(username));
    return std::any_of(users_.begin(), users_.end(), [&normalized](const auto& user) {
        return util::lower(user->username()) == normalized;
    });
}

User& Authentication::registerUser(const std::string& fullName, const std::string& username,
                                   const std::string& password, const std::string& confirmation,
                                   const std::string& email, const UserRole role,
                                   const std::vector<std::string>& skills,
                                   const std::vector<std::string>& securityAnswers) {
    const auto cleanName = util::trim(fullName);
    const auto cleanUsername = util::trim(username);
    const auto cleanEmail = util::trim(email);
    if (cleanName.empty()) throw ValidationError("Full name cannot be empty.");
    if (cleanUsername.empty()) throw ValidationError("Username cannot be empty.");
    if (password.empty()) throw ValidationError("Password cannot be empty.");
    if (password.size() < 4) throw ValidationError("Password must contain at least 4 characters.");
    if (password != confirmation) throw ValidationError("Password confirmation does not match.");
    if (usernameExists(cleanUsername)) throw ValidationError("That username is already registered.");
    if (!cleanEmail.empty() && cleanEmail.find('@') == std::string::npos) {
        throw ValidationError("Email address must contain @, or be left empty.");
    }
    const auto answerHashes = hashSecurityAnswers(cleanUsername, securityAnswers);
    const int id = User::generateId();
    users_.push_back(makeUser(role, id, cleanName, cleanUsername,
                              util::hashPassword(password, cleanUsername), cleanEmail, skills,
                              Date::today().toString(), answerHashes));
    return *users_.back();
}

User& Authentication::login(const std::string& username, const std::string& password) const {
    const auto normalized = util::lower(util::trim(username));
    const auto position = std::find_if(users_.begin(), users_.end(), [&normalized](const auto& user) {
        return util::lower(user->username()) == normalized;
    });
    if (position == users_.end() ||
        (*position)->passwordHash() != util::hashPassword(password, (*position)->username())) {
        throw AuthenticationError("Invalid username or password.");
    }
    return **position;
}

void Authentication::changePassword(User& user, const std::string& oldPassword,
                                    const std::string& newPassword,
                                    const std::string& confirmation) const {
    if (user.passwordHash() != util::hashPassword(oldPassword, user.username())) {
        throw AuthenticationError("Current password is incorrect.");
    }
    if (newPassword.size() < 4) throw ValidationError("New password must contain at least 4 characters.");
    if (newPassword != confirmation) throw ValidationError("Password confirmation does not match.");
    user.setPasswordHash(util::hashPassword(newPassword, user.username()));
}

void Authentication::resetPassword(User& user,
                                   const std::vector<std::string>& securityAnswers,
                                   const std::string& newPassword,
                                   const std::string& confirmation) const {
    if (!user.securityQuestionsConfigured()) {
        throw AuthenticationError("This account has not configured security questions. Sign in normally and configure them from My profile.");
    }
    if (hashSecurityAnswers(user.username(), securityAnswers) != user.securityAnswerHashes()) {
        throw AuthenticationError("One or more security answers are incorrect.");
    }
    if (newPassword.size() < 4) {
        throw ValidationError("New password must contain at least 4 characters.");
    }
    if (newPassword != confirmation) throw ValidationError("Password confirmation does not match.");
    user.setPasswordHash(util::hashPassword(newPassword, user.username()));
}

void Authentication::setSecurityAnswers(User& user, const std::string& currentPassword,
                                        const std::vector<std::string>& securityAnswers) const {
    if (user.passwordHash() != util::hashPassword(currentPassword, user.username())) {
        throw AuthenticationError("Current password is incorrect.");
    }
    user.setSecurityAnswerHashes(hashSecurityAnswers(user.username(), securityAnswers));
}

void Authentication::editProfile(User& user, const std::string& fullName,
                                 const std::string& email,
                                 const std::vector<std::string>& skills) const {
    const auto cleanName = util::trim(fullName);
    const auto cleanEmail = util::trim(email);
    if (cleanName.empty()) throw ValidationError("Full name cannot be empty.");
    if (!cleanEmail.empty() && cleanEmail.find('@') == std::string::npos) {
        throw ValidationError("Email address must contain @, or be left empty.");
    }
    user.setFullName(cleanName);
    user.setEmail(cleanEmail);
    user.setSkills(skills);
}

} // namespace teamsync
