#include "app/component_status.h"

#include "model/component_ids.h"
#include "updates/version.h"

namespace oreo {

namespace {

// NMS.exe reports its build as a bare number ("170671"). Anything else is not a build number
// we understand, and comparing it would be nonsense.
bool is_plain_build(const std::string& value) {
    if (value.empty()) return false;
    for (char c : value)
        if (c < '0' || c > '9') return false;
    return true;
}

void set(Component& c, CompState state, const char* message, ActionKind action) {
    c.state = state;
    c.message = message;
    c.action = action;
}

}  // namespace

void evaluate_component(Component& c, const ComponentFacts& facts) {
    if (c.busy) return;

    const bool is_game = c.id == kIdGame;
    const bool installed = !c.installed.empty();
    const bool is_package = c.id == kIdPymhf || c.id == kIdNmspy || c.id == kIdRuntime;

    // NMSpy carries the game build it was made for in its version ("170671.3" is for 170671).
    std::string build_note;
    VersionOrder build_order = VersionOrder::Unknown;
    if (c.id == kIdNmspy && installed && is_plain_build(facts.game_build)) {
        std::string target = nmspy_target_build(c.installed);
        if (!target.empty() && target != facts.game_build) {
            build_note = "Built for game build " + target + ", you have " + facts.game_build;
            build_order = compare_versions(target, facts.game_build);
        }
    }

    c.action = ActionKind::None;

    if (is_game) {
        if (!facts.game_found) return set(c, CompState::CheckFailed, "No Man's Sky was not found", ActionKind::Retry);
        if (!installed) return set(c, CompState::UnknownVersion, "Version could not be read", ActionKind::None);
    } else if (c.id == kIdLoader && facts.loader_foreign) {
        // Before "not installed": a version.dll of another mod has no ProjectOreo version, and
        // calling it missing would hide the conflict.
        c.error = facts.loader_error;
        return set(c, CompState::CheckFailed, "version.dll belongs to another program", ActionKind::None);
    } else if (!facts.python_available && is_package) {
        // Without our own Python there is nothing to detect and nothing pip could fix.
        c.error = facts.python_error;
        return set(c, CompState::CheckFailed, "ProjectOreo's private Python is missing", ActionKind::Retry);
    } else if (!c.error.empty() && !installed && c.id != kIdLoader) {
        return set(c, CompState::CheckFailed, "Check failed", ActionKind::Retry);
    } else if (!installed) {
        return set(c, CompState::NotInstalled,
                   c.id == kIdLoader ? "Not installed in the game folder" : "Not installed", ActionKind::Install);
    }

    // Always the newest release. Versions a mod declares under [Compatibility] decide nothing:
    // which package works with which is for pip and the package authors to answer.
    if (!c.latest.empty() && version_newer(c.latest, c.installed)) {
        set(c, CompState::UpdateAvailable, "Update available", ActionKind::Update);
    } else if (!version_parsable(c.installed) && !is_game) {
        set(c, CompState::UnknownVersion, "Version could not be read", ActionKind::None);
    } else {
        set(c, CompState::Ready, is_game ? "Compatible" : "Up to date", ActionKind::None);
    }

    if (!build_note.empty()) {
        // Advisory only: nothing here blocks PLAY.
        if (c.state == CompState::Ready) {
            c.state = build_order == VersionOrder::Less ? CompState::InstalledOlderThanTested
                                                        : CompState::InstalledNewerThanTested;
            if (build_order == VersionOrder::Less && !c.latest.empty() && version_newer(c.latest, c.installed))
                c.action = ActionKind::Update;
        }
        c.message = build_note;
    }
}

}  // namespace oreo
