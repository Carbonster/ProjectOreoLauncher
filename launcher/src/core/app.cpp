#include "app.h"

#include "game_gate.h"
#include "log.h"
#include "process.h"
#include "util.h"
#include "version.h"

namespace oreo {

namespace {

const char* kIdLoader = "loader";
const char* kIdPymhf = "pymhf";
const char* kIdNmspy = "nmspy";
const char* kIdRuntime = "runtime";
const char* kIdGame = "game";

// Distribution names, as the python environment knows them.
const char* kPkgPymhf = "pymhf";
const char* kPkgNmspy = "nmspy";
const char* kPkgRuntime = "project-oreo-runtime";

Component make_component(const char* id, const char* name, const char* subtitle) {
    Component c;
    c.id = id;
    c.name = name;
    c.subtitle = subtitle;
    return c;
}

// NMS.exe reports its build as a bare number ("170671"). Anything else means the version
// resource does not carry a build number we understand, and comparing it would be nonsense.
bool is_plain_build(const std::string& value) {
    if (value.empty()) return false;
    for (char c : value)
        if (c < '0' || c > '9') return false;
    return true;
}

// What a refused click says. Naming the operation that is in the way matters: "try again
// later" leaves the player clicking to find out when later is.
std::string busy_message(const std::string& running) {
    if (running.empty()) return "Another operation is running. Wait for it to finish.";
    return "Busy " + running + ". Wait for it to finish.";
}

}  // namespace

App::App(StartMode mode, unsigned long loader_pid) : mode_(mode), loader_pid_(loader_pid) {}
App::~App() { wait_for_workers(); }

void App::wait_for_workers() {
    // `workers_` is only ever appended to from the thread that owns the window, and this runs
    // on that same thread once it is gone, so the vector needs no lock of its own.
    for (std::thread& t : workers_)
        if (t.joinable()) t.join();
}

bool App::begin_operation(const std::string& what, std::string* running_now) {
    std::lock_guard<std::mutex> lock(op_mutex_);
    if (!op_name_.empty()) {
        if (running_now) *running_now = op_name_;
        return false;
    }
    op_name_ = what;
    return true;
}

void App::end_operation() {
    std::lock_guard<std::mutex> lock(op_mutex_);
    op_name_.clear();
}

std::string App::current_operation() const {
    std::lock_guard<std::mutex> lock(op_mutex_);
    return op_name_;
}

void App::init() {
    cfg_ = Config::load();
    Log::init(path_join(cfg_.oreo_dir, "logs"), cfg_.verbose);
    Log::info("----------------------------------------------------------------");
    Log::info("ProjectOreo launcher starting");
    Log::info(std::string("start mode: ") + (mode_ == StartMode::FromLoader ? "from loader" : "user"));
    if (loader_pid_) Log::info("game pid reported by the loader: " + std::to_string(loader_pid_));
    Log::info("AlwaysShowWindow=" + std::string(cfg_.always_show_window ? "1" : "0") +
              " DebugHotkeys=" + std::string(cfg_.debug_hotkeys ? "true" : "false") +
              " CheckForUpdates=" + (cfg_.check_for_updates ? "1" : "0") +
              " Verbose=" + (cfg_.verbose ? "1" : "0"));
    Log::info("config: " + cfg_.config_path);

    python_ = std::make_unique<PythonRuntimeService>(cfg_);
    packages_ = std::make_unique<PackageService>(*python_);
    game_ = std::make_unique<GameService>(cfg_);
    loader_ = std::make_unique<LoaderService>(cfg_, *game_);
    discovery_ = std::make_unique<ModDiscoveryService>();
    updates_ = std::make_unique<UpdateService>(cfg_);
    components_updater_ = std::make_unique<ComponentUpdateService>(cfg_, *game_, *python_);
    launch_ = std::make_unique<LaunchService>(cfg_, *game_, *python_);

    components_.clear();
    components_.push_back(make_component(kIdLoader, "ProjectOreo Loader", "Core runtime"));
    components_.push_back(make_component(kIdPymhf, "pyMHF", "Python mod framework"));
    components_.push_back(make_component(kIdNmspy, "NMSpy", "NMS Python bridge"));
    components_.push_back(
        make_component(kIdRuntime, "Project Oreo Runtime", "Overlay and controller support"));
    components_.push_back(make_component(kIdGame, "No Man's Sky", "Game"));
}

void App::run_checks() {
    checking_ = true;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        check_status_ = "Checking...";
        for (Component& c : components_) {
            // A row with an operation running owns its own state. rebuild_components() has
            // always known that; this loop did not, and reset the row to "Checking..." under
            // a running install. The gate makes that unreachable today - the two loops still
            // have to agree, or the next thing that relaxes the gate brings it straight back.
            if (c.busy) continue;
            c.state = CompState::Checking;
            c.message = "Checking...";
            c.error.clear();
        }
    }
    detect_local();
    lookup_latest();
    lookup_component_manifests();
    rebuild_components();
    decide_show_reason();
    checking_ = false;
}

