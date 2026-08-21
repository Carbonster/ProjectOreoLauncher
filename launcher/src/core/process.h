// process.h - safe process execution.
// Everything is launched as (executable, argument list) - never as a shell string - so paths
// with spaces or non-ASCII characters work, and no console window is ever shown.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace oreo {

struct ProcResult {
    bool started = false;        // false = CreateProcess itself failed (see `error`)
    bool timed_out = false;
    unsigned long exit_code = 0;
    std::string out;             // captured stdout
    std::string err;             // captured stderr
    std::string command_line;    // exactly what was executed, for the error details view
    std::string error;           // human readable reason when `started` is false
    int duration_ms = 0;

    bool ok() const { return started && !timed_out && exit_code == 0; }
};

using EnvMap = std::map<std::string, std::string>;

// Use as an EnvMap value to REMOVE a variable from the child environment (rather than set it
// to an empty string, which several tools treat as a meaningful value).
extern const char* const kEnvUnset;

// Runs `exe` with `args`, waits up to `timeout_ms`, captures stdout and stderr separately.
// The child inherits our environment plus `extra_env`. No window, ever.
ProcResult run_capture(const std::string& exe, const std::vector<std::string>& args, int timeout_ms,
                       const EnvMap& extra_env = EnvMap(), const std::string& cwd = "");

// Starts `exe` and returns immediately. Used for the game/mod launch, which outlives us.
bool run_detached(const std::string& exe, const std::vector<std::string>& args, const EnvMap& extra_env,
                  const std::string& cwd, unsigned long* out_pid, std::string* error, bool show_window);

// Opens a document/URL with the shell (log files, steam:// links).
bool shell_open(const std::string& target);

// First process id with this executable name, or 0 when it is not running.
unsigned long find_process_id(const std::string& exe_name);
bool process_alive(unsigned long pid);

// True once `module_name` (e.g. "vulkan-1.dll") is loaded in the given process - used to know
// the game has actually started rendering before mods are injected into it.
bool process_has_module(unsigned long pid, const std::string& module_name);

// True when the process owns a visible window of the given class. The game's window is a
// GLFW one ("GLFW30"), and its existence is the honest "the game is actually up" signal -
// unlike a loaded dll, which is already mapped while the game is still starting.
bool process_has_window(unsigned long pid, const std::string& window_class);

// Full path of a running process's executable, or "" when it cannot be read.
std::string process_exe_path(unsigned long pid);

// Builds the exact command line a call would use (for logs and the details dialog).
std::string build_command_line(const std::string& exe, const std::vector<std::string>& args);

}  // namespace oreo
