// preferences.cpp - the Settings window and the theme switch.
#include "app/app.h"
#include "system/files.h"
#include "system/ini.h"
#include "system/log.h"
#include "system/text.h"

namespace oreo {

SettingsValues App::settings_values() const { return SettingsService::from_config(cfg_); }

std::string App::detected_game_path() const { return detected_game_path_; }

std::string App::detected_mods_path() const {
    return SettingsService::default_mods_path(detected_game_path_.empty() ? game_->root() : detected_game_path_);
}

bool App::apply_settings(const SettingsValues& values, SettingsErrors& errors, std::string& error) {
    error.clear();
    if (test_mode()) {
        errors = SettingsErrors();
        error = "This is the test version - settings are only saved in the real launcher.";
        return false;
    }
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
    // Under the gate: every service reads cfg_, and moving the mods folder under a running mod
    // install sent the mod somewhere else.
    std::string running;
    if (!begin_operation("saving settings", &running)) {
        error = "Busy " + running + ". Wait for it to finish.";
        return false;
    }
    Operation gate(*this);

    if (!SettingsService::save(cfg_.config_path, values, error)) return false;

    const std::string previous_mods_dir = game_->mods_dir();
    // Every service holds this same Config, so changing it in place applies the new folders.
    cfg_.game_path = trim(values.game_path);
    cfg_.mods_path = trim(values.mods_path);
    cfg_.always_show_window = values.always_show_window;
    cfg_.check_for_updates = values.check_for_updates;
    cfg_.high_contrast = values.high_contrast;
    cfg_.debug_hotkeys = values.debug_hotkeys;
    cfg_.verbose = values.verbose;
    Log::set_verbose(cfg_.verbose);

    detected_game_path_ = GameService::autodetect_root(cfg_.oreo_dir);
    game_->detect();
    loader_->detect();
    rescan_mods();
    size_t found = 0;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        found = mods_.size();
    }
    rebuild_components();
    Log::info("settings applied: mods rescanned in " + game_->mods_dir() + " (" + std::to_string(found) + " found)" +
              (previous_mods_dir == game_->mods_dir() ? "" : ", folder changed from " + previous_mods_dir));
    return true;
}

void App::set_light_theme(bool light) {
    if (cfg_.light_theme == light) return;
    cfg_.light_theme = light;
    if (test_mode()) return;   // the test version remembers nothing
    // Through the same writer the Settings window uses, so the file keeps its comments.
    std::string text;
    read_file(cfg_.config_path, text);
    write_file(cfg_.config_path, ini_set(text, "Launcher", "LightTheme", light ? "1" : "0"));
    Log::info(std::string("theme: ") + (light ? "light" : "dark"));
}

}  // namespace oreo