void App::detect_local() {
    python_->detect();
    detected_game_path_ = GameService::autodetect_root(cfg_.oreo_dir);
    game_->detect();
    loader_->detect();
    discovery_->scan(game_->mods_dir(), cfg_.mod_state_path);

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        mods_ = discovery_->mods();
    }
    Log::info("mods detected: " + std::to_string(mods_.size()));

    PackageProbe probe = packages_->probe({kPkgPymhf, kPkgNmspy, kPkgRuntime});
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (Component& c : components_) {
        if (c.id == kIdPymhf || c.id == kIdNmspy || c.id == kIdRuntime) {
            const char* pkg = c.id == kIdPymhf ? kPkgPymhf
                              : c.id == kIdNmspy ? kPkgNmspy
                                                 : kPkgRuntime;
            c.installed = probe.ok ? probe.versions[pkg] : "";
            if (!probe.ok) {
                c.error = probe.error;
                c.details = probe.details;
            }
        } else if (c.id == kIdLoader) {
            c.installed = loader_->installed_version();
            c.latest = loader_->latest_version();
            c.error = loader_->error();
        } else if (c.id == kIdGame) {
            c.installed = game_->display_version();
            c.error = game_->error();
        }
    }
    Log::info("pyMHF installed: " + (probe.versions[kPkgPymhf].empty() ? std::string("(not installed)")
                                                                      : probe.versions[kPkgPymhf]));
    Log::info("NMSpy installed: " + (probe.versions[kPkgNmspy].empty() ? std::string("(not installed)")
                                                                      : probe.versions[kPkgNmspy]));
    Log::info("Runtime installed: " + (probe.versions[kPkgRuntime].empty()
                                           ? std::string("(not installed)")
                                           : probe.versions[kPkgRuntime]));
}

void App::lookup_latest() {
    // Network work only. Failures here are recorded on the component but never change its
    // installed state, and never block anything.
    LatestInfo pymhf_latest = updates_->latest_pypi(kPkgPymhf);
    LatestInfo nmspy_latest = updates_->latest_pypi(kPkgNmspy);

    std::lock_guard<std::mutex> lock(state_mutex_);
    for (Component& c : components_) {
        if (c.id == kIdPymhf) {
            c.latest = pymhf_latest.version;
            if (!pymhf_latest.ok && c.error.empty()) c.error = pymhf_latest.error;
        } else if (c.id == kIdNmspy) {
            c.latest = nmspy_latest.version;
            if (!nmspy_latest.ok && c.error.empty()) c.error = nmspy_latest.error;
        }
    }
    if (!pymhf_latest.ok || !nmspy_latest.ok) {
        check_status_ = updates_->enabled() ? "Unable to check for updates" : "Update checks are off";
    }
}

