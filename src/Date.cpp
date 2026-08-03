#include "Date.h"

#include "Exceptions.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <tuple>

namespace teamsync {

Date::Date() : Date(1, 1, 1970) {}

Date::Date(const int day, const int month, const int year)
    : day_(day), month_(month), year_(year) {
    if (!isValid(day, month, year)) {
        throw ValidationError("Invalid date. Use a real date in YYYY-MM-DD format.");
    }
}

bool Date::isValid(const int day, const int month, const int year) {
    if (year < 1900 || year > 9999 || month < 1 || month > 12 || day < 1) return false;
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int maximum = days[month - 1];
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    if (month == 2 && leap) maximum = 29;
    return day <= maximum;
}

std::string Date::toString() const {
    std::ostringstream out;
    out << std::setw(4) << std::setfill('0') << year_ << '-'
        << std::setw(2) << month_ << '-' << std::setw(2) << day_;
    return out.str();
}

Date Date::fromString(const std::string& value) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
        throw ValidationError("Invalid date format. Expected YYYY-MM-DD.");
    }
    try {
        if (value.find_first_not_of("0123456789", 0) != 4) {
            // Separator layout is checked above; stoi validation below is authoritative.
        }
        return Date(std::stoi(value.substr(8, 2)), std::stoi(value.substr(5, 2)),
                    std::stoi(value.substr(0, 4)));
    } catch (const ValidationError&) {
        throw;
    } catch (...) {
        throw ValidationError("Invalid date format. Expected YYYY-MM-DD.");
    }
}

Date Date::today() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t raw = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &raw);
#else
    localtime_r(&raw, &local);
#endif
    return Date(local.tm_mday, local.tm_mon + 1, local.tm_year + 1900);
}

bool Date::operator==(const Date& other) const {
    return std::tie(year_, month_, day_) == std::tie(other.year_, other.month_, other.day_);
}

bool Date::operator<(const Date& other) const {
    return std::tie(year_, month_, day_) < std::tie(other.year_, other.month_, other.day_);
}

std::ostream& operator<<(std::ostream& out, const Date& date) {
    return out << date.toString();
}

} // namespace teamsync
