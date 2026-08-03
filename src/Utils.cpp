#include "Utils.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace teamsync::util {

std::string trim(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(),
                                        [](unsigned char c) { return std::isspace(c) != 0; });
    const auto last = std::find_if_not(value.rbegin(), value.rend(),
                                       [](unsigned char c) { return std::isspace(c) != 0; }).base();
    return first < last ? std::string(first, last) : std::string{};
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool containsIgnoreCase(const std::string& text, const std::string& query) {
    return lower(text).find(lower(query)) != std::string::npos;
}

std::string encodeField(const std::string& value) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (const unsigned char c : value) {
        if (c == '%' || c == '|' || c == ';' || c == ',' || c == '\n' || c == '\r') {
            out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        } else {
            out << static_cast<char>(c);
        }
    }
    return out.str();
}

std::string decodeField(const std::string& value) {
    std::string result;
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            unsigned int decoded = 0;
            std::istringstream hex(value.substr(i + 1, 2));
            if (hex >> std::hex >> decoded) {
                result.push_back(static_cast<char>(decoded));
                i += 2;
                continue;
            }
        }
        result.push_back(value[i]);
    }
    return result;
}

std::vector<std::string> split(const std::string& value, const char delimiter) {
    std::vector<std::string> fields;
    std::stringstream stream(value);
    std::string field;
    while (std::getline(stream, field, delimiter)) {
        fields.push_back(field);
    }
    if (!value.empty() && value.back() == delimiter) {
        fields.emplace_back();
    }
    return fields;
}

std::string join(const std::vector<std::string>& values, const char delimiter) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out << delimiter;
        out << encodeField(values[i]);
    }
    return out.str();
}

std::vector<int> parseIntList(const std::string& value) {
    std::vector<int> result;
    if (value.empty()) return result;
    for (const auto& part : split(value, ',')) {
        try {
            result.push_back(std::stoi(part));
        } catch (...) {
            // One malformed list item does not invalidate an otherwise usable record.
        }
    }
    return result;
}

std::string joinInts(const std::vector<int>& values) {
    std::ostringstream out;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out << ',';
        out << values[i];
    }
    return out.str();
}

std::string timestampNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &value);
#else
    localtime_r(&value, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
    return out.str();
}

std::string hashPassword(const std::string& password, const std::string& username) {
    // Salted FNV-1a is used only to avoid plain-text storage. It is intentionally
    // documented as educational, not equivalent to Argon2/bcrypt/scrypt.
    const std::string input = "TeamSync-v1:" + lower(username) + ':' + password;
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char c : input) {
        hash ^= c;
        hash *= 1099511628211ULL;
    }
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << hash;
    return out.str();
}

std::string generateJoinCode(const int teamId, const std::string& teamName) {
    std::uint32_t value = static_cast<std::uint32_t>(teamId * 2654435761U);
    for (const unsigned char c : teamName) value = (value * 33U) ^ c;
    std::ostringstream out;
    out << "TS" << teamId << '-' << std::uppercase << std::hex << (value & 0xFFFFU);
    return out.str();
}

} // namespace teamsync::util
