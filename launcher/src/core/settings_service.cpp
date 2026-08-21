#include "settings_service.h"

#include "log.h"
#include "util.h"

namespace oreo {

namespace {

const char* kBool(bool value) { return value ? "1" : "0"; }

}  // namespace

bool SettingsService::is_game_folder(const std::string& path) {
    if (path.empty()) return false;
    return file_exists(path_join(path_join(path, "Binaries"), "NMS.exe"));
}

std::string SettingsService::default_mods_path(const std::string& game_path) {
    if (game_path.empty()) return "";
    return path_join(path_join(game_path, "GAMEDATA"), "MODS");
}

SettingsValues SettingsService::from_config(const Config& cfg) {
    SettingsValues values;
    values.game_path = cfg.game_path;
    values.mods_path = cfg.mods_path;
    values.always_show_window = cfg.always_show_window;
    values.check_for_updates = cfg.check_for_updates;
    values.high_contrast = cfg.high_contrast;
    values.debug_hotkeys = cfg.debug_hotkeys;
    values.verbose = cfg.verbose;
    return values;
}

SettingsValues SettingsService::defaults() {
    // Deliberately the same values the generated config.ini ships with: "Restore defaults" must
    // land on a first-install setup, not on some second set of defaults hidden in the code.
    SettingsValues values;
    values.game_path.clear();
    values.mods_path.clear();
    values.always_show_window = true;
    values.check_for_updates = true;
    values.high_contrast = false;
    values.debug_hotkeys = false;
    values.verbose = false;
    return values;
}

SettingsErrors SettingsService::validate(const SettingsValues& values) {
    SettingsErrors errors;

    std::string game = trim(values.game_path);
    if (!game.empty()) {
        if (!dir_exists(game)) {
            errors.game_path = "This folder does not exist.";
        } else if (!is_game_folder(game)) {
            errors.game_path =
                "No Man's Sky is not here. The folder you pick must contain Binaries\\NMS.exe.";
        }
    }

    std::string mods = trim(values.mods_path);
    if (!mods.empty() && !dir_exists(mods)) {
        errors.mods_path = "This folder does not exist.";
    }

    return errors;
}

bool SettingsService::save(const std::string& config_path, const SettingsValues& values,
                           std::string& error) {
    error.clear();

    std::string text;
    if (!read_file(config_path, text)) {
        // Not fatal: a missing config.ini just means we write the keys into a fresh file.
        Log::warn("settings: config.ini could not be read, writing a new one: " + config_path);
        text.clear();
    }

    text = ini_set(text, "Launcher", "AlwaysShowWindow", kBool(values.always_show_window));
    text = ini_set(text, "Launcher", "HighContrast", kBool(values.high_contrast));
    text = ini_set(text, "Launcher", "GamePath", trim(values.game_path));
    text = ini_set(text, "Launcher", "ModsPath", trim(values.mods_path));
    text = ini_set(text, "Debug", "DebugHotkeys", values.debug_hotkeys ? "true" : "false");
    text = ini_set(text, "Updates", "CheckForUpdates", kBool(values.check_for_updates));
    text = ini_set(text, "Logging", "Verbose", kBool(values.verbose));

    if (!write_file(config_path, text)) {
        error = "Settings could not be saved to " + config_path +
                ". Check that the file is not read-only.";
        Log::error("settings: " + error);
        return false;
    }

    Log::info("settings saved: GamePath=" + (values.game_path.empty() ? std::string("(auto)")
                                                                      : values.game_path) +
              " ModsPath=" + (values.mods_path.empty() ? std::string("(default)") : values.mods_path) +
              " AlwaysShowWindow=" + kBool(values.always_show_window) +
              " CheckForUpdates=" + kBool(values.check_for_updates) +
              " HighContrast=" + kBool(values.high_contrast) +
              " DebugHotkeys=" + (values.debug_hotkeys ? "true" : "false") +
              " Verbose=" + kBool(values.verbose));
    return true;
}

}  // namespace oreo
