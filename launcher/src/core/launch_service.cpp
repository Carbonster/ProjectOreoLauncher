#include "launch_service.h"

#include <windows.h>

#include "log.h"
#include "util.h"

namespace oreo {

namespace {

// How long we are willing to wait for a game that is still starting before injecting mods.
const int kGameReadyTimeoutMs = 180 * 1000;
const int kPollIntervalMs = 500;

// The config overrides travel to python as JSON in an environment variable rather than inside
// the `-c` script. Windows paths are full of backslashes and quotes are hard to get right twice
// (command line, then python literal); JSON only has to be escaped once, and the bootstrap below
// stays short enough to read.
const char* kConfigEnvVar = "PROJECTOREO_NMSPY_CONFIG";

// Hands the whole run to NMSpy's own loader - the same code path `pymhf run nmspy` reaches.
//
// pyMHF's own `run()` cannot be reused directly: it resolves its config through
// %APPDATA%\pymhf\nmspy\pymhf.local.toml, which is whatever a previous manual run happened to
// save. We read NMSpy's shipped pymhf.toml instead and put our own values on top, so the folder
// scanned for mods is always the one the launcher decided on.
//
// `run_module` is pyMHF's documented entry point for this (`from pymhf import run_module`) and
// the one `load_module` itself calls.
const char* kBootstrap = R"PY(import json
import os
import os.path as op
import sys

# Players get no console, so python starts with sys.stdout and sys.stderr set to None and every
# message pyMHF prints - including the reason it could not start - is dropped on the floor.
# Point them at a file instead, so a failed launch leaves something to read.
_log_path = os.environ.get("PROJECTOREO_LAUNCH_LOG")
if _log_path:
    try:
        os.makedirs(op.dirname(_log_path), exist_ok=True)
        _log = open(_log_path, "w", encoding="utf-8", errors="replace", buffering=1)
        sys.stdout = _log
        sys.stderr = _log
    except Exception:
        pass


def fail(code, message):
    sys.stderr.write("ProjectOreo: " + message + "\n")
    sys.stderr.flush()
    raise SystemExit(code)


try:
    import nmspy
except Exception as exc:
    fail(3, "NMSpy could not be imported: %r" % (exc,))

try:
    from pymhf import run_module
    from pymhf.utils.parse_toml import read_pymhf_settings
except Exception as exc:
    fail(4, "pyMHF could not be imported: %r" % (exc,))

module_dir = op.dirname(op.abspath(nmspy.__file__))
config = read_pymhf_settings(op.join(module_dir, "pymhf.toml")) or {}
for key, value in json.loads(os.environ["PROJECTOREO_NMSPY_CONFIG"]).items():
    if isinstance(value, dict) and isinstance(config.get(key), dict):
        config[key].update(value)
    else:
        config[key] = value

# The same folder `pymhf run nmspy` uses, so the offset cache is shared with a manual run and
# never lands inside site-packages, where pip would delete it on the next NMSpy update.
config_dir = op.join(os.environ.get("APPDATA", op.expanduser("~")), "pymhf", "nmspy")
os.makedirs(config_dir, exist_ok=True)

run_module(module_dir, config, "nmspy", config_dir)
)PY";

// Waits until the game is genuinely up, which means its WINDOW exists.
//
// The obvious check - "is vulkan-1.dll loaded?" - is useless here: the loader holds the game
// inside DllMain, and by that point its dlls are already mapped, so the check passes instantly
// and pyMHF would attach to a game that has not started yet. The GLFW window, on the other
// hand, only appears once the engine is actually running. It is the same signal the overlay
// uses to find the game.
bool wait_until_game_ready(unsigned long pid, const LaunchService::ProgressFn& progress) {
    int waited = 0;
    bool told = false;
    while (waited < kGameReadyTimeoutMs) {
        if (!process_alive(pid)) return false;
        if (process_has_window(pid, LaunchService::game_window_class())) return true;
        if (!told && progress) {
            progress("Waiting for No Man's Sky to open its window...");
            told = true;
        }
        Sleep(kPollIntervalMs);
        waited += kPollIntervalMs;
    }
    return false;
}

std::string json_string(const std::string& value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back((char)c);
                }
        }
    }
    out.push_back('"');
    return out;
}

std::string join_names(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) out += (out.empty() ? "" : ", ") + name;
    return out;
}

}  // namespace

std::vector<const ModInfo*> LaunchService::runnable(const std::vector<ModInfo>& mods) {
    std::vector<const ModInfo*> out;
    for (const ModInfo& mod : mods) {
        if (mod.entry_point.empty()) continue;
        out.push_back(&mod);
    }
    return out;
}

bool LaunchService::start_game_via_steam(std::string* error) const {
    std::string url = "steam://rungameid/" + std::to_string(GameService::kSteamAppId);
    Log::info("starting the game through Steam: " + url);
    if (shell_open(url)) return true;
    // Steam is not available - fall back to the executable, which still works for most setups.
    if (game_.found() && run_detached(game_.exe_path(), {}, EnvMap(), game_.binaries_dir(), nullptr, error,
                                      true)) {
        return true;
    }
    if (error && error->empty()) *error = "Steam did not react to the launch request.";
    return false;
}

