// mod_discovery.h - ModDiscoveryService: finds ProjectOreo mods in <No Man's Sky>\GAMEDATA\MODS.
//
// Every immediate subdirectory of GAMEDATA\MODS is one mod. A projectoreo.ini file and a
// pyMHF entry point add metadata and Python launch behavior, but neither is required.
#pragma once

#include <string>
#include <vector>

#include "model/state.h"
#include "system/ini.h"

namespace oreo {

class ModDiscoveryService {
public:
    // `mods_dir` is <game>\GAMEDATA\MODS. A missing folder is not an error: it just means the
    // player has not installed any mod yet.
    void scan(const std::string& mods_dir, const std::string& state_path);

    // A python mod's switch is ProjectOreo's own (mod_states.ini) and is what PLAY hands to
    // pyMHF. Every other mod is switched through the game's own per-mod Enabled flag in
    // Binaries\SETTINGS\GCMODSETTINGS.MXML.
    bool set_enabled(const ModInfo& mod, const std::string& mods_dir,
                     const std::string& state_path, bool enabled, std::string& error) const;

    // The game's own "disable all mods" switch in the same file. PLAY WITHOUT MODS turns it on
    // for one run and puts it back afterwards; `was` receives what it said before. Each mod's
    // own Enabled flag is left as it is, so putting it back restores exactly what was there.
    bool set_all_game_mods_disabled(const std::string& mods_dir, bool disabled, bool* was,
                                    std::string& error) const;

    const std::vector<ModInfo>& mods() const { return mods_; }
    const std::string& error() const { return error_; }
    bool scanned_ok() const { return scanned_ok_; }

    static const char* manifest_name();

    void set_backup_dir(const std::string& dir) { backup_dir_ = dir; }

private:
    bool read_manifest(const std::string& folder_path, ModInfo& mod) const;
    bool detect_pymhf_entry(const std::string& folder_path, ModInfo& mod) const;
    void scan_root(const std::string& root, const IniMap& states);

    std::vector<ModInfo> mods_;
    bool save_game_settings(const std::string& path, const std::string& xml) const;

    std::string error_;
    std::string backup_dir_;
    bool scanned_ok_ = false;
};

}  // namespace oreo
