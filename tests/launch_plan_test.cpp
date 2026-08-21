// launch_plan_test.cpp - what PLAY would tell NMSpy, without starting anything.
//
// LaunchService::build_plan() is deliberately pure: it takes the mod list, the mods folder and a
// game pid and produces the exact configuration pyMHF receives. That makes the interesting part
// of launching testable on a machine where neither No Man's Sky nor pyMHF is installed.
#include <iostream>
#include <string>
#include <vector>

#include "core/launch_service.h"
#include "core/state.h"

namespace {

int g_failures = 0;

bool expect(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
    return false;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

bool has_name(const std::vector<std::string>& names, const std::string& wanted) {
    for (const std::string& name : names)
        if (name == wanted) return true;
    return false;
}

oreo::ModInfo python_mod(const std::string& name, bool enabled) {
    oreo::ModInfo mod;
    mod.folder = name;
    mod.name = name;
    mod.path = "C:\\Game\\GAMEDATA\\MODS\\" + name;
    mod.entry_point = "mod.py";
    mod.is_python = true;
    mod.enabled = enabled;
    return mod;
}

oreo::ModInfo unpacked_mod(const std::string& name) {
    oreo::ModInfo mod;
    mod.folder = name;
    mod.name = name;
    mod.path = "C:\\Game\\GAMEDATA\\MODS\\" + name;
    mod.is_unpacked = true;
    mod.enabled = true;
    return mod;
}

const char* kModsDir = "C:\\Game\\GAMEDATA\\MODS";
const char* kLogDir = "C:\\Game\\Binaries\\ProjectOreo\\logs";

// Two enabled python mods have to end up in ONE run: pyMHF injects a single python runtime per
// game process, so a second launch would be a second runtime inside the same game.
void two_mods_share_one_run() {
    std::vector<oreo::ModInfo> mods{python_mod("Fishing Minigame", true),
                                    python_mod("Teleporter Cost", true)};
    oreo::NmspyLaunchPlan plan = oreo::LaunchService::build_plan(mods, kModsDir, kLogDir, 0, false);

    expect(plan.valid, "a plan with two enabled mods should be valid");
    expect(plan.mod_names.size() == 2, "both enabled mods should be covered by the run");
    expect(has_name(plan.mod_names, "Fishing Minigame"), "the first mod is missing from the run");
    expect(has_name(plan.mod_names, "Teleporter Cost"), "the second mod is missing from the run");
    // One plan means one process: there is no per-mod command anywhere in it.
    expect(contains(plan.script, "run_module("),
           "the run should go through pyMHF's run_module, which loads the whole mod folder");
    expect(!contains(plan.script, "load_mod_file"),
           "load_mod_file starts a single file and must no longer be used");
}

// The folder the launcher worked out is what NMSpy is told to scan.
void mod_dir_is_the_detected_folder() {
    std::vector<oreo::ModInfo> mods{python_mod("Fishing Minigame", true)};
    oreo::NmspyLaunchPlan plan = oreo::LaunchService::build_plan(mods, kModsDir, kLogDir, 0, false);

    expect(plan.mod_dir == kModsDir, "the plan should carry the mods folder it was given");
    // Backslashes have to survive the trip into JSON.
    expect(contains(plan.config_json, "\"mod_dir\":\"C:\\\\Game\\\\GAMEDATA\\\\MODS\""),
           "mod_dir should reach pyMHF as an escaped JSON string");
}

// A folder we could not work out is a clear refusal, not a launch with an empty path.
void unknown_mod_dir_is_rejected() {
    std::vector<oreo::ModInfo> mods{python_mod("Fishing Minigame", true)};
    oreo::NmspyLaunchPlan plan = oreo::LaunchService::build_plan(mods, "", kLogDir, 0, false);

    expect(!plan.valid, "a plan without a mods folder must not be valid");
    expect(!plan.error.empty(), "an invalid plan has to say why");
    expect(plan.config_json.empty(), "an invalid plan must not produce a pyMHF config");
}

// A running game is attached to: its pid travels with the config and the exe is never started.
void running_game_is_attached_to() {
    std::vector<oreo::ModInfo> mods{python_mod("Fishing Minigame", true)};
    oreo::NmspyLaunchPlan plan = oreo::LaunchService::build_plan(mods, kModsDir, kLogDir, 4242, false);

    expect(plan.game_pid == 4242, "the plan should carry the pid it was given");
    expect(contains(plan.config_json, "\"pid\":4242"), "the pid should be passed to pyMHF");
    expect(contains(plan.config_json, "\"start_exe\":false"),
           "start_exe must be false so a second copy of the game is never started");
}

// Nothing is running: pyMHF starts the game itself, so start_exe stays at NMSpy's own default.
void no_game_means_pymhf_starts_it() {
    std::vector<oreo::ModInfo> mods{python_mod("Fishing Minigame", true)};
    oreo::NmspyLaunchPlan plan = oreo::LaunchService::build_plan(mods, kModsDir, kLogDir, 0, false);

    expect(plan.game_pid == 0, "no pid should be recorded when the game is not running");
    expect(!contains(plan.config_json, "\"pid\""), "no pid should be sent when there is none");
    expect(!contains(plan.config_json, "start_exe"),
           "start_exe should be left to NMSpy when it has to start the game itself");
}

// pyMHF's interactive prompt reads stdin. The launcher starts python without a console, so the
// prompt would raise at once - and pyMHF answers an exception there by killing the GAME.
void interactive_console_is_always_off() {
    std::vector<oreo::ModInfo> mods{python_mod("Fishing Minigame", true)};
    for (bool developer : {false, true}) {
        oreo::NmspyLaunchPlan plan = oreo::LaunchService::build_plan(mods, kModsDir, kLogDir, 1, developer);
        expect(contains(plan.config_json, "\"interactive_console\":false"),
               "the interactive console must be off in every mode");
    }
}

// A player never sees pyMHF's developer GUI or its log console; DebugHotkeys is the only switch.
void developer_windows_follow_debug_hotkeys() {
    std::vector<oreo::ModInfo> mods{python_mod("Fishing Minigame", true)};

    oreo::NmspyLaunchPlan player = oreo::LaunchService::build_plan(mods, kModsDir, kLogDir, 1, false);
    expect(contains(player.config_json, "\"gui\":{\"shown\":false}"),
           "the developer GUI must be hidden for players");
    expect(contains(player.config_json, "\"logging\":{\"shown\":false"),
           "the log console must be hidden for players");

    oreo::NmspyLaunchPlan developer = oreo::LaunchService::build_plan(mods, kModsDir, kLogDir, 1, true);
    expect(contains(developer.config_json, "\"gui\":{\"shown\":true}"),
           "DebugHotkeys should bring the developer GUI back");
    expect(contains(developer.config_json, "\"logging\":{\"shown\":true"),
           "DebugHotkeys should bring the log console back");
}

// Folders without a pyMHF entry point are mods too (unpacked game files) - they are counted,
// but they are not something NMSpy runs.
void non_python_mods_are_counted_not_run() {
    std::vector<oreo::ModInfo> mods{python_mod("Fishing Minigame", true), unpacked_mod("NoFreePillars")};
    oreo::NmspyLaunchPlan plan = oreo::LaunchService::build_plan(mods, kModsDir, kLogDir, 0, false);

    expect(plan.detected_mods == 2, "every mod folder should be counted as detected");
    expect(plan.mod_names.size() == 1, "only python mods are part of the NMSpy run");
    expect(!has_name(plan.mod_names, "NoFreePillars"), "an unpacked mod is not run by NMSpy");
}

// The game's mod menu switch governs the game's own file mods, not python ones - and NMSpy has
// no per-mod switch at all, it loads the whole folder. So a python mod marked off in the game
// still runs, and the plan says so rather than pretending it was skipped.
void a_game_disabled_python_mod_still_runs() {
    std::vector<oreo::ModInfo> mods{python_mod("Fishing Minigame", true),
                                    python_mod("Teleporter Cost", false)};
    oreo::NmspyLaunchPlan plan = oreo::LaunchService::build_plan(mods, kModsDir, kLogDir, 0, false);

    expect(plan.mod_names.size() == 2,
           "NMSpy loads every python mod in the folder, so both belong to the run");
    expect(has_name(plan.mod_names, "Teleporter Cost"),
           "a mod switched off in the game's mod menu is still loaded by NMSpy");
    expect(plan.game_disabled.size() == 1 && has_name(plan.game_disabled, "Teleporter Cost"),
           "the plan should record which mods the game has switched off");
    expect(!has_name(plan.game_disabled, "Fishing Minigame"),
           "an enabled mod must not be reported as switched off");
}

// Every python mod is switched off in the game menu: they still run, because that switch is
// about the game's file mods. Otherwise PLAY would silently do nothing with no way to fix it.
void all_game_disabled_still_produces_a_run() {
    std::vector<oreo::ModInfo> mods{python_mod("Fishing Minigame", false),
                                    python_mod("Teleporter Cost", false)};
    oreo::NmspyLaunchPlan plan = oreo::LaunchService::build_plan(mods, kModsDir, kLogDir, 0, false);

    expect(plan.valid, "the run should still be valid");
    expect(plan.mod_names.size() == 2, "both mods should still be part of the run");
}

// No mod to run at all is not a launch: PLAY falls back to starting the game plain.
void no_runnable_mods_produces_no_run() {
    std::vector<oreo::ModInfo> mods{unpacked_mod("NoFreePillars")};
    oreo::NmspyLaunchPlan plan = oreo::LaunchService::build_plan(mods, kModsDir, kLogDir, 0, false);

    expect(plan.mod_names.empty(), "an unpacked mod alone gives nothing for NMSpy to run");
}

// pyMHF asks interactive setup questions on import unless PYTEST_VERSION is set, and the config
// itself travels in an environment variable rather than inside the command line.
void bootstrap_reads_its_config_from_the_environment() {
    std::vector<oreo::ModInfo> mods{python_mod("Fishing Minigame", true)};
    oreo::NmspyLaunchPlan plan = oreo::LaunchService::build_plan(mods, kModsDir, kLogDir, 0, false);

    expect(contains(plan.script, "PROJECTOREO_NMSPY_CONFIG"),
           "the bootstrap should read its config from the environment");
    expect(contains(plan.script, "import nmspy"), "the bootstrap should load NMSpy");
    expect(contains(plan.script, "fail(3,") && contains(plan.script, "fail(4,"),
           "a missing NMSpy or pyMHF should exit with a clear message, not a traceback");
}

// pyMHF writes its own log wherever `log_dir` points, and NMSpy's pymhf.toml points it at
// {EXE_DIR} - the game's Binaries folder. A new pymhf-<timestamp>.log per launch, in a folder
// that is not ours, that nothing prunes and that outlives uninstalling us. The plan has to
// override it.
void pymhf_logs_land_in_our_own_folder() {
    std::vector<oreo::ModInfo> mods{python_mod("Fishing Minigame", true)};
    oreo::NmspyLaunchPlan plan = oreo::LaunchService::build_plan(mods, kModsDir, kLogDir, 0, false);

    expect(contains(plan.config_json, "log_dir"),
           "the config should tell pyMHF where to put its log");
    expect(contains(plan.config_json, "ProjectOreo"),
           "the log folder handed to pyMHF should be ours, not the game's");
    expect(!contains(plan.config_json, "EXE_DIR"),
           "the {EXE_DIR} default points at the game folder and must not survive");

    // An unknown log folder is better than a wrong one: pyMHF keeps its own default rather
    // than being handed an empty path it would fail to resolve.
    oreo::NmspyLaunchPlan blank = oreo::LaunchService::build_plan(mods, kModsDir, "", 0, false);
    expect(!contains(blank.config_json, "log_dir"),
           "an empty log folder should be left out of the config entirely");
}

}  // namespace

int main() {
    two_mods_share_one_run();
    mod_dir_is_the_detected_folder();
    unknown_mod_dir_is_rejected();
    running_game_is_attached_to();
    no_game_means_pymhf_starts_it();
    interactive_console_is_always_off();
    developer_windows_follow_debug_hotkeys();
    non_python_mods_are_counted_not_run();
    a_game_disabled_python_mod_still_runs();
    all_game_disabled_still_produces_a_run();
    no_runnable_mods_produces_no_run();
    bootstrap_reads_its_config_from_the_environment();
    pymhf_logs_land_in_our_own_folder();

    if (g_failures) std::cerr << g_failures << " check(s) failed\n";
    return g_failures ? 1 : 0;
}
