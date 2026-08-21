// app.h - the orchestrator that owns every service and holds the state the UI renders.
//
// All decisions live here; the UI only draws what it finds in a snapshot and calls back with
// "the user pressed this button". Long operations (version checks, pip, launching) run on
// worker threads so the window never freezes, and no two pip operations ever run at once.
#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "component_update.h"
#include "config.h"
#include "game_service.h"
#include "launch_service.h"
#include "loader_service.h"
#include "mod_discovery.h"
#include "package_service.h"
#include "python_runtime.h"
#include "settings_service.h"
#include "state.h"
#include "update_service.h"

namespace oreo {

// How the launcher was started. Started by the loader = allowed to stay invisible; started by
// the user = they asked to see the manager, so it always opens.
enum class StartMode {
    User = 0,
    FromLoader,
};

struct AppSnapshot {
    std::vector<Component> components;
    std::vector<ModInfo> mods;
    bool checking = true;
    std::string check_status;     // top-right line: "Checking for updates...", "Up to date", ...
    ShowReason reason;
    bool play_running = false;
    bool play_done = false;
    std::string play_message;
    std::string play_stage;      // "Waiting for No Man's Sky to open its window...", ...
    // A mod update in flight. The folder says which row to draw as busy; the stage is what that
    // row shows instead of the button. Empty folder means nothing is updating.
    std::string mod_update_folder;
    std::string mod_update_stage;
    // Non-empty while a file-touching operation runs, holding a human name for it ("updating
    // NMSpy"). Every button that would start another one goes flat for the duration.
    std::string busy_with;
    bool high_contrast = false;
    std::string game_version;
    // What the launcher was able to find out about a component's version. Shown even when the
    // answer is "nothing new" - an empty row leaves the player guessing whether the check ran.
    enum class UpdateState {
        Unmanaged = 0,   // not something the launcher updates; say nothing at all
        Unknown,         // it is, but the check did not get an answer (offline, 404, ...)
        Current,         // checked, nothing newer
        Available,       // checked, there is a newer version
    };

    // The launcher's own, shown in the status bar rather than as a component: it is not
    // something a mod requires, it is the program the player is looking at.
    UpdateState launcher_update = UpdateState::Unmanaged;
    std::string launcher_latest;
    std::string launcher_notes_url;
    // Per mod, by index into `mods`. Absent means Unmanaged.
    std::map<size_t, UpdateState> mod_updates;
    // DebugHotkeys diagnostics.
    std::string python_path, python_version, game_path, mods_path, loader_path, log_path;
};

class App {
public:
    App(StartMode mode, unsigned long loader_pid);
    ~App();

    // Loads config, opens the log, prepares services. Never fails fatally: a broken environment
    // is something the UI has to be able to explain.
    void init();

    // Runs local detection and the (network) update lookups. Blocking - called before the
    // window is created so a healthy setup never flashes a window.
    void run_checks();

    // Re-runs everything (the "Check Again" action).
    void recheck_async();

    // Whether the window should be shown, and why.
    const ShowReason& show_reason() const { return reason_; }

    AppSnapshot snapshot();
    const Config& config() const { return cfg_; }

    // User actions. Each runs on its own thread and only touches its own component.
    void run_action(const std::string& component_id);
    // Updates a mod the catalog knows about. `index` is into the snapshot's mod list.
    void update_mod_async(size_t index);
    // Replaces the launcher with the newest release. When it succeeds the caller is handed a
    // path to start and must then close the game gate and exit - see main.cpp.
    void update_launcher_async();
    // Set once the launcher has replaced itself: main() starts this and quits.
    std::string pending_restart() const;

    // Joins every worker thread. main() calls it before handing over to an updated launcher:
    // nothing from the old build may still be writing files when the new one starts. Safe to
    // call twice - join() leaves a thread not joinable, so the destructor's pass is a no-op.
    void wait_for_workers();
    bool toggle_mod(size_t index, std::string& error);
    void play_async();
    // Flipped from the window; remembered in config.ini so it survives a restart.
    void toggle_high_contrast();

