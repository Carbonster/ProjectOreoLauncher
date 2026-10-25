// checks.cpp - what is installed, what is newest, and whether the window opens.
#include "app/app.h"
#include "app/component_status.h"
#include "app/window_rule.h"
#include "model/component_ids.h"
#include "system/log.h"
#include "system/text.h"
#include "updates/approved_catalog.h"

namespace oreo {

namespace {

AppSnapshot::UpdateState update_state(const UpdateCheck& check) {
    if (!check.ok) return AppSnapshot::UpdateState::Unknown;
    return check.update_available ? AppSnapshot::UpdateState::Available : AppSnapshot::UpdateState::Current;
}

std::string or_dash(const std::string& value, const char* dash) { return value.empty() ? dash : value; }

}  // namespace

void App::run_checks() {
    if (test_mode()) {
        load_test_state();
        checking_ = false;
        return;
    }
    checking_ = true;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (Component& c : components_) {
            if (c.busy) continue;   // the operation running on it owns its state
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

void App::recheck_async() {
    // Not read-only: detection runs pip to read the installed versions.
    if (test_mode()) {
        simulate(operation::kCheckingVersions, [] {}, [] {});
        return;
    }
    if (!claim_operation(operation::kCheckingVersions)) return;
    workers_.emplace_back([this]() {
        Operation gate(*this);
        run_checks();
    });
}

void App::rescan_mods() {
    discovery_->scan(game_->mods_dir(), cfg_.mod_state_path);
    std::lock_guard<std::mutex> lock(state_mutex_);
    mods_ = discovery_->mods();
}

void App::detect_local() {
    python_->detect();
    detected_game_path_ = GameService::autodetect_root(cfg_.oreo_dir);
    game_->detect();
    loader_->detect();
    restore_mods_after_run_without_mods();
    rescan_mods();
    Log::info("mods detected: " + std::to_string(mods_.size()));

    PackageProbe probe = packages_->probe({kPkgPymhf, kPkgNmspy, kPkgRuntime});
    std::lock_guard<std::mutex> lock(state_mutex_);
    for (Component& c : components_) {
        if (c.id == kIdPymhf || c.id == kIdNmspy || c.id == kIdRuntime) {
            const char* pkg = c.id == kIdPymhf ? kPkgPymhf : c.id == kIdNmspy ? kPkgNmspy : kPkgRuntime;
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
    Log::info("pyMHF installed: " + or_dash(probe.versions[kPkgPymhf], "(not installed)"));
    Log::info("NMSpy installed: " + or_dash(probe.versions[kPkgNmspy], "(not installed)"));
    Log::info("Runtime installed: " + or_dash(probe.versions[kPkgRuntime], "(not installed)"));
}

void App::lookup_latest() {
    // Failures are noted on the component; they never change what is installed or block anything.
    LatestInfo pymhf_latest = updates_->latest_pypi(kPkgPymhf);
    LatestInfo nmspy_latest = updates_->latest_pypi(kPkgNmspy);

    std::lock_guard<std::mutex> lock(state_mutex_);
    for (Component& c : components_) {
        const LatestInfo* latest = c.id == kIdPymhf ? &pymhf_latest : c.id == kIdNmspy ? &nmspy_latest : nullptr;
        if (!latest) continue;
        c.latest = latest->version;
        if (!latest->ok && c.error.empty()) c.error = latest->error;
    }
}

void App::lookup_component_manifests() {
    // Everything installed as a file rather than through pip - the Runtime, the launcher, the
    // mods the catalog knows - answers from its own manifest, so a release needs no new launcher.
    std::map<std::string, ReleaseManifest> found;
    auto ask = [&](const ApprovedCatalogEntry& entry, const std::string& installed) {
        UpdateCheck check = components_updater_->check(entry, installed);
        if (check.ok) found[entry.id] = check.manifest;
        return check;
    };

    std::string runtime_installed;
    std::vector<ModInfo> mods_now;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (Component* runtime = find(kIdRuntime)) runtime_installed = runtime->installed;
        mods_now = mods_;
    }

    if (const ApprovedCatalogEntry* entry = find_approved(kIdRuntime)) {
        UpdateCheck check = ask(*entry, runtime_installed);
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (Component* runtime = find(kIdRuntime)) {
            runtime->latest = check.latest;
            if (!check.ok && runtime->error.empty() && !check.error.empty()) runtime->error = check.error;
        }
    }

    AppSnapshot::UpdateState launcher_state = AppSnapshot::UpdateState::Unmanaged;
    std::string launcher_latest;
    if (const ApprovedCatalogEntry* entry = find_approved(kIdLauncher)) {
        UpdateCheck check = ask(*entry, PROJECTOREO_VERSION);
        launcher_state = update_state(check);
        launcher_latest = check.latest;
    }

    // A mod is matched to the catalog by its folder name only: its own files never choose which
    // entry, and so which download address, applies to it.
    std::map<size_t, AppSnapshot::UpdateState> mod_states;
    for (size_t i = 0; i < mods_now.size(); ++i) {
        if (const ApprovedCatalogEntry* entry = find_approved_mod(mods_now[i].folder))
            mod_states[i] = update_state(ask(*entry, mods_now[i].version));
    }

    std::lock_guard<std::mutex> lock(state_mutex_);
    manifests_ = found;
    launcher_update_ = launcher_state;
    launcher_latest_ = launcher_latest;
    mod_updates_ = mod_states;
}

void App::rebuild_components() {
    ComponentFacts facts;
    facts.python_available = python_->available();
    facts.python_error = python_->error();
    facts.game_found = game_->found();
    facts.game_build = game_->build();
    facts.loader_foreign = loader_->foreign();
    facts.loader_error = loader_->error();

    std::lock_guard<std::mutex> lock(state_mutex_);
    for (Component& c : components_) evaluate_component(c, facts);
    for (const Component& c : components_)
        Log::info("component " + c.id + ": installed=" + or_dash(c.installed, "-") +
                  " latest=" + or_dash(c.latest, "unknown") + " state=" + to_string(c.state));
}

void App::decide_show_reason() {
    AppSnapshot cards = snapshot();
    WindowRule rule;
    rule.always_show = cfg_.always_show_window;
    rule.debug_hotkeys = cfg_.debug_hotkeys;
    rule.started_by_user = mode_ == StartMode::User;
    ShowReason reason = decide_window(cards, rule);

    for (const Component& c : cards.components)
        if (c.needs_attention()) Log::info("not a reason to open: " + c.name + ": " + c.message);
    if (!python_->available()) Log::info("not a reason to open: private Python: " + python_->error());
    if (!discovery_->scanned_ok() && !discovery_->error().empty())
        Log::info("not a reason to open: mods: " + discovery_->error());

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        reason_ = reason;
    }
    for (const std::string& line : reason.reasons) Log::info("UI reason: " + line);
    Log::info(reason.show ? "showing the launcher window" : "no action required - skipping the launcher UI");
}

}  // namespace oreo
