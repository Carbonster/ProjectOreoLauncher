#include "state.h"

namespace oreo {

bool Component::needs_attention() const {
    switch (state) {
        case CompState::Ready:
        case CompState::Checking:
        case CompState::Installing:
        case CompState::Updating:
            return false;
        default:
            return true;
    }
}

std::string ModInfo::tested_for(const std::string& component_id) const {
    if (component_id == "game") return tested_nms;
    if (component_id == "nmspy") return tested_nmspy;
    if (component_id == "pymhf") return tested_pymhf;
    if (component_id == "loader") return tested_loader;
    if (component_id == "runtime") return tested_runtime;
    return "";
}

std::string to_string(CompState state) {
    switch (state) {
        case CompState::Checking: return "checking";
        case CompState::Ready: return "ready";
        case CompState::NotInstalled: return "not_installed";
        case CompState::UpdateAvailable: return "update_available";
        case CompState::InstalledNewerThanTested: return "installed_newer_than_tested";
        case CompState::InstalledOlderThanTested: return "installed_older_than_tested";
        case CompState::UnknownVersion: return "unknown_version";
        case CompState::CheckFailed: return "check_failed";
        case CompState::Installing: return "installing";
        case CompState::Updating: return "updating";
        case CompState::InstallFailed: return "install_failed";
        case CompState::UpdateFailed: return "update_failed";
    }
    return "unknown";
}

std::string to_string(ActionKind action) {
    switch (action) {
        case ActionKind::None: return "";
        case ActionKind::Install: return "Install";
        case ActionKind::Update: return "Update";
        case ActionKind::Repair: return "Repair";
        case ActionKind::Retry: return "Retry";
    }
    return "";
}

}  // namespace oreo
