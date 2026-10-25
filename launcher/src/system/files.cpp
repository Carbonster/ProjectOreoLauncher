#include "system/files.h"

#include <windows.h>

#pragma comment(lib, "version.lib")

#include <cstdio>

#include "system/text.h"

namespace oreo {

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

bool is_link(const std::string& p) {
    DWORD attr = GetFileAttributesW(to_wide(p).c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT);
}

bool remove_tree(const std::string& p) {
    if (p.empty()) return false;
    DWORD attr = GetFileAttributesW(to_wide(p).c_str());
    if (attr == INVALID_FILE_ATTRIBUTES) return true;
    if (!(attr & FILE_ATTRIBUTE_DIRECTORY)) return DeleteFileW(to_wide(p).c_str()) != 0;
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) return RemoveDirectoryW(to_wide(p).c_str()) != 0;
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
    const std::wstring temp = to_wide(p + ".oreo-new");
    DeleteFileW(temp.c_str());
    HANDLE h = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = data.empty() || (WriteFile(h, data.data(), (DWORD)data.size(), &written, nullptr) &&
                               written == data.size());
    ok = FlushFileBuffers(h) && ok;
    CloseHandle(h);
    ok = ok && MoveFileExW(temp.c_str(), to_wide(p).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
}

std::string exe_path() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    return to_utf8(std::wstring(buf, n));
}

std::string exe_dir() { return path_dir(exe_path()); }

std::string read_file_base64(const std::string& path) {
    std::string raw;
    if (!read_file(path, raw) || raw.empty()) return "";
    return base64_encode(raw);
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

}  // namespace oreo
