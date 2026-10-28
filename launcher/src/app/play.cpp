// play.cpp - PLAY and PLAY WITHOUT MODS.
#include <windows.h>

#include "app/app.h"
#include "game/game_gate.h"
#include "model/component_ids.h"
#include "system/files.h"
#include "system/ini.h"
#include "system/log.h"
#include "system/process.h"
#include "system/text.h"

namespace oreo {

namespace {

const char* kStartingGame = "starting the game";

}  // namespace

void App::play_async() {
    if (play_running_.load()) return;
    if (test_mode()) {   // STARTING for a while, then back: no game is started
        simulate(kStartingGame, [this] { play_running_ = true; }, [this] { play_running_ = false; });
        return;
    }
    if (!claim_operation(kStartingGame)) return;
    play_running_ = true;
    workers_.emplace_back([this]() {
        Operation gate(*this);
        play_blocking();
    });
}

LaunchResult App::play_blocking() {
    play_running_ = true;
    // The game is held inside the loader, so it cannot get ready until it is let go: release,
    // then wait for its window, then inject. The player just pressed PLAY here, so this process
    // may hand the game the right to come to the front when its window opens.
    if (loader_pid_) AllowSetForegroundWindow(loader_pid_);
    gate_release("PLAY");

    std::vector<ModInfo> mods;
    bool pymhf_ready = false;
    bool nmspy_ready = false;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        mods = mods_;
        if (Component* c = find(kIdPymhf)) pymhf_ready = !c->installed.empty();
        if (Component* c = find(kIdNmspy)) nmspy_ready = !c->installed.empty();
    }
    // Every run goes through NMSpy's own mod loader, so both have to be there.
    const bool framework_ready = pymhf_ready && nmspy_ready;
    if (!framework_ready)
        Log::warn(std::string("play: framework incomplete - pyMHF ") + (pymhf_ready ? "installed" : "MISSING") +
                  ", NMSpy " + (nmspy_ready ? "installed" : "MISSING"));
    const bool loader_installed = loader_->present() && !loader_->foreign();
    LaunchResult result = launch_->play(mods, loader_pid_, framework_ready,
                                        [](const std::string& stage) { Log::info("play stage: " + stage); },
                                        loader_installed);
    Log::info("play: " + result.message);
    play_running_ = false;
    play_done_ = true;
    return result;
}

void App::play_without_mods_async() {
    if (play_running_.load()) return;
    if (test_mode()) {
        simulate(kStartingGame, [this] { play_running_ = true; }, [this] { play_running_ = false; });
        return;
    }
    if (!claim_operation(kStartingGame)) return;
    play_running_ = true;
    workers_.emplace_back([this]() {
        Operation gate(*this);
        // Before the game reads its settings: it is still held by the loader, or not started.
        switch_game_mods_off_for_one_run();
        if (!gate_released()) {
            // The game waits inside the loader for this launcher: letting it go and starting
            // nothing is the whole of playing without mods. Nothing waits for its window here,
            // so it may come to the front on its own.
            if (loader_pid_) AllowSetForegroundWindow(loader_pid_);
            gate_release("PLAY WITHOUT MODS");
            Log::info("play without mods: released the game, no mods started");
        } else {
            LaunchResult result = launch_->start_without_mods();
            Log::info("play without mods: " + result.message);
        }
        play_running_ = false;
        play_done_ = true;
    });
}

std::string App::game_exe() const { return game_->found() ? game_->exe_path() : ""; }

std::string App::game_binaries_dir() const { return game_->binaries_dir(); }

bool App::take_start_without_mods() { return LaunchService::take_start_without_mods(cfg_.oreo_dir); }

std::string App::mods_off_record_path() const { return path_join(cfg_.oreo_dir, "mods_off_for_one_run.ini"); }

void App::switch_game_mods_off_for_one_run() {
    // A record already there is a run without mods not yet put back: it holds the player's own
    // setting, which switching off again must not overwrite with "off".
    const bool pending = file_exists(mods_off_record_path());
    bool was = false;
    std::string error;
    if (!discovery_->set_all_game_mods_disabled(game_->mods_dir(), true, &was, error)) {
        Log::warn("play without mods: the game's own mods could not be switched off - " + error);
        return;
    }
    if (!pending)
        write_file(mods_off_record_path(),
                   std::string("; Written by PLAY WITHOUT MODS. The next launcher start puts the game's\r\n"
                               "; \"disable all mods\" back to this and deletes the file.\r\n"
                               "[Restore]\r\nDisableAllMods=") + (was ? "true" : "false") + "\r\n");
    Log::info(std::string("play without mods: the game's own mods are switched off for this run") +
              (was ? " (they already were)" : ""));
}

void App::restore_mods_after_run_without_mods() {
    const std::string record = mods_off_record_path();
    IniMap ini;
    if (!ini_load(record, ini)) return;
    // Opened by hand while that game still runs: it is the run without mods, so they stay off.
    if (mode_ == StartMode::User && find_process_id("NMS.exe")) {
        Log::info("the game's own mods stay off until the next start: the game started without "
                  "mods is still running");
        return;
    }
    const bool disable_all = ini_get_bool(ini, "restore.disableallmods", false);
    std::string error;
    if (!discovery_->set_all_game_mods_disabled(game_->mods_dir(), disable_all, nullptr, error)) {
        Log::warn("could not put the game's own mods back after a run without mods - " + error);
        return;   // the record stays, so the next start tries again
    }
    DeleteFileW(to_wide(record).c_str());
    Log::info(std::string("the game's own mods are back as they were before the run without mods") +
              (disable_all ? " (all off, as the player had them)" : ""));
}

}  // namespace oreo
