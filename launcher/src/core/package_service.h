// package_service.h - PackageService: installed-version detection and pip operations, always
// against ProjectOreo's private Python.
//
// Detection uses package metadata (importlib.metadata), never file parsing and never an
// `import` of the package itself - importing pyMHF starts its interactive setup questions,
// which would hang a launcher that is supposed to be invisible.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "python_runtime.h"
#include "state.h"

namespace oreo {

struct PackageProbe {
    bool ok = false;
    std::map<std::string, std::string> versions;   // package -> version ("" = not installed)
    std::string error;                             // human explanation when ok == false
    OperationDetails details;
};

struct PackageOpResult {
    bool success = false;
    std::string version_before;
    std::string version_after;    // read back after the operation - never trusted from pip alone
    std::string message;          // human summary, success or failure
    OperationDetails details;
};

class PackageService {
public:
    explicit PackageService(const PythonRuntimeService& python) : python_(python) {}

    // One python call for all packages: fast at startup, and the natural way to re-check
    // everything after an operation that may have moved a dependency.
    PackageProbe probe(const std::vector<std::string>& packages) const;

    // `version` empty = take whatever pip considers newest. Non-empty = install exactly that
    // version ("pymhf==0.2.3"), which is what a mod's tested version means in practice.
    PackageOpResult install(const std::string& package, const std::string& version = "") const;
    PackageOpResult update(const std::string& package, const std::string& version = "") const;

private:
    PackageOpResult run_pip_op(const std::string& package, const std::string& version, bool upgrade) const;
    const PythonRuntimeService& python_;
};

}  // namespace oreo
