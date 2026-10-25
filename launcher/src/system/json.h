// json.h - writing JSON by hand, and reading the flat objects the page posts back.
#pragma once

#include <string>

namespace oreo {

// `value` as a JSON string literal, quotes included.
std::string json_quote(const std::string& value);
inline const char* json_bool(bool value) { return value ? "true" : "false"; }

// The string field `key` of a flat JSON object; "" when it is missing or not a string.
std::string json_field(const std::string& json, const std::string& key);

}  // namespace oreo
