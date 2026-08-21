#include "config.h"

#include "util.h"

namespace oreo {

namespace {

// Written verbatim the first time the launcher runs. Comments matter: this file is the
// documented way to turn developer features on, so it has to explain itself in Notepad.
const char* kDefaultConfig =
    "; ProjectOreo launcher configuration.\r\n"
    "; Plain text - edit in Notepad, no rebuild needed. Restart the launcher to apply.\r\n"
    "\r\n"
    "[Launcher]\r\n"
    "; AlwaysShowWindow=1 opens the manager on every game start, even when everything is\r\n"
    "; healthy. Set it to 0 to only see it when something needs attention.\r\n"
    "AlwaysShowWindow=1\r\n"
    "; HighContrast=1 switches to a black and white palette with bigger text and thick\r\n"
    "; borders, for anyone who finds the dark grey theme hard to read. The same switch is\r\n"
    "; in the launcher window itself.\r\n"
    "HighContrast=0\r\n"
    "; Leave empty to detect No Man's Sky automatically (Steam library scan).\r\n"
    "; Example: GamePath=D:\\Games\\steamapps\\common\\No Man's Sky\r\n"
    "GamePath=\r\n"
    "; Leave empty to use <GamePath>\\GAMEDATA\\MODS\r\n"
    "ModsPath=\r\n"
    "\r\n"
    "[Debug]\r\n"
    "; Same switch name and true/false format as fishing_mod.ini.\r\n"
    "; true also opens the window always and shows technical details.\r\n"
    "DebugHotkeys=false\r\n"
    "\r\n"
    "[Updates]\r\n"
    "; 0 = never contact the network. Installed versions are still detected locally.\r\n"
    "CheckForUpdates=1\r\n"
    "; Seconds before a version lookup gives up. Kept short so startup never stalls.\r\n"
    "TimeoutSeconds=6\r\n"
    "; How long a successful lookup is reused before asking the network again.\r\n"
    "CacheMinutes=180\r\n"
    "\r\n"
    "[Logging]\r\n"
    "; Verbose=1 adds command lines, pip output and timing to logs\\launcher.log\r\n"
    "Verbose=0\r\n"
    "\r\n"
    "[Python]\r\n"
    "; Private Python runtime. Leave empty to use <ProjectOreo>\\python\\python.exe\r\n"
    "; The system Python installation is never used, whatever this is set to.\r\n"
    "Dir=\r\n";

}  // namespace

Config Config::load() {
    Config cfg;
    cfg.oreo_dir = exe_dir();
    cfg.config_path = path_join(cfg.oreo_dir, "config.ini");
    cfg.mod_state_path = path_join(cfg.oreo_dir, "mod_states.ini");

    if (!file_exists(cfg.config_path)) write_file(cfg.config_path, kDefaultConfig);

    IniMap ini;
    if (ini_load(cfg.config_path, ini)) {
        cfg.debug_hotkeys = ini_get_bool(ini, "debug.debughotkeys", cfg.debug_hotkeys);
        cfg.always_show_window = ini_get_bool(ini, "launcher.alwaysshowwindow", cfg.always_show_window);
        cfg.high_contrast = ini_get_bool(ini, "launcher.highcontrast", cfg.high_contrast);
        cfg.game_path = ini_get(ini, "launcher.gamepath", "");
        cfg.mods_path = ini_get(ini, "launcher.modspath", "");
        cfg.check_for_updates = ini_get_bool(ini, "updates.checkforupdates", cfg.check_for_updates);
        cfg.timeout_seconds = ini_get_int(ini, "updates.timeoutseconds", cfg.timeout_seconds);
        cfg.cache_minutes = ini_get_int(ini, "updates.cacheminutes", cfg.cache_minutes);
        cfg.verbose = ini_get_bool(ini, "logging.verbose", cfg.verbose);
        cfg.python_dir = ini_get(ini, "python.dir", "");
    }

    if (cfg.timeout_seconds < 1) cfg.timeout_seconds = 1;
    if (cfg.timeout_seconds > 60) cfg.timeout_seconds = 60;
    if (cfg.cache_minutes < 0) cfg.cache_minutes = 0;
    if (cfg.python_dir.empty()) cfg.python_dir = path_join(cfg.oreo_dir, "python");

    return cfg;
}

}  // namespace oreo