void App::lookup_component_manifests() {
    // Everything the launcher ships or installs as a file rather than through pip: the Runtime,
    // the launcher itself, and any mod the catalog knows. Each answers from its own manifest, so
    // a component can put out a release without a new launcher being built.
    std::map<std::string, ReleaseManifest> found;

    auto ask = [&](const ApprovedCatalogEntry& entry, const std::string& installed) {
        UpdateCheck check = components_updater_->check(entry, installed);
        if (check.ok) found[entry.id] = check.manifest;
        return check;
    };

    std::string runtime_installed;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (const Component& c : components_)
            if (c.id == kIdRuntime) runtime_installed = c.installed;
    }

    if (const ApprovedCatalogEntry* entry = find_approved(kIdRuntime)) {
        UpdateCheck check = ask(*entry, runtime_installed);
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (Component& c : components_) {
            if (c.id != kIdRuntime) continue;
            c.latest = check.latest;
            if (!check.ok && c.error.empty() && !check.error.empty()) c.error = check.error;
        }
    }

    auto state_of = [](const UpdateCheck& check) {
        if (!check.ok) return AppSnapshot::UpdateState::Unknown;
        return check.update_available ? AppSnapshot::UpdateState::Available
                                      : AppSnapshot::UpdateState::Current;
    };

    AppSnapshot::UpdateState launcher_state = AppSnapshot::UpdateState::Unmanaged;
    std::string launcher_latest, launcher_notes;
    if (const ApprovedCatalogEntry* entry = find_approved("launcher")) {
        UpdateCheck check = ask(*entry, PROJECTOREO_VERSION);
        launcher_state = state_of(check);
        launcher_latest = check.latest;
        launcher_notes = check.notes_url;
    }

    // Mods are matched to the catalog by their folder name. A folder called fishing_mod is the
    // Fishing Minigame Mod as far as updates go - the mod's own files never get a say in which
    // catalog entry, and therefore which download address, applies to it.
    std::map<size_t, AppSnapshot::UpdateState> mod_states;
    std::vector<ModInfo> mods_now;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        mods_now = mods_;
    }
    for (size_t i = 0; i < mods_now.size(); ++i) {
        for (const ApprovedCatalogEntry& entry : approved_catalog()) {
            if (entry.install_folder.empty()) continue;
            if (to_lower(entry.install_folder) != to_lower(mods_now[i].folder)) continue;
            UpdateCheck check = ask(entry, mods_now[i].version);
            mod_states[i] = state_of(check);
            break;
        }
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    manifests_ = found;
    launcher_update_ = launcher_state;
    launcher_latest_ = launcher_latest;
    launcher_notes_url_ = launcher_notes;
    mod_updates_ = mod_states;
}

