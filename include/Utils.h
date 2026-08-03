#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace teamsync::util {

std::string trim(const std::string& value);
std::string lower(std::string value);
bool containsIgnoreCase(const std::string& text, const std::string& query);
std::string encodeField(const std::string& value);
std::string decodeField(const std::string& value);
std::vector<std::string> split(const std::string& value, char delimiter);
std::string join(const std::vector<std::string>& values, char delimiter);
std::vector<int> parseIntList(const std::string& value);
std::string joinInts(const std::vector<int>& values);
std::string timestampNow();
std::string hashPassword(const std::string& password, const std::string& username);
std::string generateJoinCode(int teamId, const std::string& teamName);

} // namespace teamsync::util
