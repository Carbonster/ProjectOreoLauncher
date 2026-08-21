#include "util.h"

#include <windows.h>

#pragma comment(lib, "version.lib")

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace oreo {

std::wstring to_wide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), &out[0], n);
    return out;
}

std::string to_utf8(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), nullptr, 0, nullptr, nullptr);
    std::string out(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), (int)wide.size(), &out[0], n, nullptr, nullptr);
    return out;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string to_lower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)::tolower(c); });
    return out;
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t pos = s.find(sep, start);
        if (pos == std::string::npos) {
            out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

std::string path_join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char last = a[a.size() - 1];
    if (last == '\\' || last == '/') return a + b;
    return a + "\\" + b;
}

std::string path_dir(const std::string& p) {
    size_t pos = p.find_last_of("\\/");
    if (pos == std::string::npos) return "";
    return p.substr(0, pos);
}

std::string path_filename(const std::string& p) {
    size_t pos = p.find_last_of("\\/");
    if (pos == std::string::npos) return p;
    return p.substr(pos + 1);
}

std::string path_stem(const std::string& p) {
    std::string name = path_filename(p);
    size_t pos = name.find_last_of('.');
    if (pos == std::string::npos) return name;
    return name.substr(0, pos);
}

std::string path_ext(const std::string& p) {
    std::string name = path_filename(p);
    size_t pos = name.find_last_of('.');
    if (pos == std::string::npos) return "";
    return to_lower(name.substr(pos));
}

bool file_exists(const std::string& p) {
    DWORD attr = GetFileAttributesW(to_wide(p).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool dir_exists(const std::string& p) {
    DWORD attr = GetFileAttributesW(to_wide(p).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

bool make_dirs(const std::string& p) {
    if (p.empty() || dir_exists(p)) return true;
    std::string parent = path_dir(p);
    if (!parent.empty() && parent != p) make_dirs(parent);
    return CreateDirectoryW(to_wide(p).c_str(), nullptr) != 0 || dir_exists(p);
}

static std::vector<std::string> list_entries(const std::string& p, bool want_dirs) {
    std::vector<std::string> out;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(to_wide(path_join(p, "*")).c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        std::string name = to_utf8(fd.cFileName);
        if (name == "." || name == "..") continue;
        bool is_dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (is_dir == want_dirs) out.push_back(name);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return out;
}

bool remove_tree(const std::string& p) {
    if (p.empty()) return false;
    if (!dir_exists(p)) return !file_exists(p) || DeleteFileW(to_wide(p).c_str()) != 0;
    for (const std::string& name : list_files(p)) {
        std::string child = path_join(p, name);
        SetFileAttributesW(to_wide(child).c_str(), FILE_ATTRIBUTE_NORMAL);
        DeleteFileW(to_wide(child).c_str());
    }
    for (const std::string& name : list_dirs(p)) remove_tree(path_join(p, name));
    return RemoveDirectoryW(to_wide(p).c_str()) != 0;
}

std::vector<std::string> list_dirs(const std::string& p) { return list_entries(p, true); }
std::vector<std::string> list_files(const std::string& p) { return list_entries(p, false); }

bool read_file(const std::string& p, std::string& out) {
    HANDLE h = CreateFileW(to_wide(p).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart > (1LL << 26)) {  // sanity cap: 64 MB
        CloseHandle(h);
        return false;
    }
    out.resize((size_t)size.QuadPart);
    DWORD read = 0;
    bool ok = out.empty() || (ReadFile(h, &out[0], (DWORD)out.size(), &read, nullptr) && read == out.size());
    CloseHandle(h);
    return ok;
}

bool write_file(const std::string& p, const std::string& data) {
    make_dirs(path_dir(p));
    HANDLE h = CreateFileW(to_wide(p).c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = data.empty() || (WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) &&
                               written == data.size());
    CloseHandle(h);
    return ok;
}

std::string exe_path() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    return to_utf8(std::wstring(buf, n));
}

std::string exe_dir() { return path_dir(exe_path()); }

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

std::string read_file_base64(const std::string& path) {
    std::string raw;
    if (!read_file(path, raw) || raw.empty()) return "";
    static const char* table = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((raw.size() + 2) / 3 * 4);
    for (size_t i = 0; i < raw.size(); i += 3) {
        unsigned value = (unsigned char)raw[i] << 16;
        if (i + 1 < raw.size()) value |= (unsigned char)raw[i + 1] << 8;
        if (i + 2 < raw.size()) value |= (unsigned char)raw[i + 2];
        out.push_back(table[(value >> 18) & 0x3f]);
        out.push_back(table[(value >> 12) & 0x3f]);
        out.push_back(i + 1 < raw.size() ? table[(value >> 6) & 0x3f] : '=');
        out.push_back(i + 2 < raw.size() ? table[value & 0x3f] : '=');
    }
    return out;
}

std::string file_version_field(const std::string& path, const std::string& field) {
    DWORD handle = 0;
    std::wstring path_w = to_wide(path);
    DWORD size = GetFileVersionInfoSizeW(path_w.c_str(), &handle);
    if (!size) return "";
    std::vector<char> buffer(size);
    if (!GetFileVersionInfoW(path_w.c_str(), 0, size, buffer.data())) return "";

    struct LangCodepage {
        WORD language;
        WORD code_page;
    }* translate = nullptr;
    UINT translate_len = 0;
    if (!VerQueryValueW(buffer.data(), L"\\VarFileInfo\\Translation", (LPVOID*)&translate, &translate_len) ||
        translate_len < sizeof(LangCodepage)) {
        return "";
    }
    std::wstring sub_block = L"\\StringFileInfo\\";
    wchar_t lang[16];
    swprintf(lang, 16, L"%04x%04x", translate[0].language, translate[0].code_page);
    sub_block += lang;
    sub_block += L"\\";
    sub_block += to_wide(field);

    wchar_t* value = nullptr;
    UINT value_len = 0;
    if (!VerQueryValueW(buffer.data(), sub_block.c_str(), (LPVOID*)&value, &value_len) || !value_len) return "";
    return trim(to_utf8(std::wstring(value, value_len - 1)));
}

std::string timestamp_now() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour,
                  st.wMinute, st.wSecond);
    return buf;
}

long long unix_time_now() { return (long long)::time(nullptr); }

}  // namespace oreo