void App::rebuild_components() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    bool any_update = false;
    bool any_problem = false;

    for (Component& c : components_) {
        if (c.busy) continue;   // an operation is in flight; leave its state alone

        const bool is_game = c.id == kIdGame;
        const bool installed = !c.installed.empty();

        // NMSpy encodes the game build it was made for in its own version ("170671.3" targets
        // build 170671). That is a real compatibility signal even when no mod declares one.
        std::string build_note;
        VersionOrder build_order = VersionOrder::Unknown;
        if (c.id == kIdNmspy && installed && is_plain_build(game_->build())) {
            std::string target = nmspy_target_build(c.installed);
            if (!target.empty() && target != game_->build()) {
                build_note = "Built for game build " + target + ", you have " + game_->build();
                build_order = compare_versions(target, game_->build());
            }
        }

        c.action = ActionKind::None;

        if (is_game) {
            if (!game_->found()) {
                c.state = CompState::CheckFailed;
                c.message = "No Man's Sky was not found";
                c.action = ActionKind::Retry;
                any_problem = true;
                continue;
            }
            if (!installed) {
                c.state = CompState::UnknownVersion;
                c.message = "Version could not be read";
                any_problem = true;
                continue;
            }
        } else if (c.id == kIdLoader && loader_->foreign()) {
            // Checked before "no version could be read": a version.dll owned by another mod has
            // no ProjectOreo version, but calling that "not installed" would hide the conflict.
            c.state = CompState::CheckFailed;
            c.message = "version.dll belongs to another program";
            c.error = loader_->error();
            c.action = ActionKind::None;
            any_problem = true;
            continue;
        } else if (!python_->available() &&
                   (c.id == kIdPymhf || c.id == kIdNmspy || c.id == kIdRuntime)) {
            // Without our own Python there is nothing to detect and nothing pip could fix:
            // name the real cause instead of reporting the package as broken.
            c.state = CompState::CheckFailed;
            c.message = "ProjectOreo's private Python is missing";
            c.error = python_->error();
            c.action = ActionKind::Retry;
            any_problem = true;
            continue;
        } else if (!c.error.empty() && !installed && c.id != kIdLoader) {
            c.state = CompState::CheckFailed;
            c.message = "Check failed";
            c.action = ActionKind::Retry;
            any_problem = true;
            continue;
        } else if (!installed) {
            c.state = CompState::NotInstalled;
            c.message = c.id == kIdLoader ? "Not installed in the game folder" : "Not installed";
            c.action = ActionKind::Install;
            any_problem = true;
            continue;
        }

        // Versions a mod declares under [Compatibility] are NOT used to decide anything. The
        // launcher installs and updates to the newest release, and pip resolves dependencies
        // between packages - that is its job, not ours. Pinning on the mod's word turned out to
        // be the wrong lever: it makes the launcher guess at a compatibility question only the
        // package authors can answer, and it silently held people back on old releases.
        //
        // Showing "tested with X" as information (green when it matches, amber when it does
        // not) and a version picker in developer mode are v2 work, see TODO.md.
        bool update_available = !c.latest.empty() && version_newer(c.latest, c.installed);

        if (update_available) {
            c.state = CompState::UpdateAvailable;
            c.message = "Update available";
            c.action = ActionKind::Update;
            any_update = true;
        } else if (!version_parsable(c.installed) && !is_game) {
            c.state = CompState::UnknownVersion;
            c.message = "Version could not be read";
            any_problem = true;
        } else {
            c.state = CompState::Ready;
            c.message = is_game ? "Compatible" : "Up to date";
        }

        if (!build_note.empty()) {
            // NMSpy aimed at a different game build than the one installed: worth the user's
            // attention, but it is still only advisory - nothing here blocks PLAY.
            if (c.state == CompState::Ready) {
                c.state = build_order == VersionOrder::Less ? CompState::InstalledOlderThanTested
                                                            : CompState::InstalledNewerThanTested;
                if (build_order == VersionOrder::Less && !c.latest.empty() &&
                    version_newer(c.latest, c.installed)) {
                    c.action = ActionKind::Update;
                }
                any_problem = true;
            }
            c.message = build_note;
        }
    }

    if (check_status_.empty() || check_status_ == "Checking...") {
        check_status_ = any_problem ? "Needs attention" : (any_update ? "Update available" : "Up to date");
    }

    for (const Component& c : components_)
        Log::info("component " + c.id + ": installed=" + (c.installed.empty() ? "-" : c.installed) +
                  " latest=" + (c.latest.empty() ? "unknown" : c.latest) +
                  " state=" + to_string(c.state));
}

void App::decide_show_reason() {
    ShowReason reason;
    std::lock_guard<std::mutex> lock(state_mutex_);

    for (const Component& c : components_) {
        if (!c.needs_attention()) continue;
        std::string line = c.name + ": " + c.message;
        reason.reasons.push_back(line);
        if (reason.summary.empty()) reason.summary = line;
    }
    if (!python_->available()) {
        reason.reasons.push_back("Private Python: " + python_->error());
        if (reason.summary.empty()) reason.summary = "ProjectOreo's private Python is missing";
    }
    if (!discovery_->scanned_ok() && !discovery_->error().empty()) {
        reason.reasons.push_back("Mods: " + discovery_->error());
        if (reason.summary.empty()) reason.summary = discovery_->error();
    }

    reason.show = !reason.reasons.empty();
    if (cfg_.always_show_window) {
        reason.reasons.push_back("AlwaysShowWindow=1 in config.ini");
        if (reason.summary.empty()) reason.summary = "Everything is ready";
        reason.show = true;
    }
    if (cfg_.debug_hotkeys) {
        reason.reasons.push_back("DebugHotkeys=true in config.ini");
        if (reason.summary.empty()) reason.summary = "Debug hotkeys are on";
        reason.show = true;
    }
    if (mode_ == StartMode::User) {
        reason.reasons.push_back("started by the user, not by the game");
        reason.show = true;
    }

    reason_ = reason;
    for (const std::string& line : reason.reasons) Log::info("UI reason: " + line);
    Log::info(reason.show ? "showing the launcher window" : "no action required - skipping the launcher UI");
}

