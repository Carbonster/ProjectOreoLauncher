// launch_service.h - LaunchService: what the PLAY button actually does.
//
// PLAY is never blocked by version mismatches, missing updates or a failed network check. The
// only thing it cannot survive is not knowing how to start the game at all - and even then it
// falls back to asking Steam to launch No Man's Sky without mods.
//
// Every enabled NMSpy mod is loaded by ONE pyMHF run, the same way `pymhf run nmspy` does it:
// pyMHF injects a single python runtime per game process, so a second run would be a second
// runtime inside the same game. We hand NMSpy the mod folder and let its own loader take over.
//
// Two situations, one code path:
//   * the game is already running (we were started by the loader inside NMS.exe): wait until
//     it is actually rendering, then have pyMHF attach to the live process;
//   * the game is not running: pyMHF starts it through Steam itself, using NMSpy's own config.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "config.h"
#include "game_service.h"
#include "process.h"
#include "python_runtime.h"
#include "state.h"

namespace oreo {

struct LaunchResult {
    bool ok = false;
    bool game_started = false;   // we asked Steam to start the game
    bool mods_started = false;   // pyMHF was launched
    std::string message;
    OperationDetails details;
};

// Everything the NMSpy run needs, worked out before a single process is started. Building it is
// pure: the tests can inspect exactly what NMSpy would be told without touching the game.
struct NmspyLaunchPlan {
    bool valid = false;
    std::string error;                    // why `valid` is false, in one human sentence

    std::string mod_dir;                  // the folder handed to NMSpy as `mod_dir`
    std::vector<std::string> mod_names;   // the python mods this one run loads
    // Mods the game's own mod menu has switched off. That switch controls the game's file mods
    // (.pak and unpacked data); it says nothing about python, and NMSpy has no per-mod switch of
    // its own - it loads everything in `mod_dir`. Listed here so the log can say so plainly.
    std::vector<std::string> game_disabled;
    int detected_mods = 0;                // every mod found in the folder, python or not
    unsigned long game_pid = 0;           // 0 = pyMHF starts the game itself

    std::string config_json;              // pymhf config overrides, handed over as JSON
    std::string script;                   // the `python -c` bootstrap that calls run_module()
};

class LaunchService {
public:
    LaunchService(const Config& cfg, const GameService& game, const PythonRuntimeService& python)
        : cfg_(cfg), game_(game), python_(python) {}

    // Runs on a worker thread: it may wait for the game to finish starting up.
    // `known_game_pid` is the process the loader reported (0 when we were started by hand) -
    // using it means we attach to the game that actually loaded us.
    // `framework_ready` = pyMHF *and* NMSpy are installed in the private Python. When they are
    // not, PLAY still starts the game - just without mods - instead of silently doing nothing.
    // `progress` is called with a short human sentence at every step, so the window can show
    // what is happening instead of freezing on "STARTING...".
    using ProgressFn = std::function<void(const std::string&)>;
    LaunchResult play(const std::vector<ModInfo>& mods, unsigned long known_game_pid = 0,
                      bool framework_ready = true, ProgressFn progress = nullptr);

    // Mods that PLAY expects NMSpy to load: everything in the folder with a pyMHF entry point.
    //
    // Deliberately NOT filtered by the per-mod enable flag. That flag is the game's own mod menu
    // switch, which governs .pak and unpacked data files and has no meaning for a python mod -
    // and NMSpy loads every mod in the folder regardless. Filtering on it would only make the
    // launcher lie about what is running. Removing a python mod means removing its folder.
    static std::vector<const ModInfo*> runnable(const std::vector<ModInfo>& mods);

    // Works out the whole NMSpy run without starting anything.
    // `developer_mode` is DebugHotkeys: it is the only thing that lets pyMHF's own console, log
    // window and developer GUI appear. A player never sees them.
    // `log_dir` is where pyMHF should write its own log. It must already exist: pyMHF drops a
    // plain path it cannot stat and falls back to the game folder.
    static NmspyLaunchPlan build_plan(const std::vector<ModInfo>& mods, const std::string& mods_dir,
                                      const std::string& log_dir, unsigned long game_pid,
                                      bool developer_mode);

    // Window class of the game's main window. Its appearance is what "the game has started"
    // actually means; the overlay finds the game the same way.
    static const char* game_window_class();

private:
    bool start_game_via_steam(std::string* error) const;
    LaunchResult start_nmspy(const NmspyLaunchPlan& plan) const;
    EnvMap launch_env(const NmspyLaunchPlan& plan) const;

    const Config& cfg_;
    const GameService& game_;
    const PythonRuntimeService& python_;
};

}  // namespace oreo
