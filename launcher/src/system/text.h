// text.h - strings. The launcher keeps text as UTF-8 in std::string; Windows wants UTF-16.
#pragma once

#include <string>
#include <vector>

namespace oreo {

std::wstring to_wide(const std::string& utf8);
std::string to_utf8(const std::wstring& wide);

std::string trim(const std::string& s);
std::string to_lower(const std::string& s);
bool starts_with(const std::string& s, const std::string& prefix);
bool ends_with(const std::string& s, const std::string& suffix);
std::vector<std::string> split(const std::string& s, char sep);

std::string base64_encode(const std::string& raw);

}  // namespace oreo