AppSnapshot App::snapshot() {
    AppSnapshot snap;
    std::lock_guard<std::mutex> lock(state_mutex_);
    snap.components = components_;
    snap.mods = mods_;
    snap.checking = checking_.load();
    snap.check_status = check_status_;
    snap.reason = reason_;
    snap.play_running = play_running_.load();
    snap.play_done = play_done_.load();
    snap.play_message = play_message_;
    snap.play_stage = play_stage_;
    snap.mod_update_folder = mod_update_folder_;
    snap.mod_update_stage = mod_update_stage_;
    snap.busy_with = current_operation();
    snap.high_contrast = cfg_.high_contrast;
    snap.launcher_update = launcher_update_;
    snap.launcher_latest = launcher_latest_;
    snap.launcher_notes_url = launcher_notes_url_;
    snap.mod_updates = mod_updates_;
    snap.game_version = game_->display_version();
    snap.python_path = python_->exe();
    snap.python_version = python_->version();
    snap.game_path = game_->root();
    snap.mods_path = game_->mods_dir();
    snap.loader_path = loader_->installed_path();
    snap.log_path = Log::file_path();
    return snap;
}

Component* App::find(const std::string& id) {
    for (Component& c : components_)
        if (c.id == id) return &c;
    return nullptr;
}

void App::set_busy(const std::string& id, bool busy, const std::string& progress) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    Component* c = find(id);
    if (!c) return;
    c->busy = busy;
    c->progress = progress;
    if (busy) c->state = progress == "Installing..." ? CompState::Installing : CompState::Updating;
}

