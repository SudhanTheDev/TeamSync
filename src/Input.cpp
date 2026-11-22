#include "Input.h"

#include "Exceptions.h"
#include "Utils.h"

#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace teamsync {
namespace {
class TerminalEchoGuard {
public:
    explicit TerminalEchoGuard(std::istream& input) {
        if (&input != &std::cin) return;
#ifdef _WIN32
        handle_ = GetStdHandle(STD_INPUT_HANDLE);
        if (handle_ != INVALID_HANDLE_VALUE && GetConsoleMode(handle_, &original_))
            active_ = SetConsoleMode(handle_, original_ & ~ENABLE_ECHO_INPUT) != 0;
#else
        if (isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &original_) == 0) {
            auto hidden = original_;
            hidden.c_lflag &= static_cast<tcflag_t>(~(ECHO | ECHONL));
            active_ = tcsetattr(STDIN_FILENO, TCSANOW, &hidden) == 0;
        }
#endif
    }
    ~TerminalEchoGuard() {
        if (!active_) return;
#ifdef _WIN32
        SetConsoleMode(handle_, original_);
#else
        tcsetattr(STDIN_FILENO, TCSANOW, &original_);
#endif
    }
    TerminalEchoGuard(const TerminalEchoGuard&) = delete;
    TerminalEchoGuard& operator=(const TerminalEchoGuard&) = delete;
    bool active() const { return active_; }
private:
    bool active_ = false;
#ifdef _WIN32
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    DWORD original_ = 0;
#else
    termios original_{};
#endif
};
} // namespace

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

std::string Input::secretLine(const std::string& prompt, const bool allowEmpty) {
    TerminalEchoGuard echo(in_); // Restores original mode on Back/Dashboard/EOF too.
    while (true) {
        out_ << prompt;
        showNavigationHint();
        out_ << std::flush;
        std::string value;
        const bool read = static_cast<bool>(std::getline(in_, value));
        if (echo.active()) out_ << '\n';
        if (!read) throw TeamSyncError("Input stream was closed.");
        // Passwords may legitimately contain leading/trailing spaces. The GUI
        // passes them unchanged; console authentication must do the same.
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
