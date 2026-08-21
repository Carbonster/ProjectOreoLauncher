#include "package_service.h"

#include "log.h"
#include "util.h"

namespace oreo {

namespace {

const int kProbeTimeoutMs = 30 * 1000;
const int kPipTimeoutMs = 10 * 60 * 1000;   // pip may compile/download a lot on a slow line

// Prints "<package>=<version>" per line, empty version when the package is not installed.
// importlib.metadata reads the installed distribution metadata - no import of the package.
std::string probe_script(const std::vector<std::string>& packages) {
    std::string list;   // "'pymhf', 'nmspy'," - the trailing comma keeps it a tuple of one too
    for (const std::string& p : packages) list += "'" + p + "', ";
    return "import importlib.metadata as md\n"
           "for name in (" + list + "):\n"
           "    try:\n"
           "        print(name + '=' + md.version(name))\n"
           "    except Exception:\n"
           "        print(name + '=')\n";
}

OperationDetails make_details(const ProcResult& r, const std::string& python_path,
                              const std::string& package_version) {
    OperationDetails d;
    d.valid = true;
    d.command = r.command_line;
    d.exit_code = r.started ? std::to_string(r.exit_code) : std::string("(not started)");
    d.out = r.out;
    d.err = r.err;
    d.python_path = python_path;
    d.package_version = package_version;
    d.timestamp = timestamp_now();
    return d;
}

// Turns pip's wall of text into one sentence a player can act on.
std::string humanize_pip_failure(const ProcResult& r, const std::string& package, bool upgrade) {
    std::string blob = to_lower(r.err + "\n" + r.out);
    std::string what = (upgrade ? "update " : "install ") + package;
    if (r.timed_out) return "Could not " + what + ": the download took too long and was stopped.";
    if (!r.started) return "Could not " + what + ": " + r.error;
    if (blob.find("no matching distribution") != std::string::npos ||
        blob.find("could not find a version") != std::string::npos)
        return "Could not " + what + ": no release matching this Python version was found.";
    if (blob.find("temporary failure in name resolution") != std::string::npos ||
        blob.find("failed to establish a new connection") != std::string::npos ||
        blob.find("network is unreachable") != std::string::npos ||
        blob.find("connection error") != std::string::npos ||
        blob.find("retries exceeded") != std::string::npos)
        return "Could not " + what + ": PyPI could not be reached. Check your internet connection.";
    if (blob.find("ssl") != std::string::npos && blob.find("certificate") != std::string::npos)
        return "Could not " + what + ": the secure connection to PyPI failed.";
    if (blob.find("permission denied") != std::string::npos || blob.find("access is denied") != std::string::npos)
        return "Could not " + what + ": a file was locked. Close No Man's Sky and try again.";
    if (blob.find("no module named pip") != std::string::npos)
        return "Could not " + what + ": ProjectOreo's private Python has no pip. Reinstall ProjectOreo.";
    return "Could not " + what + ". See Details for the full output.";
}

}  // namespace

PackageProbe PackageService::probe(const std::vector<std::string>& packages) const {
    PackageProbe probe;
    for (const std::string& p : packages) probe.versions[p] = "";

    if (!python_.available()) {
        probe.error = python_.error();
        return probe;
    }

    ProcResult r = python_.run({"-c", probe_script(packages)}, kProbeTimeoutMs);
    probe.details = make_details(r, python_.exe(), "");
    if (!r.ok()) {
        probe.error = r.timed_out ? "Reading the installed package versions took too long."
                                  : "Could not read the installed package versions from ProjectOreo's Python.";
        Log::error("package probe failed: exit=" + std::to_string(r.exit_code) + " err=" + trim(r.err));
        return probe;
    }

    for (const std::string& line : split(r.out, '\n')) {
        std::string entry = trim(line);
        if (entry.empty()) continue;
        size_t eq = entry.find('=');
        if (eq == std::string::npos) continue;
        probe.versions[entry.substr(0, eq)] = trim(entry.substr(eq + 1));
    }
    probe.ok = true;
    return probe;
}

PackageOpResult PackageService::install(const std::string& package, const std::string& version) const {
    return run_pip_op(package, version, false);
}

PackageOpResult PackageService::update(const std::string& package, const std::string& version) const {
    return run_pip_op(package, version, true);
}

PackageOpResult PackageService::run_pip_op(const std::string& package, const std::string& version,
                                           bool upgrade) const {
    PackageOpResult out;

    if (!python_.available()) {
        out.message = python_.error();
        return out;
    }

    // Always know what was there before, so a failed operation can report the real final state.
    PackageProbe before = probe({package});
    out.version_before = before.versions[package];

    std::vector<std::string> args{"install"};
    if (upgrade && version.empty()) args.push_back("--upgrade");
    // An exact pin is also how we go BACKWARDS (installed newer than the tested version).
    args.push_back(version.empty() ? package : package + "==" + version);

    Log::info(std::string(upgrade ? "updating" : "installing") + " " + package +
              (version.empty() ? " (latest)" : " (pinned to " + version + ")") +
              (out.version_before.empty() ? "" : ", installed: " + out.version_before));
    ProcResult r = python_.run_pip(args, kPipTimeoutMs);

    // pip's exit code is not the source of truth: read the installed version back.
    PackageProbe after = probe({package});
    out.version_after = after.versions[package];
    out.details = make_details(r, python_.exe(),
                               "before: " + (out.version_before.empty() ? "(none)" : out.version_before) +
                                   ", after: " + (out.version_after.empty() ? "(none)" : out.version_after));

    if (!out.version_after.empty() && (r.ok() || out.version_after != out.version_before)) {
        out.success = true;
        if (upgrade && out.version_after == out.version_before) {
            out.message = package + " is already at " + out.version_after + ".";
        } else if (out.version_before.empty()) {
            // Nothing was there before, so there is nothing to compare against - saying
            // "(was not installed)" only restates the row the player was just looking at.
            out.message = package + " " + out.version_after + " installed.";
        } else {
            out.message = package + " " + out.version_after +
                          (upgrade ? " installed (updated from " : " installed (was ") +
                          out.version_before + ").";
        }
        Log::info("pip ok: " + out.message);
        return out;
    }

    out.success = false;
    out.message = humanize_pip_failure(r, package, upgrade);
    if (!out.version_after.empty()) {
        // pip failed but the previous installation survived - say so plainly.
        out.message += " " + package + " " + out.version_after + " is still installed and unchanged.";
    }
    Log::error("pip failed: " + out.message + " (exit " + std::to_string(r.exit_code) + ")");
    return out;
}

}  // namespace oreo