void App::run_action(const std::string& component_id) {
    ActionKind action = ActionKind::None;
    std::string name;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        Component* c = find(component_id);
        if (!c || c->busy) return;   // ignore a second click on a running operation
        action = c->action;
        name = c->name;
        if (action == ActionKind::None) return;
    }

    // Retry means "look again", never "run pip again": the thing that failed was the check.
    // Deliberately before the gate is claimed - recheck_async() claims it itself, and taking it
    // here first would make Retry refuse its own recheck.
    if (action == ActionKind::Retry) {
        recheck_async();
        return;
    }

    const bool installing = action == ActionKind::Install;
    std::string running;
    if (!begin_operation((installing ? "installing " : "updating ") + name, &running)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        play_message_ = busy_message(running);
        return;
    }

    workers_.emplace_back([this, component_id, action, installing]() {
        // Declared first so it is destroyed last: the gate outlives every scoped lock below.
        Operation gate(*this);
        set_busy(component_id, true, installing ? "Installing..." : "Updating...");

        if (component_id == kIdLoader) {
            LoaderOpResult result = action == ActionKind::Install ? loader_->install()
                                    : action == ActionKind::Repair ? loader_->repair()
                                                                   : loader_->update();
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                Component* c = find(component_id);
                if (c) {
                    c->busy = false;
                    c->progress.clear();
                    c->details = result.details;
                    c->error = result.success ? "" : result.message;
                    c->installed = loader_->installed_version();
                    c->latest = loader_->latest_version();
                    if (!result.success) c->state = CompState::InstallFailed;
                }
            }
            // Same as the Runtime below: without this the row keeps the Installing state it was
            // given when the work started.
            if (result.success) rebuild_components();
        } else if (component_id == kIdGame) {
            set_busy(component_id, false, "");
        } else if (component_id == kIdRuntime) {
            // Not a pip package from PyPI: a wheel published on GitHub, fetched and installed by
            // ComponentUpdateService. The manifest was already read during the version check.
            ReleaseManifest manifest;
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                auto it = manifests_.find(kIdRuntime);
                if (it != manifests_.end()) manifest = it->second;
            }
            const ApprovedCatalogEntry* entry = find_approved(kIdRuntime);
            InstallResult result;
            if (!entry) {
                result.message = "The Runtime is not in the catalog of things this launcher installs.";
            } else {
                result = components_updater_->install(*entry, manifest, [this](const std::string& s) {
                    set_busy(kIdRuntime, true, s);
                });
            }
            PackageProbe probe = packages_->probe({kPkgRuntime});
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                Component* c = find(component_id);
                if (c) {
                    c->busy = false;
                    c->progress.clear();
                    c->details = result.details;
                    c->error = result.ok ? "" : result.message;
                    if (probe.ok) c->installed = probe.versions[kPkgRuntime];
                    if (!result.ok) c->state = installing ? CompState::InstallFailed
                                                          : CompState::UpdateFailed;
                }
            }
            if (result.ok) {
                // Clearing busy is not enough: the state is still Installing, which the page
                // draws as running dots and a live button. Only a rebuild turns the freshly
                // installed version into "up to date" and takes the action off the row.
                rebuild_components();   // failures keep their failed state until the next check
                std::lock_guard<std::mutex> lock(state_mutex_);
                Component* c = find(component_id);
                if (c) c->message = result.message;
            }
        } else {
            const std::string package = component_id == kIdPymhf ? kPkgPymhf : kPkgNmspy;
            // Always the newest release, never a pinned one. Which pyMHF works with which NMSpy
            // is a question their own dependency metadata answers; pip resolves it. The launcher
            // guessing from a mod's [Compatibility] line only ever held people back.
            // Never two pip operations against the same environment at the same time: the
            // operation gate this worker holds is what guarantees it now, and it covers the
            // version probes in run_checks() too, which the old pip mutex did not.
            PackageOpResult result =
                installing ? packages_->install(package) : packages_->update(package);

            // NMSpy pulls pyMHF in, and a pyMHF change can move NMSpy - so after any package
            // operation both are read back, never just the one that was clicked.
            PackageProbe probe = packages_->probe({kPkgPymhf, kPkgNmspy});
            {
                std::lock_guard<std::mutex> lock(state_mutex_);
                Component* c = find(component_id);
                if (c) {
                    c->busy = false;
                    c->progress.clear();
                    c->details = result.details;
                    c->error = result.success ? "" : result.message;
                    if (!result.success) c->state = installing ? CompState::InstallFailed : CompState::UpdateFailed;
                }
                if (probe.ok) {
                    for (Component& other : components_) {
                        if (other.id == kIdPymhf) other.installed = probe.versions[kPkgPymhf];
                        if (other.id == kIdNmspy) other.installed = probe.versions[kPkgNmspy];
                    }
                }
            }
            if (result.success) {
                rebuild_components();   // failures keep their failed state until the next check
                std::lock_guard<std::mutex> lock(state_mutex_);
                Component* c = find(component_id);
                if (c) c->message = result.message;
            }
        }
    });
}

bool App::toggle_mod(size_t index, std::string& error) {
    ModInfo mod;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (index >= mods_.size()) {
            error = "The mod list changed. Please try again.";
            return false;
        }
        if (play_running_.load()) {
            error = "A mod cannot be changed while the game is starting.";
            return false;
        }
        mod = mods_[index];
    }

    std::string running;
    if (!begin_operation("changing a mod", &running)) {
        error = busy_message(running);
        return false;
    }
    Operation gate(*this);

    bool enabled = !mod.enabled;
    if (!discovery_->set_enabled(mod, game_->mods_dir(), cfg_.mod_state_path, enabled, error)) {
        Log::error("could not toggle mod " + mod.name + ": " + error);
        return false;
    }

    discovery_->scan(game_->mods_dir(), cfg_.mod_state_path);
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        mods_ = discovery_->mods();
    }
    rebuild_components();
    Log::info("mod " + mod.name + (enabled ? " enabled" : " disabled"));
    return true;
}

