// loader.cpp - ProjectOreo Loader: a proxy version.dll that lives in <No Man's Sky>\Binaries.
//
// Why version.dll: Windows looks for a dll in the executable's own folder before the system
// one, and NMS.exe imports version.dll. Dropping ours next to the game therefore makes the
// game load ProjectOreo on its own, with no Steam launch options and no wrapper executable.
//
// Two jobs, and nothing else:
//   1. behave exactly like the real version.dll - every export is forwarded to
//      C:\Windows\System32\version.dll, so the game cannot tell the difference;
//   2. start ProjectOreo\ProjectOreoLauncher.exe and pause the game's startup until the player
//      has decided (pressed PLAY, or closed the manager).
//
// The pause happens here in DllMain, before the game has a window or a renderer, so the player
// never sees the game flicker past the manager. Every failure path resumes the game instead of
// hanging it: no launcher, launcher crashed, launcher never answered - the game just runs on,
// unmodded. The only case where the game does not start is the player explicitly closing the
// manager instead of pressing PLAY.
#include <windows.h>

#include <string>

#include "oreo_version.h"

// Address of our own module, without asking the loader for a handle.
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

HMODULE g_real = nullptr;
CRITICAL_SECTION g_lock;
bool g_lock_ready = false;

std::wstring module_directory(HMODULE module) {
    wchar_t buffer[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(module, buffer, (DWORD)(sizeof(buffer) / sizeof(buffer[0])));
    std::wstring path(buffer, n);
    size_t pos = path.find_last_of(L"\\/");
    return pos == std::wstring::npos ? std::wstring() : path.substr(0, pos);
}

// Appends one line to <Binaries>\ProjectOreo\logs\loader.log. The loader runs inside the game,
// where nothing else can report a problem, so this file is the only way to see what happened.
void log_line(const std::wstring& text) {
    std::wstring dir = module_directory((HMODULE)&__ImageBase);
    if (dir.empty()) return;
    std::wstring logs = dir + L"\\ProjectOreo\\logs";
    CreateDirectoryW((dir + L"\\ProjectOreo").c_str(), nullptr);
    CreateDirectoryW(logs.c_str(), nullptr);

    HANDLE file = CreateFileW((logs + L"\\loader.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                              OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t stamp[64];
    swprintf(stamp, 64, L"[%04d-%02d-%02d %02d:%02d:%02d] ", st.wYear, st.wMonth, st.wDay, st.wHour,
             st.wMinute, st.wSecond);
    std::wstring line = std::wstring(stamp) + text + L"\r\n";

    int size = WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, line.c_str(), (int)line.size(), &utf8[0], size, nullptr, nullptr);
    DWORD written = 0;
    WriteFile(file, utf8.data(), (DWORD)utf8.size(), &written, nullptr);
    CloseHandle(file);
}

// The real version.dll, loaded on first use (never from DllMain, to stay clear of loader lock).
HMODULE real_module() {
    if (g_real) return g_real;
    if (!g_lock_ready) return nullptr;
    EnterCriticalSection(&g_lock);
    if (!g_real) {
        wchar_t system_dir[MAX_PATH];
        UINT n = GetSystemDirectoryW(system_dir, MAX_PATH);
        std::wstring path = std::wstring(system_dir, n) + L"\\version.dll";
        g_real = LoadLibraryW(path.c_str());
        if (!g_real) log_line(L"FATAL: could not load the real " + path);
    }
    LeaveCriticalSection(&g_lock);
    return g_real;
}

template <typename Fn>
Fn forward(const char* name) {
    HMODULE module = real_module();
    return module ? (Fn)GetProcAddress(module, name) : nullptr;
}

// Starts the launcher and holds the game here until it says the player pressed PLAY (or closed
// the window). Runs directly inside DllMain: the game's startup is exactly what we are pausing.
void start_launcher_and_wait() {
    std::wstring dir = module_directory((HMODULE)&__ImageBase);
    std::wstring launcher = dir + L"\\ProjectOreo\\ProjectOreoLauncher.exe";

    if (GetFileAttributesW(launcher.c_str()) == INVALID_FILE_ATTRIBUTES) {
        log_line(L"launcher not found at " + launcher + L" - the game continues without mods");
        return;
    }

    // Both created before the launcher starts, so it can never miss the handshake:
    //   "go"     -> the player pressed PLAY, let the game run
    //   "cancel" -> the player closed the manager, do not start the game at all
    std::wstring pid_text = std::to_wstring(GetCurrentProcessId());
    HANDLE go = CreateEventW(nullptr, TRUE, FALSE, (L"Local\\ProjectOreoGo_" + pid_text).c_str());
    HANDLE cancel = CreateEventW(nullptr, TRUE, FALSE, (L"Local\\ProjectOreoCancel_" + pid_text).c_str());
    if (!go || !cancel) {
        log_line(L"could not create the start gate - the game continues without waiting");
        if (go) CloseHandle(go);
        if (cancel) CloseHandle(cancel);
        return;
    }

    std::wstring command = L"\"" + launcher + L"\" --from-loader --pid " +
                           std::to_wstring(GetCurrentProcessId());
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(launcher.c_str(), &command[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        (dir + L"\\ProjectOreo").c_str(), &si, &pi)) {
        wchar_t code[32];
        swprintf(code, 32, L"%lu", GetLastError());
        log_line(L"could not start the launcher (Windows error " + std::wstring(code) +
                 L") - the game continues without mods");
        CloseHandle(go);
        CloseHandle(cancel);
        return;
    }
    CloseHandle(pi.hThread);
    log_line(L"started the launcher, holding the game until it answers");

    // Wait for go, cancel, or the launcher dying - a dead launcher must not freeze the game.
    HANDLE handles[3] = {go, cancel, pi.hProcess};
    DWORD waited = WaitForMultipleObjects(3, handles, FALSE, INFINITE);
    bool cancelled = false;
    switch (waited) {
        case WAIT_OBJECT_0:
            log_line(L"launcher said go - resuming the game");
            break;
        case WAIT_OBJECT_0 + 1:
            log_line(L"launcher cancelled the launch - closing the game");
            cancelled = true;
            break;
        case WAIT_OBJECT_0 + 2:
            log_line(L"launcher exited without answering - resuming the game");
            break;
        default:
            log_line(L"wait on the start gate failed - resuming the game");
            break;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(go);
    CloseHandle(cancel);

    if (cancelled) {
        // The player decided not to play. We are still inside DllMain: the game has no window,
        // no save loaded and nothing to lose, so ending the process here is clean.
        ExitProcess(0);
    }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        InitializeCriticalSection(&g_lock);
        g_lock_ready = true;
        log_line(L"ProjectOreo Loader " PROJECTOREO_VERSION_W L" attached to the game");
        // The game's startup is paused right here until the launcher answers. Only the loading
        // thread waits, and every failure path below resumes the game rather than hanging it.
        start_launcher_and_wait();
    }
    return TRUE;
}

// ---------------------------------------------------------------------------------------
// Forwarded exports. Names are mapped to the real ones by loader.def.
// ---------------------------------------------------------------------------------------
extern "C" {

BOOL WINAPI proxy_GetFileVersionInfoA(LPCSTR name, DWORD handle, DWORD len, LPVOID data) {
    typedef BOOL(WINAPI * Fn)(LPCSTR, DWORD, DWORD, LPVOID);
    Fn fn = forward<Fn>("GetFileVersionInfoA");
    return fn ? fn(name, handle, len, data) : FALSE;
}

BOOL WINAPI proxy_GetFileVersionInfoW(LPCWSTR name, DWORD handle, DWORD len, LPVOID data) {
    typedef BOOL(WINAPI * Fn)(LPCWSTR, DWORD, DWORD, LPVOID);
    Fn fn = forward<Fn>("GetFileVersionInfoW");
    return fn ? fn(name, handle, len, data) : FALSE;
}

BOOL WINAPI proxy_GetFileVersionInfoExA(DWORD flags, LPCSTR name, DWORD handle, DWORD len, LPVOID data) {
    typedef BOOL(WINAPI * Fn)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
    Fn fn = forward<Fn>("GetFileVersionInfoExA");
    return fn ? fn(flags, name, handle, len, data) : FALSE;
}

BOOL WINAPI proxy_GetFileVersionInfoExW(DWORD flags, LPCWSTR name, DWORD handle, DWORD len, LPVOID data) {
    typedef BOOL(WINAPI * Fn)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
    Fn fn = forward<Fn>("GetFileVersionInfoExW");
    return fn ? fn(flags, name, handle, len, data) : FALSE;
}

DWORD WINAPI proxy_GetFileVersionInfoSizeA(LPCSTR name, LPDWORD handle) {
    typedef DWORD(WINAPI * Fn)(LPCSTR, LPDWORD);
    Fn fn = forward<Fn>("GetFileVersionInfoSizeA");
    return fn ? fn(name, handle) : 0;
}

DWORD WINAPI proxy_GetFileVersionInfoSizeW(LPCWSTR name, LPDWORD handle) {
    typedef DWORD(WINAPI * Fn)(LPCWSTR, LPDWORD);
    Fn fn = forward<Fn>("GetFileVersionInfoSizeW");
    return fn ? fn(name, handle) : 0;
}

DWORD WINAPI proxy_GetFileVersionInfoSizeExA(DWORD flags, LPCSTR name, LPDWORD handle) {
    typedef DWORD(WINAPI * Fn)(DWORD, LPCSTR, LPDWORD);
    Fn fn = forward<Fn>("GetFileVersionInfoSizeExA");
    return fn ? fn(flags, name, handle) : 0;
}

DWORD WINAPI proxy_GetFileVersionInfoSizeExW(DWORD flags, LPCWSTR name, LPDWORD handle) {
    typedef DWORD(WINAPI * Fn)(DWORD, LPCWSTR, LPDWORD);
    Fn fn = forward<Fn>("GetFileVersionInfoSizeExW");
    return fn ? fn(flags, name, handle) : 0;
}

DWORD WINAPI proxy_VerFindFileA(DWORD flags, LPSTR filename, LPSTR windir, LPSTR appdir, LPSTR curdir,
                                PUINT curdirlen, LPSTR destdir, PUINT destdirlen) {
    typedef DWORD(WINAPI * Fn)(DWORD, LPSTR, LPSTR, LPSTR, LPSTR, PUINT, LPSTR, PUINT);
    Fn fn = forward<Fn>("VerFindFileA");
    return fn ? fn(flags, filename, windir, appdir, curdir, curdirlen, destdir, destdirlen) : 0;
}

DWORD WINAPI proxy_VerFindFileW(DWORD flags, LPWSTR filename, LPWSTR windir, LPWSTR appdir, LPWSTR curdir,
                                PUINT curdirlen, LPWSTR destdir, PUINT destdirlen) {
    typedef DWORD(WINAPI * Fn)(DWORD, LPWSTR, LPWSTR, LPWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
    Fn fn = forward<Fn>("VerFindFileW");
    return fn ? fn(flags, filename, windir, appdir, curdir, curdirlen, destdir, destdirlen) : 0;
}

DWORD WINAPI proxy_VerInstallFileA(DWORD flags, LPSTR srcname, LPSTR destname, LPSTR srcdir, LPSTR destdir,
                                   LPSTR curdir, LPSTR tmpfile, PUINT tmpfilelen) {
    typedef DWORD(WINAPI * Fn)(DWORD, LPSTR, LPSTR, LPSTR, LPSTR, LPSTR, LPSTR, PUINT);
    Fn fn = forward<Fn>("VerInstallFileA");
    return fn ? fn(flags, srcname, destname, srcdir, destdir, curdir, tmpfile, tmpfilelen) : 0;
}

DWORD WINAPI proxy_VerInstallFileW(DWORD flags, LPWSTR srcname, LPWSTR destname, LPWSTR srcdir,
                                   LPWSTR destdir, LPWSTR curdir, LPWSTR tmpfile, PUINT tmpfilelen) {
    typedef DWORD(WINAPI * Fn)(DWORD, LPWSTR, LPWSTR, LPWSTR, LPWSTR, LPWSTR, LPWSTR, PUINT);
    Fn fn = forward<Fn>("VerInstallFileW");
    return fn ? fn(flags, srcname, destname, srcdir, destdir, curdir, tmpfile, tmpfilelen) : 0;
}

DWORD WINAPI proxy_VerLanguageNameA(DWORD language, LPSTR buffer, DWORD size) {
    typedef DWORD(WINAPI * Fn)(DWORD, LPSTR, DWORD);
    Fn fn = forward<Fn>("VerLanguageNameA");
    return fn ? fn(language, buffer, size) : 0;
}

DWORD WINAPI proxy_VerLanguageNameW(DWORD language, LPWSTR buffer, DWORD size) {
    typedef DWORD(WINAPI * Fn)(DWORD, LPWSTR, DWORD);
    Fn fn = forward<Fn>("VerLanguageNameW");
    return fn ? fn(language, buffer, size) : 0;
}

BOOL WINAPI proxy_VerQueryValueA(LPCVOID block, LPCSTR sub_block, LPVOID* buffer, PUINT len) {
    typedef BOOL(WINAPI * Fn)(LPCVOID, LPCSTR, LPVOID*, PUINT);
    Fn fn = forward<Fn>("VerQueryValueA");
    return fn ? fn(block, sub_block, buffer, len) : FALSE;
}

BOOL WINAPI proxy_VerQueryValueW(LPCVOID block, LPCWSTR sub_block, LPVOID* buffer, PUINT len) {
    typedef BOOL(WINAPI * Fn)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    Fn fn = forward<Fn>("VerQueryValueW");
    return fn ? fn(block, sub_block, buffer, len) : FALSE;
}

}  // extern "C"
