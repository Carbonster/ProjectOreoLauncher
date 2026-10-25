#include "system/ini.h"

#include <cstdlib>
#include <vector>

#include "system/files.h"
#include "system/text.h"

namespace oreo {

IniMap ini_parse(const std::string& text) {
    IniMap out;
    std::string section;
    // Notepad and PowerShell write UTF-8 files with a byte order mark. Without stripping it the
    // very first line ("[Mod]") would not be recognised as a section header.
    std::string body = starts_with(text, "\xEF\xBB\xBF") ? text.substr(3) : text;
    for (const std::string& raw_line : split(body, '\n')) {
        std::string line = trim(raw_line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            size_t close = line.find(']');
            if (close != std::string::npos) section = to_lower(trim(line.substr(1, close - 1)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = to_lower(trim(line.substr(0, eq)));
        std::string value = trim(line.substr(eq + 1));
        // Strip a trailing inline comment only when it is clearly one (" ; " / " # ").
        size_t cpos = value.find(" ;");
        if (cpos == std::string::npos) cpos = value.find(" #");
        if (cpos != std::string::npos) value = trim(value.substr(0, cpos));
        out[section.empty() ? key : section + "." + key] = value;
    }
    return out;
}

bool ini_load(const std::string& path, IniMap& out) {
    std::string text;
    if (!read_file(path, text)) return false;
    out = ini_parse(text);
    return true;
}

std::string ini_get(const IniMap& ini, const std::string& key, const std::string& fallback) {
    auto it = ini.find(to_lower(key));
    return it == ini.end() ? fallback : it->second;
}

int ini_get_int(const IniMap& ini, const std::string& key, int fallback) {
    std::string v = ini_get(ini, key, "");
    if (v.empty()) return fallback;
    try {
        return std::stoi(v);
    } catch (...) {
        return fallback;
    }
}

bool ini_get_bool(const IniMap& ini, const std::string& key, bool fallback) {
    std::string v = to_lower(ini_get(ini, key, ""));
    if (v.empty()) return fallback;
    return v == "1" || v == "true" || v == "yes" || v == "on";
}

namespace {

// Splits text into lines, each one keeping its own line ending. An ini file edited by hand may
// mix CRLF and LF; rewriting one key must not silently rewrite the rest of the file.
std::vector<std::string> split_keep_endings(const std::string& text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t nl = text.find('\n', start);
        if (nl == std::string::npos) {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, nl - start + 1));
        start = nl + 1;
    }
    return lines;
}

std::string line_ending_of(const std::string& line) {
    if (ends_with(line, "\r\n")) return "\r\n";
    if (ends_with(line, "\n")) return "\n";
    return "";
}

// The section a header line opens, lowercased. Empty when the line is not a header.
std::string section_header(const std::string& line) {
    std::string trimmed = trim(line);
    if (trimmed.size() < 2 || trimmed[0] != '[') return "";
    size_t close = trimmed.find(']');
    if (close == std::string::npos) return "";
    return to_lower(trim(trimmed.substr(1, close - 1)));
}

// The key a "key = value" line assigns, lowercased. Empty for comments, blanks and headers.
std::string assigned_key(const std::string& line) {
    std::string trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#' || trimmed[0] == '[') return "";
    size_t eq = trimmed.find('=');
    if (eq == std::string::npos) return "";
    return to_lower(trim(trimmed.substr(0, eq)));
}

}  // namespace

std::string ini_set(const std::string& text, const std::string& section, const std::string& key,
                    const std::string& value) {
    const std::string wanted_section = to_lower(trim(section));
    const std::string wanted_key = to_lower(trim(key));
    std::vector<std::string> lines = split_keep_endings(text);

    // Preferred line ending: whatever the file already uses. A fresh file gets CRLF, because
    // config.ini is opened in Notepad often enough for that to matter.
    std::string eol = "\r\n";
    for (const std::string& line : lines) {
        std::string found = line_ending_of(line);
        if (!found.empty()) {
            eol = found;
            break;
        }
    }

    std::string current;
    size_t section_start = std::string::npos;   // index of the section header line
    size_t insert_at = std::string::npos;       // one past the section's last meaningful line
    for (size_t i = 0; i < lines.size(); ++i) {
        std::string header = section_header(lines[i]);
        if (!header.empty()) {
            if (current == wanted_section && insert_at == std::string::npos) insert_at = i;
            current = header;
            if (current == wanted_section) section_start = i;
            continue;
        }
        if (current != wanted_section) continue;
        if (assigned_key(lines[i]) == wanted_key) {
            // Found it: keep this line's indentation and ending, replace only the value.
            std::string indent = lines[i].substr(0, lines[i].find_first_not_of(" \t"));
            std::string ending = line_ending_of(lines[i]);
            if (ending.empty()) ending = eol;
            lines[i] = indent + trim(key) + "=" + value + ending;
            std::string out;
            for (const std::string& l : lines) out += l;
            return out;
        }
        if (!trim(lines[i]).empty()) insert_at = i + 1;
    }

    if (section_start == std::string::npos) {
        // No such section anywhere: append it, keeping the file's own line ending.
        std::string out = text;
        if (!out.empty() && !ends_with(out, "\n")) out += eol;
        out += eol + "[" + trim(section) + "]" + eol + trim(key) + "=" + value + eol;
        return out;
    }

    if (insert_at == std::string::npos) insert_at = section_start + 1;
    // The header itself may be the file's last line without an ending; give it one first.
    if (insert_at > 0 && line_ending_of(lines[insert_at - 1]).empty()) lines[insert_at - 1] += eol;
    lines.insert(lines.begin() + insert_at, trim(key) + "=" + value + eol);
    std::string out;
    for (const std::string& l : lines) out += l;
    return out;
}

}  // namespace oreo