void App::update_mod_async(size_t index) {
    // One mod update at a time. Without this a second click simply starts a second worker on the
    // same paths: both unpack into the same staging folder and both delete it, so the loser can
    // leave a half-written mod behind. It also used to destroy the backup the first one had just
    // made. The page hides the button while an update runs, but a page is not a lock.
    //
    // That much is now the shared gate's job; what stays here is the folder name the row needs.
    std::string folder;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        folder = index < mods_.size() ? mods_[index].folder : std::string();
    }

    std::string running;
    if (!begin_operation("updating " + (folder.empty() ? std::string("a mod") : folder), &running)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        play_message_ = busy_message(running);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        mod_update_folder_ = folder;
        mod_update_stage_ = "Updating...";
    }

    workers_.emplace_back([this, index]() {
        // Declared first, so the gate is released after the row has stopped saying "Updating".
        Operation gate(*this);
        // Every path out of this thread has to clear the row, including the early returns.
        struct ReleaseOnExit {
            App* app;
            ~ReleaseOnExit() {
                std::lock_guard<std::mutex> lock(app->state_mutex_);
                app->mod_update_folder_.clear();
                app->mod_update_stage_.clear();
            }
        } release{this};

        ModInfo mod;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (index >= mods_.size()) return;
            if (play_running_.load()) {
                play_message_ = "A mod cannot be updated while the game is starting.";
                return;
            }
            mod = mods_[index];
        }

        const ApprovedCatalogEntry* entry = nullptr;
        for (const ApprovedCatalogEntry& candidate : approved_catalog()) {
            if (candidate.install_folder.empty()) continue;
            if (to_lower(candidate.install_folder) == to_lower(mod.folder)) {
                entry = &candidate;
                break;
            }
        }
        if (!entry) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            play_message_ = mod.name + " is not a mod this launcher knows how to update.";
            return;
        }

        ReleaseManifest manifest;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            auto it = manifests_.find(entry->id);
            if (it != manifests_.end()) manifest = it->second;
        }

        InstallResult result = components_updater_->install(*entry, manifest, [this](const std::string& s) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            mod_update_stage_ = s;
        });

        // The folder just changed underneath us, so what the window shows has to be re-read
        // rather than patched.
        discovery_->scan(game_->mods_dir(), cfg_.mod_state_path);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            mods_ = discovery_->mods();
            play_message_ = result.message;
        }
        lookup_component_manifests();
        rebuild_components();
        Log::info("mod update: " + result.message);
    });
}

void App::update_launcher_async() {
    // The one that used to have no guard at all. Replacing the running exe while pip is halfway
    // through installing NMSpy left the window closing on a process that was still working, and
    // it overwrites the reference loader that the Loader row's own button copies into the game.
    std::string running;
    if (!begin_operation("updating the launcher", &running)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        play_message_ = busy_message(running);
        return;
    }

    workers_.emplace_back([this]() {
        Operation gate(*this);
        const ApprovedCatalogEntry* entry = find_approved("launcher");
        ReleaseManifest manifest;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            auto it = manifests_.find("launcher");
            if (it != manifests_.end()) manifest = it->second;
        }
        if (!entry) return;

        InstallResult result = components_updater_->install(*entry, manifest, [this](const std::string& s) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            play_stage_ = s;
        });

        std::lock_guard<std::mutex> lock(state_mutex_);
        play_stage_.clear();
        play_message_ = result.message;
        if (result.ok && result.restart_required) {
            // main() takes it from here: it cancels the start gate, launches this path and
            // exits. Doing it from a worker thread would race with the window still being up.
            pending_restart_ = result.restart_exe;
        }
        Log::info("launcher update: " + result.message);
    });
}

std::string App::pending_restart() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return pending_restart_;
}

void App::recheck_async() {
    // Not a read-only button: detect_local() shells out to pip to read the installed versions,
    // so "Check Again" during an install was a second pip against the same environment.
    std::string running;
    if (!begin_operation("checking versions", &running)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        play_message_ = busy_message(running);
        return;
    }

    workers_.emplace_back([this]() {
        Operation gate(*this);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            check_status_ = "Checking...";
        }
        run_checks();
    });
}

