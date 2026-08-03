#pragma once

#include <iosfwd>
#include <string>

namespace teamsync {

class Date {
public:
    Date();
    Date(int day, int month, int year);

    int day() const { return day_; }
    int month() const { return month_; }
    int year() const { return year_; }

    std::string toString() const;
    static Date fromString(const std::string& value);
    static Date today();
    static bool isValid(int day, int month, int year);

    bool operator==(const Date& other) const;
    bool operator!=(const Date& other) const { return !(*this == other); }
    bool operator<(const Date& other) const;
    bool operator>(const Date& other) const { return other < *this; }
    bool operator<=(const Date& other) const { return !(other < *this); }
    bool operator>=(const Date& other) const { return !(*this < other); }

    friend std::ostream& operator<<(std::ostream& out, const Date& date);

private:
    int day_;
    int month_;
    int year_;
};

} // namespace teamsync
