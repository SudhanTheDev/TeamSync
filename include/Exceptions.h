#pragma once

#include <stdexcept>
#include <string>

namespace teamsync {

enum class NavigationTarget {
    Back,
    Dashboard
};

class NavigationRequest final : public std::exception {
public:
    explicit NavigationRequest(const NavigationTarget target) noexcept : target_(target) {}

    NavigationTarget target() const noexcept { return target_; }
    const char* what() const noexcept override {
        return target_ == NavigationTarget::Back
            ? "Return to the previous menu"
            : "Return to the dashboard";
    }

private:
    NavigationTarget target_;
};

class TeamSyncError : public std::runtime_error {
public:
    explicit TeamSyncError(const std::string& message) : std::runtime_error(message) {}
};

class ValidationError : public TeamSyncError {
public:
    using TeamSyncError::TeamSyncError;
};

class AuthenticationError : public TeamSyncError {
public:
    using TeamSyncError::TeamSyncError;
};

class AuthorizationError : public TeamSyncError {
public:
    using TeamSyncError::TeamSyncError;
};

class NotFoundError : public TeamSyncError {
public:
    using TeamSyncError::TeamSyncError;
};

class PersistenceError : public TeamSyncError {
public:
    using TeamSyncError::TeamSyncError;
};

} // namespace teamsync