const char* LaunchService::game_window_class() { return "GLFW30"; }

NmspyLaunchPlan LaunchService::build_plan(const std::vector<ModInfo>& mods, const std::string& mods_dir,
                                         const std::string& log_dir, unsigned long game_pid,
                                         bool developer_mode) {
    NmspyLaunchPlan plan;
    plan.detected_mods = (int)mods.size();
    plan.game_pid = game_pid;
    plan.mod_dir = mods_dir;
    for (const ModInfo* mod : runnable(mods)) {
        plan.mod_names.push_back(mod->name);
        if (!mod->enabled) plan.game_disabled.push_back(mod->name);
    }

    if (mods_dir.empty()) {
        plan.error = "The mods folder is unknown, so NMSpy has nothing to load.";
        return plan;
    }

    // NMSpy reads `mod_dir` and loads every mod inside it in ONE injected python runtime. Handing
    // over the folder is the whole point: a per-mod launch would mean a second python runtime in
    // the same game process, which pyMHF does not support.
    std::string json = "{";
    json += "\"mod_dir\":" + json_string(mods_dir);
    // pyMHF's default is an interactive ">>> " prompt on stdin. We start python without a console
    // for players, so that prompt would raise immediately - and pyMHF answers an exception there
    // by sending SIGTERM to the GAME. It has to be off.
    json += ",\"interactive_console\":false";
    // Outside DebugHotkeys the player never sees pyMHF's developer GUI or its log console.
    json += std::string(",\"gui\":{\"shown\":") + (developer_mode ? "true" : "false") + "}";
    // NMSpy's own pymhf.toml sets `log_dir = "{EXE_DIR}"`, which is the game's Binaries folder:
    // a new pymhf-<timestamp>.log per launch, in someone else's directory, that nothing ever
    // cleans up and that survives uninstalling us. Point it at our own logs folder instead.
    json += std::string(",\"logging\":{\"shown\":") + (developer_mode ? "true" : "false");
    if (!log_dir.empty()) json += ",\"log_dir\":" + json_string(log_dir);
    json += "}";
    if (game_pid) {
        // Attach to the running game instead of starting a second copy of it.
        json += ",\"start_exe\":false";
        json += ",\"pid\":" + std::to_string(game_pid);
    }
    json += "}";

    plan.config_json = json;
    plan.script = kBootstrap;
    plan.valid = true;
    return plan;
}

EnvMap LaunchService::launch_env(const NmspyLaunchPlan& plan) const {
    EnvMap env;
    // pyMHF asks interactive setup questions on import unless this is set. It is checked in
    // exactly one place in pyMHF (its __init__), purely to skip that questionnaire.
    env["PYTEST_VERSION"] = "1";
    env["PROJECTOREO_DIR"] = cfg_.oreo_dir;
    env[kConfigEnvVar] = plan.config_json;
    // With DebugHotkeys the console is on screen and is the better place to watch. Without
    // it there is no console at all, so pyMHF output has to go somewhere readable later.
    if (!cfg_.debug_hotkeys)
        env["PROJECTOREO_LAUNCH_LOG"] =
            path_join(path_join(cfg_.oreo_dir, "logs"), "nmspy-launch.log");
    return env;
}

