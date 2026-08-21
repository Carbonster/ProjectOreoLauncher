#include "python_runtime.h"

#include "log.h"
#include "util.h"

namespace oreo {

namespace {

// Applied to every child process we start with our Python. Anything that could make the system
// installation leak into our runtime is removed rather than merely overridden.
//
// `pip_cache_dir` is ours. Left alone, pip caches downloads in %LOCALAPPDATA%\pip\Cache, which
// belongs to the machine and to whatever else the player uses pip for: our wheels would sit in
// it for good, and uninstalling ProjectOreo would not take them out. An empty value means pip
// keeps its own default - useful only if we ever want that back.
EnvMap isolated_env(const std::string& pip_cache_dir) {
    EnvMap env;
    env["PYTHONHOME"] = kEnvUnset;
    env["PYTHONPATH"] = kEnvUnset;
    env["PYTHONSTARTUP"] = kEnvUnset;
    env["PYTHONUSERBASE"] = kEnvUnset;
    env["PYTHONNOUSERSITE"] = "1";
    env["PYTHONUTF8"] = "1";        // pip output stays UTF-8 whatever the console code page is
    env["PYTHONIOENCODING"] = "utf-8";
    env["PIP_REQUIRE_VIRTUALENV"] = kEnvUnset;  // a user-wide pip.conf must not block us
    env["PIP_DISABLE_PIP_VERSION_CHECK"] = "1";
    if (!pip_cache_dir.empty()) env["PIP_CACHE_DIR"] = pip_cache_dir;
    return env;
}

}  // namespace

PythonRuntimeService::PythonRuntimeService(const Config& cfg) {
    dir_ = cfg.python_dir;
    exe_ = path_join(dir_, "python.exe");
    // Inside the launcher folder, so removing ProjectOreo removes this with it and there is no
    // extra uninstall step to remember.
    pip_cache_dir_ = path_join(cfg.oreo_dir, "pipcache");
}

void PythonRuntimeService::detect() {
    available_ = false;
    version_.clear();
    error_.clear();

    if (!file_exists(exe_)) {
        error_ = "ProjectOreo's private Python is missing (expected " + exe_ +
                 "). Reinstall ProjectOreo to restore it.";
        Log::error("private python not found at " + exe_);
        return;
    }

    ProcResult r = run({"-c", "import sys;print('%d.%d.%d' % sys.version_info[:3])"}, 15000);
    if (!r.ok()) {
        error_ = "ProjectOreo's private Python could not be started. " +
                 (r.error.empty() ? std::string("It may be damaged - reinstall ProjectOreo.") : r.error);
        Log::error("private python failed to run: exit=" + std::to_string(r.exit_code) + " err=" + trim(r.err));
        return;
    }
    version_ = trim(r.out);
    available_ = true;
    Log::info("private python: " + exe_ + " (version " + version_ + ")");
}

std::string PythonRuntimeService::site_packages() const {
    return path_join(path_join(dir_, "Lib"), "site-packages");
}

ProcResult PythonRuntimeService::run(const std::vector<std::string>& args, int timeout_ms) const {
    std::vector<std::string> full;
    full.push_back("-I");  // isolated: ignore PYTHON* env vars and the user site directory
    full.insert(full.end(), args.begin(), args.end());
    return run_capture(exe_, full, timeout_ms, isolated_env(pip_cache_dir_), dir_);
}

ProcResult PythonRuntimeService::run_pip(const std::vector<std::string>& args, int timeout_ms) const {
    std::vector<std::string> full;
    full.push_back("-I");
    full.push_back("-m");
    full.push_back("pip");
    full.insert(full.end(), args.begin(), args.end());
    full.push_back("--disable-pip-version-check");
    full.push_back("--no-input");
    // pip creates the folder itself, but only if its parent exists - and on a first run nothing
    // has written inside the launcher folder yet.
    if (!pip_cache_dir_.empty()) make_dirs(pip_cache_dir_);
    return run_capture(exe_, full, timeout_ms, isolated_env(pip_cache_dir_), dir_);
}

}  // namespace oreo