LaunchResult App::play_blocking() {
    play_running_ = true;
    // The game is frozen inside the loader right now, so it cannot possibly become "ready"
    // until we let it go. Release first, then wait for its renderer, then inject.
    gate_release("PLAY");
    std::vector<ModInfo> mods;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        mods = mods_;
    }
    // The whole run goes through NMSpy's own mod loader, so NMSpy has to be there too - pyMHF
    // on its own can no longer start anything.
    bool pymhf_ready = false;
    bool nmspy_ready = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (const Component& c : components_) {
            if (c.id == kIdPymhf) pymhf_ready = !c.installed.empty();
            if (c.id == kIdNmspy) nmspy_ready = !c.installed.empty();
        }
    }
    bool framework_ready = pymhf_ready && nmspy_ready;
    if (!framework_ready)
        Log::warn(std::string("play: framework incomplete - pyMHF ") +
                  (pymhf_ready ? "installed" : "MISSING") + ", NMSpy " +
                  (nmspy_ready ? "installed" : "MISSING"));
    LaunchResult result = launch_->play(mods, loader_pid_, framework_ready, [this](const std::string& stage) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        play_stage_ = stage;
        Log::info("play stage: " + stage);
    });
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        play_message_ = result.message;
    }
    play_running_ = false;
    play_done_ = true;
    return result;
}

void App::toggle_high_contrast() {
    cfg_.high_contrast = !cfg_.high_contrast;
    // Written straight into config.ini so the choice is remembered, and through the same writer
    // the Settings window uses, so the file keeps its comments either way.
    std::string text;
    read_file(cfg_.config_path, text);
    write_file(cfg_.config_path,
               ini_set(text, "Launcher", "HighContrast", cfg_.high_contrast ? "1" : "0"));
    Log::info(std::string("high contrast: ") + (cfg_.high_contrast ? "on" : "off"));
}

SettingsValues App::settings_values() const { return SettingsService::from_config(cfg_); }

std::string App::detected_game_path() const { return detected_game_path_; }

std::string App::detected_mods_path() const {
    return SettingsService::default_mods_path(
        detected_game_path_.empty() ? game_->root() : detected_game_path_);
}

bool App::apply_settings(const SettingsValues& values, SettingsErrors& errors, std::string& error) {
    error.clear();
    errors = SettingsService::validate(values);
    if (!errors.ok()) {
        Log::warn("settings rejected: game=" + (errors.game_path.empty() ? "ok" : errors.game_path) +
                  " mods=" + (errors.mods_path.empty() ? "ok" : errors.mods_path));
        return false;
    }
    if (play_running_.load()) {
        error = "Settings cannot be changed while the game is starting.";
        return false;
    }
    // Runs on the UI thread and rewrites cfg_ in place, which every service reads - including
    // install_mod_archive(), which asks game_->mods_dir() for where to put the folder it has
    // already unpacked. Changing the mods folder under it sent the mod somewhere else.
    std::string running;
    if (!begin_operation("saving settings", &running)) {
        error = busy_message(running);
        return false;
    }
    Operation gate(*this);

    if (!SettingsService::save(cfg_.config_path, values, error)) return false;

    const std::string previous_mods_dir = game_->mods_dir();

    // Every service reads this same Config object, so updating it in place is what makes the new
    // folders take effect without a restart.
    cfg_.game_path = trim(values.game_path);
    cfg_.mods_path = trim(values.mods_path);
    cfg_.always_show_window = values.always_show_window;
    cfg_.check_for_updates = values.check_for_updates;
    cfg_.high_contrast = values.high_contrast;
    cfg_.debug_hotkeys = values.debug_hotkeys;
    cfg_.verbose = values.verbose;
    Log::set_verbose(cfg_.verbose);

    // A changed game or mods folder has to be visible immediately: re-detect, then re-scan.
    detected_game_path_ = GameService::autodetect_root(cfg_.oreo_dir);
    game_->detect();
    loader_->detect();
    discovery_->scan(game_->mods_dir(), cfg_.mod_state_path);
    size_t found = 0;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        mods_ = discovery_->mods();
        found = mods_.size();
    }
    rebuild_components();
    Log::info("settings applied: mods rescanned in " + game_->mods_dir() + " (" +
              std::to_string(found) + " found)" +
              (previous_mods_dir == game_->mods_dir()
                   ? ""
                   : ", folder changed from " + previous_mods_dir));
    return true;
}

void App::play_async() {
    if (play_running_.load()) return;
    std::string running;
    if (!begin_operation("starting the game", &running)) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        play_message_ = busy_message(running);
        return;
    }
    play_running_ = true;
    workers_.emplace_back([this]() {
        Operation gate(*this);
        play_blocking();
    });
}

}  // namespace oreo
