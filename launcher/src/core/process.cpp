#include "process.h"

#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <thread>

#include "log.h"
#include "util.h"

namespace oreo {

const char* const kEnvUnset = "\x01__OREO_UNSET__";

namespace {

// Windows command line quoting (the rules CommandLineToArgvW parses back).
std::string quote_arg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos) return arg;
    std::string out = "\"";
    size_t backslashes = 0;
    for (char c : arg) {
        if (c == '\\') {
            backslashes++;
            continue;
        }
        if (c == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out.push_back('"');
        } else {
            out.append(backslashes, '\\');
            out.push_back(c);
        }
        backslashes = 0;
    }
    out.append(backslashes * 2, '\\');
    out.push_back('"');
    return out;
}

std::string last_error_text(DWORD code) {
    LPWSTR buffer = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                 FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, code, 0, (LPWSTR)&buffer, 0, nullptr);
    std::string msg = n && buffer ? trim(to_utf8(std::wstring(buffer, n))) : "";
    if (buffer) LocalFree(buffer);
    if (msg.empty()) msg = "error " + std::to_string(code);
    return msg;
}

// Current environment + overrides, as the double-null terminated block CreateProcessW wants.
std::wstring build_environment(const EnvMap& extra) {
    std::map<std::wstring, std::wstring> vars;  // ordered: Windows expects a sorted block
    LPWCH block = GetEnvironmentStringsW();
    if (block) {
        for (LPWCH p = block; *p; ) {
            std::wstring entry(p);
            p += entry.size() + 1;
            if (entry.empty() || entry[0] == L'=') continue;  // skip "=C:" drive entries
            size_t eq = entry.find(L'=');
            if (eq == std::wstring::npos) continue;
            vars[entry.substr(0, eq)] = entry.substr(eq + 1);
        }
        FreeEnvironmentStringsW(block);
    }
    for (const auto& kv : extra) {
        if (kv.second == kEnvUnset) {
            vars.erase(to_wide(kv.first));
        } else {
            vars[to_wide(kv.first)] = to_wide(kv.second);
        }
    }

    std::wstring out;
    for (const auto& kv : vars) {
        out += kv.first;
        out += L'=';
        out += kv.second;
        out.push_back(L'\0');
    }
    out.push_back(L'\0');
    return out;
}

void drain_pipe(HANDLE pipe, std::string* sink) {
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) sink->append(buffer, read);
}

}  // namespace

std::string build_command_line(const std::string& exe, const std::vector<std::string>& args) {
    std::string cmd = quote_arg(exe);
    for (const std::string& a : args) cmd += " " + quote_arg(a);
    return cmd;
}

ProcResult run_capture(const std::string& exe, const std::vector<std::string>& args, int timeout_ms,
                       const EnvMap& extra_env, const std::string& cwd) {
    ProcResult result;
    result.command_line = build_command_line(exe, args);
    DWORD start_tick = GetTickCount();

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE out_read = nullptr, out_write = nullptr, err_read = nullptr, err_write = nullptr;
    if (!CreatePipe(&out_read, &out_write, &sa, 0) || !CreatePipe(&err_read, &err_write, &sa, 0)) {
        result.error = "Could not create a pipe to read the program output.";
        return result;
    }
    SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = out_write;
    si.hStdError = err_write;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi{};
    std::wstring cmd_w = to_wide(result.command_line);
    std::wstring env_block = build_environment(extra_env);
    std::wstring cwd_w = to_wide(cwd);

    Log::debug("exec: " + result.command_line);

    BOOL ok = CreateProcessW(to_wide(exe).c_str(), &cmd_w[0], nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT, (LPVOID)env_block.c_str(),
                             cwd.empty() ? nullptr : cwd_w.c_str(), &si, &pi);
    CloseHandle(out_write);
    CloseHandle(err_write);
    if (!ok) {
        DWORD code = GetLastError();
        result.error = "Could not start " + path_filename(exe) + ": " + last_error_text(code);
        CloseHandle(out_read);
        CloseHandle(err_read);
        return result;
    }
    result.started = true;

    // Both pipes must be drained concurrently, otherwise a child that fills one of them blocks
    // forever while we wait on the other.
    std::thread err_reader(drain_pipe, err_read, &result.err);
    drain_pipe(out_read, &result.out);
    err_reader.join();

    DWORD wait = WaitForSingleObject(pi.hProcess, timeout_ms > 0 ? (DWORD)timeout_ms : INFINITE);
    if (wait == WAIT_TIMEOUT) {
        result.timed_out = true;
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
        result.error = "The command did not finish within " + std::to_string(timeout_ms / 1000) +
                       " seconds and was stopped.";
    }
    DWORD exit_code = 0;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    result.exit_code = exit_code;

    CloseHandle(out_read);
    CloseHandle(err_read);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    result.duration_ms = (int)(GetTickCount() - start_tick);
    return result;
}

