// state.h - the data model the UI renders. No logic beyond trivial derivations lives here.
//
// The three version concepts stay strictly separate and are never collapsed into each other:
//   installed - what is on this machine right now
//   latest    - what exists upstream (may be unknown: no network is not an error)
//   tested    - what a mod says it was developed against (may be unknown: mods may not declare)
#pragma once

#include <string>
#include <vector>

namespace oreo {

enum class CompState {
    Checking = 0,
    Ready,                       // installed, matches tested, nothing to do
    NotInstalled,
    UpdateAvailable,             // a newer release exists upstream
    InstalledNewerThanTested,    // works, but no mod was tested against this
    InstalledOlderThanTested,    // mod expects newer than what is installed
    UnknownVersion,              // installed, but the version could not be read
    CheckFailed,                 // the local detection itself failed
    Installing,
    Updating,
    InstallFailed,
    UpdateFailed,
};

enum class ActionKind {
    None = 0,
    Install,
    Update,     // take the newest release
    // UseTested is gone: the launcher no longer installs a version a mod names. Bringing
    // back a version picker, developer mode only, is v2 work - see TODO.md.
    Repair,
    Retry,
};

// Everything needed to explain a failed pip/loader operation without dumping it in the main UI.
struct OperationDetails {
    bool valid = false;
    std::string command;
    std::string exit_code;
    std::string out;
    std::string err;
    std::string python_path;
    std::string package_version;
    std::string timestamp;
};

// Which mod declared a tested version, so conflicting declarations can be shown as such.
struct TestedRef {
    std::string mod_name;
    std::string version;
};

struct Component {
    std::string id;          // "loader" | "pymhf" | "nmspy" | "game"
    std::string name;        // "ProjectOreo Loader"
    std::string subtitle;    // "Core runtime"

    std::string installed;   // empty = not installed / not detected
    std::string latest;      // empty = unknown (offline, lookup failed, or no source)
    std::string tested;      // consensus across mods; empty when unknown or conflicting
    std::vector<TestedRef> tested_by;
    bool tested_conflict = false;   // mods disagree about the tested version

    CompState state = CompState::Checking;
    std::string message;     // one line, human, shown in the status column
    std::string error;       // human explanation when something failed
    OperationDetails details;

    bool busy = false;       // an operation is running - buttons disabled, no parallel pip
    std::string progress;    // "Updating...", "Installing..."

    ActionKind action = ActionKind::None;

    // A component needs the user's attention when it is missing, failed, out of step with what
    // mods were tested against, or has an update waiting.
    bool needs_attention() const;
    // Fatal for the mods (not for the game): the component is simply not there.
    bool is_missing() const { return state == CompState::NotInstalled; }
};

struct ModInfo {
    std::string folder;          // "FishingMod"
    std::string path;            // full path to the mod folder
    std::string name;            // display name (manifest, else folder name)
    std::string description;     // optional human description from the manifest
    std::string summary;         // one line for the card; the full text stays in Details
    std::string version;         // mod version from the manifest, empty when undeclared
    std::string entry_point;     // python file pyMHF should run
    std::string thumbnail;       // optional image next to the manifest
    std::string manifest_path;   // empty when the mod ships no manifest yet
    bool has_manifest = false;
    bool enabled = true;
    bool is_python = false;      // launched by pyMHF
    bool is_unpacked = false;    // native game files: nested .MBIN / .EXML

    // Tested-against versions the mod declares. Empty string = the mod did not say.
    std::string tested_nms;
    std::string tested_nmspy;
    std::string tested_pymhf;
    std::string tested_loader;
    // Declaring a Runtime version is also how a mod says it needs the Runtime at all. Most
    // Python mods do not: they change game logic and never draw anything or read a controller.
    std::string tested_runtime;

    // True when the mod says it needs Project Oreo Runtime. A mod without a manifest has said
    // nothing, and nothing is not a requirement.
    bool needs_runtime() const { return !tested_runtime.empty(); }

    std::string tested_for(const std::string& component_id) const;
};

// Why the window was opened. Logged, and shown at the top of the UI.
struct ShowReason {
    bool show = false;
    std::string summary;               // "pyMHF update available"
    std::vector<std::string> reasons;  // every individual reason, for the log
};

std::string to_string(CompState state);
std::string to_string(ActionKind action);

}  // namespace oreo
