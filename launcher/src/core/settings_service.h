// settings_service.h - the model behind the Settings window.
//
// config.ini stays the storage, but it stops being the interface: the window reads these values,
// validates what the user picked and writes it back. Everything here is pure enough to be tested
// without a window, which is the whole point of keeping it out of the UI layer.
#pragma once

#include <string>

#include "config.h"

namespace oreo {

// Exactly what the Settings window shows. An empty path means "use the automatic value" - that
// is a real, valid setting, not a missing one.
struct SettingsValues {
    std::string game_path;         // empty = detect No Man's Sky automatically
    std::string mods_path;         // empty = <game>\GAMEDATA\MODS
    bool always_show_window = true;
    bool check_for_updates = true;
    bool high_contrast = false;
    bool debug_hotkeys = false;
    bool verbose = false;
};

// One message per field, empty when that field is fine. Never a single "invalid input" blob:
// the window has to be able to point at the box that is actually wrong.
struct SettingsErrors {
    std::string game_path;
    std::string mods_path;

    bool ok() const { return game_path.empty() && mods_path.empty(); }
};

class SettingsService {
public:
    // Current values, as the window should show them.
    static SettingsValues from_config(const Config& cfg);

    // What "Restore defaults" puts in the form. Nothing is written until the user presses Save.
    static SettingsValues defaults();

    static SettingsErrors validate(const SettingsValues& values);

    // Rewrites config.ini in place. Comments and unrelated keys survive; the file a developer
    // hand-edited yesterday is still the file they will recognise tomorrow.
    static bool save(const std::string& config_path, const SettingsValues& values, std::string& error);

    // A No Man's Sky folder is one with Binaries\NMS.exe in it. That is the only check that
    // means anything - the folder name is whatever the player renamed it to.
    static bool is_game_folder(const std::string& path);

    // <game>\GAMEDATA\MODS: the path the launcher uses when the user set no override.
    static std::string default_mods_path(const std::string& game_path);
};

}  // namespace oreo