LaunchResult LaunchService::play(const std::vector<ModInfo>& mods, unsigned long known_game_pid,
                                 bool framework_ready, ProgressFn progress) {
    LaunchResult result;
    auto say = [&progress](const std::string& text) {
        if (progress) progress(text);
    };

    // Prefer the pid the loader gave us: that is the game instance ProjectOreo belongs to.
    unsigned long game_pid = process_alive(known_game_pid) ? known_game_pid : find_process_id("NMS.exe");
    bool game_running = game_pid != 0;

    // pyMHF only accepts a literal path that already exists - it stats the value and falls back
    // to the game folder otherwise. The folder is normally there already (the launcher's own log
    // lives in it), but PLAY must not depend on that.
    std::string pymhf_log_dir = path_join(cfg_.oreo_dir, "logs");
    make_dirs(pymhf_log_dir);
    NmspyLaunchPlan plan =
        build_plan(mods, game_.mods_dir(), pymhf_log_dir, game_pid, cfg_.debug_hotkeys);
    Log::info("play: game folder " + (game_.root().empty() ? std::string("(unknown)") : game_.root()));
    Log::info("play: mod_dir " + (plan.mod_dir.empty() ? std::string("(unknown)") : plan.mod_dir));
    Log::info("play: " + std::to_string(plan.detected_mods) + " mods detected, " +
              std::to_string(plan.mod_names.size()) + " enabled for NMSpy" +
              (plan.mod_names.empty() ? "" : ": " + join_names(plan.mod_names)));
    if (!plan.game_disabled.empty()) {
        // The game's mod menu governs its own file mods, not python ones, and NMSpy loads every
        // mod in the folder anyway. Say so rather than let the log imply they were skipped.
        Log::info("play: switched off in the game's mod menu but still loaded by NMSpy (that "
                  "switch only affects the game's own file mods): " + join_names(plan.game_disabled));
    }

    // No mod to run: PLAY simply starts (or leaves running) the game. Never a dead end.
    if (plan.mod_names.empty()) {
        if (game_running) {
            result.ok = true;
            result.message = "No Man's Sky is already running. No ProjectOreo mod was found to start.";
            Log::info("play: no runnable mods, game already running");
            return result;
        }
        std::string error;
        result.ok = start_game_via_steam(&error);
        result.game_started = result.ok;
        result.message = result.ok ? "No Man's Sky is starting without mods (none were found)."
                                   : "No Man's Sky could not be started. " + error;
        return result;
    }

    const std::string mod_list = join_names(plan.mod_names);

    if (!framework_ready) {
        // Nothing to inject with: start the game plain rather than leaving the player with a
        // button that appears to do nothing.
        std::string error;
        bool started = game_running ? true : start_game_via_steam(&error);
        result.ok = started;
        result.game_started = started && !game_running;
        result.message = "pyMHF and NMSpy are not installed, so no mod can start. " +
                         (started ? std::string("The game is starting without mods.")
                                  : "The game could not be started either. " + error);
        Log::warn("play: pyMHF/NMSpy missing - starting the game without mods");
        return result;
    }

    if (!python_.available()) {
        // Without our Python no mod can run, but the player still gets their game.
        std::string error;
        bool started = game_running ? true : start_game_via_steam(&error);
        result.ok = started;
        result.game_started = started && !game_running;
        result.message = "ProjectOreo's private Python is unavailable, so no mod can start. " +
                         (started ? std::string("The game is starting without mods.") : error);
        Log::error("play: no private python - " + python_.error());
        return result;
    }

    if (!plan.valid) {
        std::string error;
        bool started = game_running ? true : start_game_via_steam(&error);
        result.ok = started;
        result.game_started = started && !game_running;
        result.message = plan.error + (started ? " The game is starting without mods." : "");
        Log::error("play: " + plan.error);
        return result;
    }

    if (game_running) {
        say("Starting No Man's Sky...");
        Log::info("play: game already running (pid " + std::to_string(game_pid) +
                  "), waiting for its window before attaching");
        if (!wait_until_game_ready(game_pid, progress)) {
            if (!process_alive(game_pid)) {
                result.message = "No Man's Sky closed before the mods could be started.";
                Log::warn("play: game process disappeared while waiting");
                return result;
            }
            Log::warn("play: the game window did not appear in time, attaching anyway");
        }
    }

    say(plan.mod_names.size() == 1 ? "Starting " + mod_list + "..." : "Starting mods...");
    result = start_nmspy(plan);
    if (result.mods_started)
        say(plan.mod_names.size() == 1 ? mod_list + " is running." : "Mods are running.");
    if (!game_running && result.mods_started) {
        // pyMHF starts the game itself using the steam id in NMSpy's own configuration.
        result.game_started = true;
    }
    return result;
}

LaunchResult LaunchService::start_nmspy(const NmspyLaunchPlan& plan) const {
    LaunchResult result;
    const std::string mod_list = join_names(plan.mod_names);

    std::vector<std::string> args{"-I", "-c", plan.script};
    EnvMap env = launch_env(plan);

    result.details.valid = true;
    result.details.command = build_command_line(python_.exe(), args);
    result.details.python_path = python_.exe();
    result.details.timestamp = timestamp_now();

    Log::info("play: starting NMSpy for " + std::to_string(plan.mod_names.size()) +
              " mod(s): " + mod_list);
    Log::info("play: NMSpy config " + plan.config_json);
    if (plan.game_pid)
        Log::info("play: attaching to No Man's Sky pid " + std::to_string(plan.game_pid));
    else
        Log::info("play: No Man's Sky is not running - pyMHF will start it");

    std::string error;
    unsigned long pid = 0;
    // The launcher's own folder is the working directory: with several mods in one run there is
    // no single "the mod folder", and it keeps us from holding a handle on the game's files.
    // DebugHotkeys keeps pyMHF's console visible; players get no window at all.
    bool started = run_detached(python_.exe(), args, env, cfg_.oreo_dir, &pid, &error,
                                cfg_.debug_hotkeys);
    if (!started) {
        result.message = "The mods could not be started: " + error;
        result.details.err = error;
        Log::error("play: " + result.message);
        return result;
    }

    result.ok = true;
    result.mods_started = true;
    result.message =
        (plan.mod_names.size() == 1 ? mod_list + " is starting"
                                    : std::to_string(plan.mod_names.size()) + " mods are starting") +
        (plan.game_pid ? " (attaching to the running game)." : ".");
    Log::info("play: launched NMSpy via pyMHF, python pid " + std::to_string(pid) +
              (plan.game_pid ? " (attach mode)" : " (pyMHF starts the game)"));
    if (!cfg_.debug_hotkeys) {
        Log::info("play: pyMHF output goes to logs\\nmspy-launch.log; per-mod load errors are "
                  "reported by NMSpy inside the game and land in its own pymhf log");
    }
    return result;
}

}  // namespace oreo