    // ---- Settings window ---------------------------------------------------------------
    // Current values, plus the paths detection came up with so the window can show what
    // "automatic" actually resolved to and offer to go back to it.
    SettingsValues settings_values() const;
    std::string detected_game_path() const;
    std::string detected_mods_path() const;

    // Validates, writes config.ini, then re-detects the game and re-scans the mods folder so a
    // changed Mods folder takes effect immediately instead of on the next start.
    // Returns false and fills `errors` (per field) or `error` (writing failed) without changing
    // anything. Cancel simply never calls this.
    bool apply_settings(const SettingsValues& values, SettingsErrors& errors, std::string& error);
    bool play_finished() const { return play_done_.load(); }

    // Called once when the launcher exits silently (no window) - starts the mods.
    LaunchResult play_blocking();

private:
    void rebuild_components();
    void detect_local();
    void lookup_latest();
    // Asks every component that publishes a manifest what its newest release is.
    void lookup_component_manifests();
    void decide_show_reason();
    Component* find(const std::string& id);
    void set_busy(const std::string& id, bool busy, const std::string& progress);

    // ---- One file-touching operation at a time -----------------------------------------
    // Every button that downloads, installs, rescans, rewrites config or launches passes
    // through this gate. It replaces three guards that between them left four holes:
    // `Component::busy` only ever stopped a second click on the SAME row, so Loader + NMSpy +
    // Runtime could run at once; `mod_update_running_` covered mod archives alone; and
    // `pip_mutex_` covered pip, which left the Loader, the launcher's own update, "Check
    // Again" (it shells out to pip to read versions) and Settings->OK unguarded entirely.
    //
    // Deliberately try-and-refuse rather than a mutex: every one of these is entered on the UI
    // thread, which must never block. A refused click says what to wait for instead.
    //
    // `what` is a human fragment ("updating NMSpy") that ends up in that sentence. On refusal
    // `running_now`, when given, receives the fragment of whatever holds the gate - reading it
    // back separately would race with that operation finishing.
    bool begin_operation(const std::string& what, std::string* running_now = nullptr);
    void end_operation();
    std::string current_operation() const;

    // Releases the gate however a worker leaves - these threads have several early returns
    // each, and the hand-rolled version of this existed in only one of them.
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

    // What each component's manifest offered, kept so pressing Update does not have to ask
    // again. Keyed by catalog id.
    std::map<std::string, ReleaseManifest> manifests_;
    AppSnapshot::UpdateState launcher_update_ = AppSnapshot::UpdateState::Unmanaged;
    std::string launcher_latest_;
    std::string launcher_notes_url_;
    std::map<size_t, AppSnapshot::UpdateState> mod_updates_;
    std::string pending_restart_;
    // What detection found before any override was applied - the "Use auto-detected path" and
    // "Use default folder" buttons need it even while the user is editing the boxes.
    std::string detected_game_path_;

    // Mutable so the const getters can take it: they read state a worker thread writes.
    mutable std::mutex state_mutex_;   // guards components_/mods_/status strings
    // Guards op_name_ and nothing else. Lock order where both are held: state_mutex_ first,
    // op_mutex_ second, never the other way round.
    mutable std::mutex op_mutex_;
    std::string op_name_;         // what the gate is currently held for; empty when idle
    std::vector<Component> components_;
    std::vector<ModInfo> mods_;
    std::string check_status_;
    ShowReason reason_;

    std::atomic<bool> checking_{true};
    std::atomic<bool> play_running_{false};
    std::atomic<bool> play_done_{false};
    std::string play_message_;
    std::string play_stage_;
    std::string mod_update_folder_;
    std::string mod_update_stage_;
    std::vector<std::thread> workers_;
};

}  // namespace oreo
