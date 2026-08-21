// main.cpp - ProjectOreo launcher entry point.
//
// Two ways in:
//   * started by the loader inside a launching game ("--from-loader --pid N"): the window is
//     only shown when something needs the player's attention; otherwise the mods are started
//     silently and the process ends without ever creating a window;
//   * started by the user (double click): the window always opens - they asked for the manager.
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>

#include <string>
#include <vector>

#include "core/app.h"
#include "core/game_gate.h"
#include "core/log.h"
#include "core/process.h"
#include "core/util.h"
#include "ui/ui.h"

namespace {

struct Arguments {
    oreo::StartMode mode = oreo::StartMode::User;
    unsigned long pid = 0;
    // Runs every check, writes the log, and exits. No window, nothing launched. This is the
    // diagnostic entry point ("why does it think NMSpy is missing?") and what to run after
    // changing config.ini.
    bool check_only = false;
};

Arguments parse_arguments() {
    Arguments args;
    int count = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!argv) return args;
    for (int i = 1; i < count; ++i) {
        std::string arg = oreo::to_lower(oreo::to_utf8(argv[i]));
        if (arg == "--from-loader") {
            args.mode = oreo::StartMode::FromLoader;
        } else if (arg == "--check-only" || arg == "--check") {
            args.check_only = true;
        } else if (arg == "--pid" && i + 1 < count) {
            args.pid = (unsigned long)_wtoi64(argv[++i]);
        } else if (oreo::starts_with(arg, "--pid=")) {
            args.pid = (unsigned long)strtoul(arg.substr(6).c_str(), nullptr, 10);
        }
    }
    LocalFree(argv);
    return args;
}

}  // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    // Only one launcher at a time: the loader and an impatient double click must not both run
    // checks (and pip) against the same Python environment.
    HANDLE single_instance = CreateMutexW(nullptr, TRUE, L"Local\\ProjectOreoLauncher");
    bool already_running = single_instance && GetLastError() == ERROR_ALREADY_EXISTS;

    Arguments args = parse_arguments();
    oreo::App app(args.mode, args.pid);
    app.init();

    // An update renames the running launcher aside before taking its name. Whichever build
    // starts next is the one that can finally delete it.
    std::string retired = oreo::exe_path() + ".old";
    if (oreo::file_exists(retired) && DeleteFileW(oreo::to_wide(retired).c_str())) {
        oreo::Log::info("removed the previous launcher left behind by an update");
    }
    // From here on the game is paused inside the loader, waiting for us. Every exit path below
    // has to let it go again.
    oreo::gate_init(args.mode == oreo::StartMode::FromLoader ? args.pid : 0);

    if (already_running) {
        oreo::Log::info("another launcher instance is already running - exiting");
        oreo::gate_release("another instance is in charge");
        CoUninitialize();
        return 0;
    }

    app.run_checks();

    int exit_code = 0;
    if (args.check_only) {
        // Everything worth knowing is already in the log; say only whether a window would open.
        oreo::Log::info(std::string("check-only: the window would ") +
                        (app.show_reason().show ? "open (" + app.show_reason().summary + ")"
                                                : "stay closed"));
    } else if (app.show_reason().show) {
        bool played = oreo::run_ui(app);
        std::string restart = app.pending_restart();
        if (!restart.empty()) {
            // The launcher replaced itself on disk and this process is running from the old
            // file. The game has not reached its window yet - the loader is still holding it -
            // so cancelling costs the player nothing visible: to them the launcher closed and
            // opened again, and they press PLAY in the new one.
            oreo::Log::info("launcher was updated - cancelling the start gate and handing over");
            oreo::gate_cancel("the launcher updated itself and is restarting");

            // Everything below has to happen BEFORE the new launcher is started, and each
            // line for its own reason:
            //   the join  - nothing from the old build may still be writing files, and this
            //               process is running from a file the new one tries to delete;
            //   the log   - it is held with FILE_SHARE_READ, so while this handle is open the
            //               new launcher's Log::init() fails and it runs with no log at all,
            //               silently;
            //   the mutex - the new launcher takes it as its single-instance check, sees
            //               ERROR_ALREADY_EXISTS and exits without ever opening a window.
            app.wait_for_workers();
            oreo::Log::info("ProjectOreo launcher exiting after update");
            oreo::Log::shutdown();
            if (single_instance) {
                CloseHandle(single_instance);
                single_instance = nullptr;
            }

            std::string error;
            if (!oreo::run_detached(restart, {}, oreo::EnvMap(), oreo::path_dir(restart), nullptr,
                                    &error, false)) {
                // The log is shut by now, so the reason has to travel in the box itself.
                std::wstring text =
                    L"ProjectOreo was updated but the new launcher could not be started.\n"
                    L"Start it by hand from the ProjectOreo folder.\n\n" + oreo::to_wide(error);
                MessageBoxW(nullptr, text.c_str(), L"ProjectOreo", MB_OK | MB_ICONWARNING);
            }
            CoUninitialize();
            return 0;
        }
        oreo::Log::info(played ? "window closed after PLAY" : "window closed without launching");
        // Closing the manager without pressing PLAY means "I do not want to play now": the
        // game is stopped before it ever gets a window, rather than started without mods.
        if (!played) oreo::gate_cancel("the manager was closed without pressing PLAY");
    } else {
        // Silent path: nothing to say, so start the mods and disappear.
        oreo::LaunchResult result = app.play_blocking();
        oreo::Log::info("silent launch: " + result.message);
        if (!result.ok) exit_code = 1;
    }

    // Last line of defence: whatever happened above, the game must never be left frozen.
    oreo::gate_release("launcher exiting");
    oreo::Log::info("ProjectOreo launcher exiting");
    oreo::Log::shutdown();
    if (single_instance) CloseHandle(single_instance);
    CoUninitialize();
    return exit_code;
}
