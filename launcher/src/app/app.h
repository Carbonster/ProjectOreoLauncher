// app.h - owns every service and the state the window draws.
//
// The window only draws snapshots and reports button presses. Long work (checks, pip,
// downloads, launching) runs on worker threads, one file-touching operation at a time.
//
// The methods are spread over files by what they do:
//   app.cpp        - start-up, snapshot, the operation gate
//   checks.cpp     - detection, version lookups, whether the window opens
//   operations.cpp - INSTALL / UPDATE of components, mods and the launcher itself
//   play.cpp       - PLAY and PLAY WITHOUT MODS
//   preferences.cpp - the Settings window and the theme
//   test_mode.cpp  - the test version
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/snapshot.h"
#include "game/game_service.h"
#include "game/launch_service.h"
#include "game/loader_service.h"
#include "mods/mod_discovery.h"
#include "python/package_service.h"
#include "python/python_runtime.h"
#include "settings/config.h"
#include "settings/settings_service.h"
#include "updates/component_update.h"
#include "updates/update_service.h"

namespace oreo {

// Started by the game's loader, the launcher may stay invisible; started by the player, it
// opens. The test version is the same window on made-up mods (test_mode.cpp).
enum class StartMode {
    User = 0,
    FromLoader,
    Test,
};

class App {
public:
    App(StartMode mode, unsigned long loader_pid);
    ~App();

    // Loads config, opens the log, creates the services. Never fails: a broken setup is
    // something the window has to be able to show.
    void init();
    // Detection and update lookups. Blocking - it runs before the window exists, so a healthy
    // setup never flashes one.
    void run_checks();
    // REFRESH: run_checks() on a worker.
    void recheck_async();

    const ShowReason& show_reason() const { return reason_; }
    AppSnapshot snapshot();
    const Config& config() const { return cfg_; }
    bool test_mode() const { return mode_ == StartMode::Test; }

    // ---- buttons (operations.cpp) ----------------------------------------------------
    // A component row's button.
    void run_action(const std::string& component_id);
    // A card's INSTALL / UPDATE: every prepare_steps() step, one after another. `index` is
    // into the snapshot's mod list.
    void prepare_mod_async(size_t index);
    // On success pending_restart() names the new exe: main() starts it and quits.
    void update_launcher_async();
    std::string pending_restart() const;
    bool toggle_mod(size_t index, std::string& error);

    // ---- play.cpp ----------------------------------------------------------------------
    void play_async();
    // PLAY WITHOUT MODS: no pyMHF, and the game's own mods switched off for that one run.
    void play_without_mods_async();
    // Starts the mods on the calling thread. Also what main() runs when no window opens.
    LaunchResult play_blocking();
    bool play_finished() const { return play_done_.load(); }
    // True once, when PLAY WITHOUT MODS in another launcher started the game: this launcher,
    // started by the game's loader, must let it go without mods.
    bool take_start_without_mods();

    // ---- preferences.cpp ---------------------------------------------------------------
    SettingsValues settings_values() const;
    // What detection found, for "Use auto-detected path" / "Use default folder".
    std::string detected_game_path() const;
    std::string detected_mods_path() const;
    // Validates and saves, then detects and scans again so new folders apply at once. On
    // failure fills `errors` per field or `error`, and changes nothing.
    bool apply_settings(const SettingsValues& values, SettingsErrors& errors, std::string& error);
    void set_light_theme(bool light);

    // Joins every worker. main() calls it before handing over to an updated launcher: nothing
    // of the old build may still be writing files. Safe to call twice.
    void wait_for_workers();

private:
    // ---- checks.cpp --------------------------------------------------------------------
    void detect_local();
    void lookup_latest();
    // Asks every component that publishes a manifest for its newest release.
    void lookup_component_manifests();
    void rebuild_components();
    void decide_show_reason();
    void rescan_mods();

    // ---- operations.cpp ----------------------------------------------------------------
    Component* find(const std::string& id);
    void set_busy(const std::string& id, bool busy, const std::string& progress);
    // On the calling thread, which holds the gate. Returns whether it succeeded.
    bool perform_component_action(const std::string& component_id, ActionKind action);
    void perform_mod_update(size_t index);
    ReleaseManifest manifest_for(const std::string& catalog_id);

    // ---- play.cpp ----------------------------------------------------------------------
    void switch_game_mods_off_for_one_run();
    // Part of every detection, so every launcher start puts them back - except the one that
    // lets the run without mods go, which leaves before detecting anything.
    void restore_mods_after_run_without_mods();
    std::string mods_off_record_path() const;

    // ---- test_mode.cpp -----------------------------------------------------------------
    void load_test_state();
    // A button in the test version: holds the gate like the real thing, runs `start` under the
    // state lock, waits, runs `finish` and lets go.
    void simulate(const std::string& what, std::function<void()> start, std::function<void()> finish);
    void wait_unless_stopping(int ms);

    // ---- one file-touching operation at a time (app.cpp) --------------------------------
    // Every button that downloads, installs, rescans, rewrites config or launches goes through
    // this gate. Try-and-refuse, not a mutex: it is entered on the UI thread, which must never
    // block. `running_now` receives what holds the gate on refusal.
    bool begin_operation(const std::string& what, std::string* running_now = nullptr);
    // begin_operation() that logs a refusal.
    bool claim_operation(const std::string& what);
    void end_operation();
    std::string current_operation() const;

    // Releases the gate however a worker leaves.
    class Operation {
    public:
        explicit Operation(App& app) : app_(app) {}
        ~Operation() { app_.end_operation(); }
        Operation(const Operation&) = delete;
        Operation& operator=(const Operation&) = delete;

    private:
        App& app_;
    };

    Config cfg_;
    StartMode mode_;
    unsigned long loader_pid_ = 0;

    std::unique_ptr<PythonRuntimeService> python_;
    std::unique_ptr<PackageService> packages_;
    std::unique_ptr<GameService> game_;
    std::unique_ptr<LoaderService> loader_;
    std::unique_ptr<ModDiscoveryService> discovery_;
    std::unique_ptr<UpdateService> updates_;
    std::unique_ptr<ComponentUpdateService> components_updater_;
    std::unique_ptr<LaunchService> launch_;

    // Guards everything below it up to op_mutex_. Mutable: const getters read what workers write.
    mutable std::mutex state_mutex_;
    std::vector<Component> components_;
    std::vector<ModInfo> mods_;
    // What each manifest offered, by catalog id, so pressing UPDATE does not ask again.
    std::map<std::string, ReleaseManifest> manifests_;
    std::map<size_t, AppSnapshot::UpdateState> mod_updates_;
    AppSnapshot::UpdateState launcher_update_ = AppSnapshot::UpdateState::Unmanaged;
    std::string launcher_latest_;
    std::string pending_restart_;
    std::string mod_update_folder_;
    std::string detected_game_path_;
    ShowReason reason_;

    // Guards op_name_ only. Where both are held: state_mutex_ first, op_mutex_ second.
    mutable std::mutex op_mutex_;
    std::string op_name_;   // empty when idle

    std::atomic<bool> checking_{true};
    std::atomic<bool> play_running_{false};
    std::atomic<bool> play_done_{false};
    std::atomic<bool> stopping_{false};   // ends the test version's simulated waits
    // Only appended to from the window's thread, and joined on it.
    std::vector<std::thread> workers_;
};

}  // namespace oreo
