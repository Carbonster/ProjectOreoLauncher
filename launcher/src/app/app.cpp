#include "app/app.h"

#include "model/component_ids.h"
#include "system/files.h"
#include "system/log.h"

namespace oreo {

namespace {

Component make_component(const char* id, const char* name, const char* subtitle) {
    Component c;
    c.id = id;
    c.name = name;
    c.subtitle = subtitle;
    return c;
}

}  // namespace

App::App(StartMode mode, unsigned long loader_pid) : mode_(mode), loader_pid_(loader_pid) {}
App::~App() { wait_for_workers(); }

void App::init() {
    cfg_ = Config::load();
    // The test version runs next to a real launcher that holds launcher.log open.
    std::string log_dir = path_join(cfg_.oreo_dir, "logs");
    Log::init(test_mode() ? path_join(log_dir, "test") : log_dir, cfg_.verbose);
    Log::info("----------------------------------------------------------------");
    Log::info("ProjectOreo launcher starting");
    Log::info(std::string("start mode: ") +
              (mode_ == StartMode::FromLoader ? "from loader" : test_mode() ? "test version" : "user"));
    if (loader_pid_) Log::info("game pid reported by the loader: " + std::to_string(loader_pid_));
    Log::info("AlwaysShowWindow=" + std::string(cfg_.always_show_window ? "1" : "0") +
              " DebugHotkeys=" + std::string(cfg_.debug_hotkeys ? "true" : "false") +
              " CheckForUpdates=" + (cfg_.check_for_updates ? "1" : "0") +
              " Verbose=" + (cfg_.verbose ? "1" : "0"));
    Log::info("config: " + cfg_.config_path);

    python_ = std::make_unique<PythonRuntimeService>(cfg_);
    packages_ = std::make_unique<PackageService>(*python_);
    game_ = std::make_unique<GameService>(cfg_);
    loader_ = std::make_unique<LoaderService>(cfg_, *game_);
    discovery_ = std::make_unique<ModDiscoveryService>();
    discovery_->set_backup_dir(path_join(path_join(cfg_.oreo_dir, "backups"), "game_settings"));
    updates_ = std::make_unique<UpdateService>(cfg_);
    components_updater_ = std::make_unique<ComponentUpdateService>(cfg_, *game_, *python_);
    launch_ = std::make_unique<LaunchService>(cfg_, *game_, *python_);

    components_ = {
        make_component(kIdLoader, "ProjectOreo Loader", "Core runtime"),
        make_component(kIdPymhf, "pyMHF", "Python mod framework"),
        make_component(kIdNmspy, "NMSpy", "NMS Python bridge"),
        make_component(kIdRuntime, "Project Oreo Runtime", "Overlay and controller support"),
        make_component(kIdGame, "No Man's Sky", "Game"),
    };
}

AppSnapshot App::snapshot() {
    AppSnapshot snap;
    std::lock_guard<std::mutex> lock(state_mutex_);
    snap.components = components_;
    snap.mods = mods_;
    snap.mod_updates = mod_updates_;
    snap.checking = checking_.load();
    snap.play_running = play_running_.load();
    snap.play_done = play_done_.load();
    snap.mod_update_folder = mod_update_folder_;
    snap.busy_with = current_operation();
    snap.launcher_update = launcher_update_;
    snap.launcher_latest = launcher_latest_;
    snap.high_contrast = cfg_.high_contrast;
    snap.light_theme = cfg_.light_theme;
    snap.game_version = test_mode() ? "test" : game_->display_version();
    snap.python_version = test_mode() ? "test" : python_->version();
    snap.game_path = game_->root();
    snap.mods_path = game_->mods_dir();
    snap.test_mode = test_mode();
    return snap;
}

void App::wait_for_workers() {
    stopping_ = true;
    for (std::thread& t : workers_)
        if (t.joinable()) t.join();
}

bool App::begin_operation(const std::string& what, std::string* running_now) {
    std::lock_guard<std::mutex> lock(op_mutex_);
    if (!op_name_.empty()) {
        if (running_now) *running_now = op_name_;
        return false;
    }
    op_name_ = what;
    return true;
}

bool App::claim_operation(const std::string& what) {
    std::string running;
    if (begin_operation(what, &running)) return true;
    Log::info("refused " + what + ": busy " + running);
    return false;
}

void App::end_operation() {
    std::lock_guard<std::mutex> lock(op_mutex_);
    op_name_.clear();
}

std::string App::current_operation() const {
    std::lock_guard<std::mutex> lock(op_mutex_);
    return op_name_;
}

}  // namespace oreo
