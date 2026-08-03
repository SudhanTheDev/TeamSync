#pragma once

#include "Date.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace teamsync {

class Input {
public:
    Input(std::istream& in, std::ostream& out) : in_(in), out_(out) {}

    void setDashboardAvailable(bool available) noexcept { dashboardAvailable_ = available; }

    std::string line(const std::string& prompt, bool allowEmpty = false);
    int integer(const std::string& prompt, int minimum, int maximum);
    Date date(const std::string& prompt);
    bool yesNo(const std::string& prompt);
    std::vector<std::string> commaSeparated(const std::string& prompt);
    void pause();

private:
    bool handleNavigation(const std::string& value);
    void showNavigationHint() const;

    std::istream& in_;
    std::ostream& out_;
    bool dashboardAvailable_ = false;
};

} // namespace teamsync
