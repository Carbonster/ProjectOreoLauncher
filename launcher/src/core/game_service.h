// game_service.h - GameService: where No Man's Sky is, which build it is, and is it running.
//
// The authoritative version of an NMS install is the build number in NMS.exe's file version
// (e.g. 170671). The marketing version ("6.6") is a label on top of that, resolved through an
// editable table - an unknown build shows its build number rather than a guess.
#pragma once

#include <string>

#include "config.h"

namespace oreo {

class GameService {
public:
    explicit GameService(const Config& cfg) : cfg_(cfg) {}

    void detect();

    bool found() const { return found_; }
    const std::string& root() const { return root_; }              // ...\No Man's Sky
    const std::string& binaries_dir() const { return binaries_; }  // ...\No Man's Sky\Binaries
    const std::string& exe_path() const { return exe_; }           // ...\Binaries\NMS.exe
    const std::string& mods_dir() const { return mods_; }          // ...\GAMEDATA\MODS
    const std::string& build() const { return build_; }            // "170671", empty if unreadable
    const std::string& marketing() const { return marketing_; }    // "6.6", empty if unknown
    const std::string& error() const { return error_; }
    std::string display_version() const;                           // "6.6" / "build 170671" / ""
    std::string how_found() const { return how_; }                 // for logs and debug diagnostics

    bool running() const { return pid_ != 0; }
    unsigned long pid() const { return pid_; }
    void refresh_running();

    static const int kSteamAppId = 275850;

    // The install detection finds on its own, ignoring any GamePath override. The Settings
    // window shows this as the auto-detected path and offers a button to go back to it.
    // `oreo_dir` is the folder holding the launcher exe. Returns "" when nothing was found.
    static std::string autodetect_root(const std::string& oreo_dir);

private:
    bool accept_root(const std::string& root, const std::string& how);
    void read_build();
    void resolve_marketing();

    const Config& cfg_;
    std::string root_, binaries_, exe_, mods_, build_, marketing_, error_, how_;
    unsigned long pid_ = 0;
    bool found_ = false;
};

}  // namespace oreo
