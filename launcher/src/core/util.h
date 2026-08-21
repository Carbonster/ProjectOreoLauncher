// util.h - small shared helpers (strings, paths, files, time).
// Everything here is dependency-free and safe to use from any service.
#pragma once

#include <string>
#include <vector>
#include <map>

namespace oreo {

// ---- string conversion (the whole app stores text as UTF-8 std::string) ----------------
std::wstring to_wide(const std::string& utf8);
std::string to_utf8(const std::wstring& wide);

std::string trim(const std::string& s);
std::string to_lower(const std::string& s);
bool starts_with(const std::string& s, const std::string& prefix);
bool ends_with(const std::string& s, const std::string& suffix);
std::vector<std::string> split(const std::string& s, char sep);

// ---- paths -----------------------------------------------------------------------------
std::string path_join(const std::string& a, const std::string& b);
std::string path_dir(const std::string& p);          // directory part, no trailing slash
std::string path_filename(const std::string& p);
std::string path_stem(const std::string& p);         // filename without extension
std::string path_ext(const std::string& p);          // lowercase extension, with dot

bool file_exists(const std::string& p);
bool dir_exists(const std::string& p);
bool make_dirs(const std::string& p);                // recursive mkdir
// Deletes a folder and everything under it. Missing is success - the caller wanted it gone.
bool remove_tree(const std::string& p);
std::vector<std::string> list_dirs(const std::string& p);   // immediate subdirectory names
std::vector<std::string> list_files(const std::string& p);  // immediate file names

bool read_file(const std::string& p, std::string& out);
bool write_file(const std::string& p, const std::string& data);

// Absolute path of the running executable, and the directory holding it.
std::string exe_path();
std::string exe_dir();

// ---- ini (human editable config files: config.ini, mod manifests) ----------------------
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

// ---- version resources -------------------------------------------------------------------
// Reads a StringFileInfo field ("FileVersion", "ProductName", "CompanyName", ...) out of an
// exe or dll. Returns "" when the file has no version resource or the field is absent.
std::string file_version_field(const std::string& path, const std::string& field);

// Reads a file and returns it as a base64 string (used to inline mod thumbnails into the UI
// page - the web view cannot open local files on its own).
std::string read_file_base64(const std::string& path);

// ---- misc -------------------------------------------------------------------------------
std::string timestamp_now();          // "2026-08-16 18:42:03" (local time, for logs)
long long unix_time_now();            // seconds since epoch, for cache expiry

}  // namespace oreo
