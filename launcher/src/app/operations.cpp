// operations.cpp - INSTALL and UPDATE: components, mods, and the launcher itself.
#include <algorithm>

#include "app/app.h"
#include "app/mod_readiness.h"
#include "model/component_ids.h"
#include "system/log.h"
#include "updates/approved_catalog.h"

namespace oreo {

namespace {

const char* kInstalling = "Installing...";
const char* kUpdating = "Updating...";

// The card that was pressed shows it is working until the worker leaves, however it leaves.
struct CardBusy {
    std::mutex& lock;
    std::string& folder;
    ~CardBusy() {
        std::lock_guard<std::mutex> guard(lock);
        folder.clear();
    }
};

}  // namespace

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
    if (busy) c->state = progress == kInstalling ? CompState::Installing : CompState::Updating;
}

ReleaseManifest App::manifest_for(const std::string& catalog_id) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    auto it = manifests_.find(catalog_id);
    return it != manifests_.end() ? it->second : ReleaseManifest();
}

void App::run_action(const std::string& component_id) {
    ActionKind action = ActionKind::None;
    std::string name;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        Component* c = find(component_id);
        if (!c || c->busy) return;
        action = c->action;
        name = c->name;
        if (action == ActionKind::None) return;
    }

    if (test_mode()) {
        const char* progress = action == ActionKind::Install ? kInstalling : kUpdating;
        simulate((action == ActionKind::Install ? "installing " : "updating ") + name,
                 [this, component_id, progress] {
                     if (Component* c = find(component_id)) {
                         c->busy = true;
                         c->progress = progress;
                     }
                 },
                 [this, component_id] {
                     if (Component* c = find(component_id)) {
                         c->busy = false;
                         c->progress.clear();
                     }
                 });
        return;
    }

    // Retry looks again rather than running pip again: what failed was the check. Before the
    // gate is claimed, because recheck_async() claims it itself.
    if (action == ActionKind::Retry) {
        recheck_async();
        return;
    }

    if (!claim_operation((action == ActionKind::Install ? "installing " : "updating ") + name)) return;
    workers_.emplace_back([this, component_id, action]() {
        Operation gate(*this);
        perform_component_action(component_id, action);
    });
}

bool App::perform_component_action(const std::string& component_id, ActionKind action) {
    const bool installing = action == ActionKind::Install;
    const CompState failed = installing ? CompState::InstallFailed : CompState::UpdateFailed;
    set_busy(component_id, true, installing ? kInstalling : kUpdating);

    // Takes the row out of its busy state with what the operation left behind.
    auto finish = [&](bool ok, const std::string& message, const OperationDetails& details,
                      CompState failed_state) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        Component* c = find(component_id);
        if (!c) return;
        c->busy = false;
        c->progress.clear();
        c->details = details;
        c->error = ok ? "" : message;
        if (!ok) c->state = failed_state;
    };
    // A rebuild is what turns the new version into "up to date" and takes the button off the
    // row; a failure keeps its failed state until the next check.
    auto succeeded = [&](const std::string& message) {
        rebuild_components();
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (Component* c = find(component_id)) c->message = message;
    };

    if (component_id == kIdGame) {
        set_busy(component_id, false, "");
        return false;
    }

    if (component_id == kIdLoader) {
        LoaderOpResult result = action == ActionKind::Install  ? loader_->install()
                                : action == ActionKind::Repair ? loader_->repair()
                                                               : loader_->update();
        finish(result.success, result.message, result.details, CompState::InstallFailed);
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (Component* c = find(component_id)) {
                c->installed = loader_->installed_version();
                c->latest = loader_->latest_version();
            }
        }
        if (result.success) rebuild_components();
        return result.success;
    }

    if (component_id == kIdRuntime) {
        // A wheel published on GitHub, not a PyPI package; its manifest was read during the check.
        const ApprovedCatalogEntry* entry = find_approved(kIdRuntime);
        InstallResult result;
        if (!entry) {
            result.message = "The Runtime is not in the catalog of things this launcher installs.";
        } else {
            result = components_updater_->install(*entry, manifest_for(kIdRuntime),
                                                  [this](const std::string& s) { set_busy(kIdRuntime, true, s); });
        }
        PackageProbe probe = packages_->probe({kPkgRuntime});
        finish(result.ok, result.message, result.details, failed);
        if (probe.ok) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (Component* c = find(component_id)) c->installed = probe.versions[kPkgRuntime];
        }
        if (result.ok) succeeded(result.message);
        return result.ok;
    }

    // pyMHF or NMSpy, always the newest release: pip resolves which works with which.
    const std::string package = component_id == kIdPymhf ? kPkgPymhf : kPkgNmspy;
    PackageOpResult result = installing ? packages_->install(package) : packages_->update(package);
    // NMSpy pulls pyMHF in and a pyMHF change can move NMSpy, so both are read back.
    PackageProbe probe = packages_->probe({kPkgPymhf, kPkgNmspy});
    finish(result.success, result.message, result.details, failed);
    if (probe.ok) {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (Component* c = find(kIdPymhf)) c->installed = probe.versions[kPkgPymhf];
        if (Component* c = find(kIdNmspy)) c->installed = probe.versions[kPkgNmspy];
    }
    if (result.success) succeeded(result.message);
    return result.success;
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
        error = "Busy " + running + ". Wait for it to finish.";
        return false;
    }
    Operation gate(*this);

    if (test_mode()) {   // made-up mods: the switch lives in memory only
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (index < mods_.size()) mods_[index].enabled = !mod.enabled;
        return true;
    }

    const bool enabled = !mod.enabled;
    if (!discovery_->set_enabled(mod, game_->mods_dir(), cfg_.mod_state_path, enabled, error)) {
        Log::error("could not toggle mod " + mod.name + ": " + error);
        return false;
    }
    rescan_mods();
    rebuild_components();
    Log::info("mod " + mod.name + (enabled ? " enabled" : " disabled"));
    return true;
}

