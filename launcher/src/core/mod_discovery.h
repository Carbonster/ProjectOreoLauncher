// mod_discovery.h - ModDiscoveryService: finds ProjectOreo mods in <No Man's Sky>\GAMEDATA\MODS.
//
// Every immediate subdirectory of GAMEDATA\MODS is one mod. A projectoreo.ini file and a
// pyMHF entry point add metadata and Python launch behavior, but neither is required.
#pragma once

#include <string>
#include <vector>

#include "state.h"
#include "util.h"

namespace oreo {

class ModDiscoveryService {
public:
    // `mods_dir` is <game>\GAMEDATA\MODS. A missing folder is not an error: it just means the
    // player has not installed any mod yet.
    void scan(const std::string& mods_dir, const std::string& state_path);

    // Python launch state is saved by ProjectOreo; the game's own per-mod Enabled flag is
    // updated for every mod through Binaries\SETTINGS\GCMODSETTINGS.MXML.
    bool set_enabled(const ModInfo& mod, const std::string& mods_dir,
                     const std::string& state_path, bool enabled, std::string& error) const;

    const std::vector<ModInfo>& mods() const { return mods_; }
    const std::string& error() const { return error_; }
    bool scanned_ok() const { return scanned_ok_; }

    static const char* manifest_name();

private:
    bool read_manifest(const std::string& folder_path, ModInfo& mod) const;
    bool detect_pymhf_entry(const std::string& folder_path, ModInfo& mod) const;
    void scan_root(const std::string& root, const IniMap& states);

    std::vector<ModInfo> mods_;
    std::string error_;
    bool scanned_ok_ = false;
};

}  // namespace oreo
