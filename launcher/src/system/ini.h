// ini.h - the human editable files: config.ini and mod manifests.
#pragma once

#include <map>
#include <string>

namespace oreo {

// Flat "section.key" -> value map. Comments start with ; or #. Values keep their case,
// section+key are lowercased so lookups are case insensitive (users type whatever).
using IniMap = std::map<std::string, std::string>;
IniMap ini_parse(const std::string& text);
bool ini_load(const std::string& path, IniMap& out);
std::string ini_get(const IniMap& ini, const std::string& key, const std::string& fallback = "");
int ini_get_int(const IniMap& ini, const std::string& key, int fallback);
bool ini_get_bool(const IniMap& ini, const std::string& key, bool fallback);

// Rewrites one key inside ini TEXT and returns the result. Comments, blank lines, key order and
// every key we did not ask for are preserved - config.ini is still a file a human may open.
// A key missing from an existing section is appended to that section; a missing section is added
// at the end of the file.
std::string ini_set(const std::string& text, const std::string& section, const std::string& key,
                    const std::string& value);

}  // namespace oreo
