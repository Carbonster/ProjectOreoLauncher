// snapshot.h - a copy of everything the window draws, taken under the App's lock.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "model/state.h"

namespace oreo {

// What the operation gate is held for, when the window shows that operation its own way.
namespace operation {
inline constexpr char kCheckingVersions[] = "checking versions";
inline constexpr char kUpdatingLauncher[] = "updating the launcher";
}  // namespace operation

struct AppSnapshot {
    // What the launcher found out about a release. Unmanaged: not something it updates.
    enum class UpdateState {
        Unmanaged = 0,
        Unknown,     // the check got no answer (offline, 404, ...)
        Current,
        Available,
    };

    std::vector<Component> components;
    std::vector<ModInfo> mods;
    // By index into `mods`. Absent means Unmanaged.
    std::map<size_t, UpdateState> mod_updates;

    bool checking = true;
    bool play_running = false;
    bool play_done = false;
    // The card whose INSTALL / UPDATE is running; empty when none is.
    std::string mod_update_folder;
    // What the operation gate is held for ("updating NMSpy"); empty when idle. While it is set,
    // every button that would start another operation is flat.
    std::string busy_with;

    UpdateState launcher_update = UpdateState::Unmanaged;
    std::string launcher_latest;

    bool high_contrast = false;
    bool light_theme = false;
    std::string game_version;
    std::string python_version;
    std::string game_path;
    std::string mods_path;
    bool test_mode = false;
};

}  // namespace oreo
