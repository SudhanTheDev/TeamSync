#include "Input.h"

#include "Exceptions.h"
#include "Utils.h"

#include <iostream>

namespace teamsync {

void Input::showNavigationHint() const {
    out_ << (dashboardAvailable_ ? "[B=Back | D=Dashboard] " : "[B=Back] ");
}

bool Input::handleNavigation(const std::string& value) {
    const auto command = util::lower(util::trim(value));
    if (command == "b" || command == "back") {
        throw NavigationRequest(NavigationTarget::Back);
    }
    if (command == "d" || command == "dashboard") {
        if (dashboardAvailable_) throw NavigationRequest(NavigationTarget::Dashboard);
        out_ << "The dashboard is available after you log in.\n";
        return true;
    }
    return false;
}

std::string Input::line(const std::string& prompt, const bool allowEmpty) {
    while (true) {
        out_ << prompt;
        showNavigationHint();
        std::string value;
        if (!std::getline(in_, value)) throw TeamSyncError("Input stream was closed.");
        value = util::trim(value);
        if (handleNavigation(value)) continue;
        if (allowEmpty || !value.empty()) return value;
        out_ << "Input cannot be empty. Please try again.\n";
    }
}

int Input::integer(const std::string& prompt, const int minimum, const int maximum) {
    while (true) {
        const auto value = line(prompt);
        try {
            std::size_t used = 0;
            const int parsed = std::stoi(value, &used);
            if (used == value.size() && parsed >= minimum && parsed <= maximum) return parsed;
        } catch (...) {
            // A consistent validation message is printed below.
        }
        out_ << "Enter a whole number from " << minimum << " to " << maximum << ".\n";
    }
}

Date Input::date(const std::string& prompt) {
    while (true) {
        try {
            return Date::fromString(line(prompt + " (YYYY-MM-DD): "));
        } catch (const ValidationError& error) {
            out_ << error.what() << '\n';
        }
    }
}

bool Input::yesNo(const std::string& prompt) {
    while (true) {
        const auto value = util::lower(line(prompt + " (y/n): "));
        if (value == "y" || value == "yes") return true;
        if (value == "n" || value == "no") return false;
        out_ << "Enter y or n.\n";
    }
}

std::vector<std::string> Input::commaSeparated(const std::string& prompt) {
    std::vector<std::string> result;
    const auto value = line(prompt, true);
    for (const auto& part : util::split(value, ',')) {
        const auto cleaned = util::trim(part);
        if (!cleaned.empty()) result.push_back(cleaned);
    }
    return result;
}

void Input::pause() {
    while (true) {
        out_ << "\nPress Enter to continue ";
        showNavigationHint();
        std::string ignored;
        if (!std::getline(in_, ignored)) throw TeamSyncError("Input stream was closed.");
        if (!handleNavigation(ignored)) return;
    }
}

} // namespace teamsync
