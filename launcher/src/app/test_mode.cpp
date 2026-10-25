// test_mode.cpp - the test version of the launcher window.
//
// Opened from Settings as a second process (`--test-ui`). It shows the real window, drawn by the
// real code, on twenty made-up mods: three need something installed, five have an update. The
// buttons go through the same states the real ones do - busy dots, everything else flat - and
// after kTestOperationMs come back exactly as they were, so they can be pressed again. Nothing
// is detected, downloaded, installed or written, apart from the test version's own log.
//
// The made-up state goes through the real decisions: which card shows INSTALL or UPDATE is
// worked out by App::prepare_steps() from the components below, and the order is the window's
// own sort. Nothing here puts a mod anywhere on purpose.
#include "app/app.h"
#include "model/component_ids.h"

#include <chrono>
#include <thread>

#include "system/log.h"

namespace oreo {

namespace {

const int kTestOperationMs = 10000;

struct TestMod {
    const char* name;
    const char* version;      // "" = the mod does not say
    bool python;
    bool needs_runtime;       // with the Runtime missing, this is what makes a card say INSTALL
    bool update;              // a newer release of the mod itself: the card says UPDATE
    bool enabled;
    const char* description;
};

const TestMod kTestMods[] = {
    {"Fishing Minigame Mod", "0.5.2", true, true, false, true,
     "Adds a fish-catching minigame to No Man's Sky. Hold the line while the fish runs, keep it in "
     "the catch bar, and reel it in. Difficulty and physics are configurable. Requires Project Oreo "
     "Runtime for the overlay and controller support."},
    {"ProjectCamera Ship Nodes", "0.1.0", true, true, false, true,
     "Camera points around your ship for screenshots and cinematic flybys."},
    {"Warp Camera", "1.14", true, true, false, false,
     "Keeps the camera outside the ship while it jumps between systems."},
    {"Scanner Range Tweaks", "2.1.0", true, false, true, true,
     "Longer scanner range, shorter cooldown."},
    {"Freighter Autopilot", "0.9.3", true, false, true, false, ""},
    {"Widgets Demo", "", true, false, false, true,
     "Every widget the Runtime overlay offers, on one page. For mod authors."},
    {"Photo Mode Plus", "1.3.0", true, false, true, true,
     "More range, more filters and a free camera for photo mode."},
    {"Galaxy Map Bookmarks", "0.4.1", true, false, false, true, ""},
    {"Ship Damage Indicator", "1.0.0", true, false, false, false,
     "Shows which ship parts are damaged without opening the inventory."},
    {"Exocraft Garage Extended Controller Support Pack", "0.2.0", true, false, false, true,
     "A name long enough to be cut on its card."},
    {"Better Freighter Interior Overhaul Extended Edition", "3.2", false, false, true, true,
     "New rooms and lighting for every freighter."},
    {"Clean Space Skybox", "1.0", false, false, false, true, ""},
    {"Faster Refiners", "1.1", false, false, true, true, "Refiners work twice as fast."},
    {"No Fog Planets", "", false, false, false, true, ""},
    {"Brighter Night Sky", "2.0", false, false, false, false, ""},
    {"Quiet Mining Laser", "1.0", false, false, false, true, ""},
    {"Bigger Inventory Stacks", "4.0", false, false, false, true, ""},
    {"Colorful Atlas Stations", "", false, false, false, true, ""},
    {"Reduced Sentinel Patrols", "0.8", false, false, false, true, ""},
    {"+Short Loading Screens", "1.0", false, false, false, true, ""},
};

void set_component(Component& c, const char* installed, const char* latest, CompState state,
                   const char* message, ActionKind action) {
    c.installed = installed;
    c.latest = latest;
    c.state = state;
    c.message = message;
    c.action = action;
    c.error.clear();
    c.busy = false;
    c.progress.clear();
}

}  // namespace

void App::load_test_state() {
    std::lock_guard<std::mutex> lock(state_mutex_);
    mods_.clear();
    mod_updates_.clear();
    for (const TestMod& t : kTestMods) {
        ModInfo m;
        m.folder = "test-mod-" + std::to_string(mods_.size());
        m.name = t.name;
        m.version = t.version;
        m.description = t.description;
        m.is_python = t.python;
        m.enabled = t.enabled;
        if (t.python) m.entry_point = "main.py";
        if (t.needs_runtime) m.tested_runtime = "0.3.1";
        if (t.update) mod_updates_[mods_.size()] = AppSnapshot::UpdateState::Available;
        mods_.push_back(m);
    }

    // NMSpy and pyMHF are up to date: one out of date would put UPDATE on every Python mod. The
    // Runtime is missing, which only the mods that ask for it care about.
    for (Component& c : components_) {
        if (c.id == kIdLoader)
            set_component(c, "0.1.16", "0.1.16", CompState::Ready, "Up to date", ActionKind::None);
        else if (c.id == kIdPymhf)
            set_component(c, "0.2.3", "0.2.3", CompState::Ready, "Up to date", ActionKind::None);
        else if (c.id == kIdNmspy)
            set_component(c, "170671.3", "170671.3", CompState::Ready, "Up to date", ActionKind::None);
        else if (c.id == kIdRuntime)
            set_component(c, "", "0.3.1", CompState::NotInstalled, "Not installed", ActionKind::Install);
        else if (c.id == kIdGame)
            set_component(c, "test", "", CompState::Ready, "Compatible", ActionKind::None);
    }
    // A newer launcher is out, so Settings shows UPDATE LAUNCHER.
    launcher_update_ = AppSnapshot::UpdateState::Available;
    launcher_latest_ = "0.1.99";
    Log::info("test version: " + std::to_string(mods_.size()) + " made-up mods loaded");
}

void App::simulate(const std::string& what, std::function<void()> start, std::function<void()> finish) {
    if (!claim_operation(what)) return;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        start();
    }
    Log::info("test version: " + what);

    workers_.emplace_back([this, what, finish]() {
        // Declared first so it is destroyed last: the gate outlives the state lock below.
        Operation gate(*this);
        wait_unless_stopping(kTestOperationMs);
        std::lock_guard<std::mutex> lock(state_mutex_);
        finish();
        Log::info("test version: done " + what);
    });
}

void App::wait_unless_stopping(int ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!stopping_.load() && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

}  // namespace oreo