void App::perform_mod_update(size_t index) {
    ModInfo mod;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (index >= mods_.size()) return;
        mod = mods_[index];
    }
    if (play_running_.load()) {
        Log::info("mod update refused: the game is starting");
        return;
    }
    const ApprovedCatalogEntry* entry = find_approved_mod(mod.folder);
    if (!entry) {
        Log::warn(mod.name + " is not a mod this launcher knows how to update");
        return;
    }

    InstallResult result = components_updater_->install(*entry, manifest_for(entry->id),
                                                        [](const std::string&) {});
    // The folder changed underneath: read it again rather than patch what the window shows.
    rescan_mods();
    lookup_component_manifests();
    rebuild_components();
    Log::info("mod update: " + result.message);
}

void App::prepare_mod_async(size_t index) {
    std::string folder, name;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (index >= mods_.size()) return;
        folder = mods_[index].folder;
        name = mods_[index].name;
    }
    const std::string what = "getting " + name + " ready";
    if (test_mode()) {
        simulate(what, [this, folder] { mod_update_folder_ = folder; }, [this] { mod_update_folder_.clear(); });
        return;
    }
    if (!claim_operation(what)) return;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        mod_update_folder_ = folder;
    }

    workers_.emplace_back([this, folder]() {
        Operation gate(*this);
        CardBusy card{state_mutex_, mod_update_folder_};

        // One step at a time, each worked out again after the one before - installing NMSpy
        // brings pyMHF along - and each tried once: a failure ends the run.
        std::vector<std::string> tried;
        for (;;) {
            AppSnapshot snap = snapshot();
            size_t index = snap.mods.size();
            for (size_t i = 0; i < snap.mods.size(); ++i)
                if (snap.mods[i].folder == folder) index = i;
            if (index == snap.mods.size()) break;   // the mod is gone from the folder

            const PrepareStep* next = nullptr;
            std::vector<PrepareStep> steps = prepare_steps(snap, index);
            for (const PrepareStep& step : steps) {
                if (std::find(tried.begin(), tried.end(), step.component_id) == tried.end()) {
                    next = &step;
                    break;
                }
            }
            if (!next) break;
            tried.push_back(next->component_id);
            Log::info("getting " + folder + " ready: " + to_string(next->action) + " " +
                      (next->component_id.empty() ? folder : next->component_id));
            if (next->component_id.empty()) {
                perform_mod_update(index);
            } else if (!perform_component_action(next->component_id, next->action)) {
                break;
            }
        }
    });
}

void App::update_launcher_async() {
    if (test_mode()) {   // busy for a while, then back: nothing is downloaded or replaced
        simulate(operation::kUpdatingLauncher, [] {}, [] {});
        return;
    }
    // Under the gate: replacing the exe while pip runs would close the window on a process
    // still working, and it overwrites the reference loader the Loader row installs from.
    if (!claim_operation(operation::kUpdatingLauncher)) return;
    workers_.emplace_back([this]() {
        Operation gate(*this);
        const ApprovedCatalogEntry* entry = find_approved(kIdLauncher);
        if (!entry) return;
        InstallResult result = components_updater_->install(
            *entry, manifest_for(kIdLauncher), [](const std::string& stage) { Log::info("launcher update: " + stage); });
        Log::info("launcher update: " + result.message);
        if (result.ok && result.restart_required) {
            // main() starts it once the window is gone; doing it from here would race the window.
            std::lock_guard<std::mutex> lock(state_mutex_);
            pending_restart_ = result.restart_exe;
        }
    });
}

std::string App::pending_restart() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return pending_restart_;
}

}  // namespace oreo
