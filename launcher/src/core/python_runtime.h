// python_runtime.h - PythonRuntimeService.
//
// ProjectOreo ships and uses its OWN Python. The user's system Python is irrelevant, whatever
// they have installed (3.10, 3.14, Microsoft Store, several at once, none at all):
//   * we never look at PATH, `py`, `python`, the registry or system site-packages;
//   * every call names the exact executable <ProjectOreo>\python\python.exe;
//   * every call passes -I (isolated), so PYTHON* environment variables and the user site
//     directory cannot leak in either;
//   * pip downloads into a cache inside our own folder, not the machine-wide one.
#pragma once

#include <string>
#include <vector>

#include "config.h"
#include "process.h"

namespace oreo {

class PythonRuntimeService {
public:
    explicit PythonRuntimeService(const Config& cfg);

    // Locates python.exe and reads its version. Cheap, local, no network.
    void detect();

    bool available() const { return available_; }
    const std::string& exe() const { return exe_; }         // full path to our python.exe
    const std::string& dir() const { return dir_; }
    const std::string& version() const { return version_; } // "3.13.3", empty if unknown
    const std::string& error() const { return error_; }     // human explanation when missing
    std::string site_packages() const;                      // for debug diagnostics
    const std::string& pip_cache_dir() const { return pip_cache_dir_; }

    // python.exe -I <args>
    ProcResult run(const std::vector<std::string>& args, int timeout_ms) const;
    // python.exe -I -m pip <args>  (never the system pip, never a bare `pip`)
    ProcResult run_pip(const std::vector<std::string>& args, int timeout_ms) const;

private:
    std::string dir_;
    std::string exe_;
    std::string pip_cache_dir_;
    std::string version_;
    std::string error_;
    bool available_ = false;
};

}  // namespace oreo