bool run_detached(const std::string& exe, const std::vector<std::string>& args, const EnvMap& extra_env,
                  const std::string& cwd, unsigned long* out_pid, std::string* error, bool show_window) {
    std::string cmd = build_command_line(exe, args);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = show_window ? SW_SHOWNORMAL : SW_HIDE;
    PROCESS_INFORMATION pi{};
    std::wstring cmd_w = to_wide(cmd);
    std::wstring env_block = build_environment(extra_env);
    std::wstring cwd_w = to_wide(cwd);

    DWORD flags = CREATE_UNICODE_ENVIRONMENT | (show_window ? CREATE_NEW_CONSOLE : CREATE_NO_WINDOW);
    Log::debug("spawn: " + cmd);
    BOOL ok = CreateProcessW(to_wide(exe).c_str(), &cmd_w[0], nullptr, nullptr, FALSE, flags,
                             (LPVOID)env_block.c_str(), cwd.empty() ? nullptr : cwd_w.c_str(), &si, &pi);
    if (!ok) {
        if (error) *error = "Could not start " + path_filename(exe) + ": " + last_error_text(GetLastError());
        return false;
    }
    if (out_pid) *out_pid = pi.dwProcessId;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool shell_open(const std::string& target) {
    HINSTANCE h = ShellExecuteW(nullptr, L"open", to_wide(target).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    return (INT_PTR)h > 32;
}

unsigned long find_process_id(const std::string& exe_name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    unsigned long pid = 0;
    std::string wanted = to_lower(exe_name);
    if (Process32FirstW(snap, &entry)) {
        do {
            if (to_lower(to_utf8(entry.szExeFile)) == wanted) {
                pid = entry.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return pid;
}

bool process_alive(unsigned long pid) {
    if (!pid) return false;
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!h) return false;
    bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}

namespace {
struct WindowSearch {
    unsigned long pid;
    std::string wanted_class;
    bool found;
};

BOOL CALLBACK window_search_proc(HWND hwnd, LPARAM param) {
    WindowSearch* search = (WindowSearch*)param;
    DWORD owner = 0;
    GetWindowThreadProcessId(hwnd, &owner);
    if (owner != search->pid || !IsWindowVisible(hwnd)) return TRUE;
    wchar_t name[256] = {0};
    GetClassNameW(hwnd, name, 256);
    if (to_lower(to_utf8(name)) == search->wanted_class) {
        search->found = true;
        return FALSE;
    }
    return TRUE;
}
}  // namespace

bool process_has_window(unsigned long pid, const std::string& window_class) {
    if (!pid) return false;
    WindowSearch search{pid, to_lower(window_class), false};
    EnumWindows(window_search_proc, (LPARAM)&search);
    return search.found;
}

std::string process_exe_path(unsigned long pid) {
    if (!pid) return "";
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return "";
    wchar_t buf[MAX_PATH * 4];
    DWORD size = (DWORD)(sizeof(buf) / sizeof(buf[0]));
    std::string out;
    if (QueryFullProcessImageNameW(h, 0, buf, &size)) out = to_utf8(std::wstring(buf, size));
    CloseHandle(h);
    return out;
}

bool process_has_module(unsigned long pid, const std::string& module_name) {
    if (!pid) return false;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    std::string wanted = to_lower(module_name);
    if (Module32FirstW(snap, &entry)) {
        do {
            if (to_lower(to_utf8(entry.szModule)) == wanted) {
                found = true;
                break;
            }
        } while (Module32NextW(snap, &entry));
    }
    CloseHandle(snap);
    return found;
}

}  // namespace oreo
