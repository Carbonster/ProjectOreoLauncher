#include "log.h"

#include <windows.h>

#include <mutex>

#include "util.h"

namespace oreo {

namespace {
std::mutex g_mutex;
HANDLE g_handle = INVALID_HANDLE_VALUE;
std::string g_path;
bool g_verbose = false;

// 2 MB: big enough to hold many sessions, small enough to open in Notepad.
const long long kMaxLogBytes = 2 * 1024 * 1024;

void write_line(const char* level, const std::string& msg) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_handle == INVALID_HANDLE_VALUE) return;
    std::string line = "[" + timestamp_now() + "] [" + level + "] " + msg + "\r\n";
    DWORD written = 0;
    WriteFile(g_handle, line.data(), (DWORD)line.size(), &written, nullptr);
    FlushFileBuffers(g_handle);  // a crash mid-run must not lose the last lines
}
}  // namespace

void Log::init(const std::string& log_dir, bool verbose) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_verbose = verbose;
    make_dirs(log_dir);
    g_path = path_join(log_dir, "launcher.log");

    // Rotate once the file gets large so the log never grows without bound.
    HANDLE probe = CreateFileW(to_wide(g_path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (probe != INVALID_HANDLE_VALUE) {
        LARGE_INTEGER size;
        bool too_big = GetFileSizeEx(probe, &size) && size.QuadPart > kMaxLogBytes;
        CloseHandle(probe);
        if (too_big) {
            std::string old = g_path + ".1";
            DeleteFileW(to_wide(old).c_str());
            MoveFileW(to_wide(g_path).c_str(), to_wide(old).c_str());
        }
    }

    g_handle = CreateFileW(to_wide(g_path).c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (g_handle != INVALID_HANDLE_VALUE) SetFilePointer(g_handle, 0, nullptr, FILE_END);
}

void Log::shutdown() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(g_handle);
        g_handle = INVALID_HANDLE_VALUE;
    }
}

void Log::info(const std::string& msg) { write_line("INFO", msg); }
void Log::warn(const std::string& msg) { write_line("WARN", msg); }
void Log::error(const std::string& msg) { write_line("ERROR", msg); }

void Log::debug(const std::string& msg) {
    if (g_verbose) write_line("DEBUG", msg);
}

std::string Log::file_path() { return g_path; }
bool Log::verbose() { return g_verbose; }

void Log::set_verbose(bool verbose) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_verbose = verbose;
}

}  // namespace oreo
