// config.h - ConfigService. Plain human editable config.ini living next to the launcher exe.
// Anything a user or developer might need to flip without a rebuild belongs here.
#pragma once

#include <string>

namespace oreo {

struct Config {
    // [Launcher]
    bool debug_hotkeys = false;    // same developer switch name and true/false format as the mod
    // 1 = the manager window opens on every game start, even when nothing needs attention.
    // Default on: most people want to see what is going on before the game starts.
    bool always_show_window = true;
    // Accessibility: a much stronger palette (pure black/white, thick borders, bigger text)
    // for anyone who cannot read low-contrast grey on dark grey.
    bool high_contrast = false;
    std::string game_path;         // override for the No Man's Sky root; empty = autodetect
    std::string mods_path;         // override for GAMEDATA\MODS; empty = <game>\GAMEDATA\MODS

    // [Updates]
    bool check_for_updates = true;
    int timeout_seconds = 6;       // per HTTP request; short on purpose, startup must stay fast
    int cache_minutes = 180;       // reuse cached "latest version" answers for this long

    // [Logging]
    bool verbose = false;

    // [Python]
    std::string python_dir;        // override for the private runtime; empty = <oreo>\python

    // Resolved at load time (not read from the file).
    std::string oreo_dir;          // directory holding the launcher exe (ProjectOreo root)
    std::string config_path;
    std::string mod_state_path;     // launcher-owned per-mod enable switches

    // Loads <exe dir>\config.ini, writing a commented default file when it does not exist yet.
    static Config load();
};

}  // namespace oreo
