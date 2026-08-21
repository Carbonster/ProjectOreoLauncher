// component_update.h - checking for and installing the things the launcher manages itself.
//
// pyMHF and NMSpy are not here: they come from PyPI and pip does the fetching, which is what
// PackageService is for. This covers what ships as a file on GitHub - the Runtime wheel, a mod
// archive, and the launcher's own replacement.
//
// The shape is the same for all three and deliberately split in two:
//
//   check()   - ask the component's manifest what the newest version is. Read only, no files
//               touched, safe to run at startup and safe to fail (no network is not an error).
//   install() - download it and put it in place. Only ever runs because somebody pressed a
//               button.
//
// Nothing here decides WHERE to download from. That is fixed in approved_catalog.cpp and
// enforced in http_download.cpp; a manifest can name a file, never a server.
#pragma once

#include <functional>
#include <string>

#include "approved_catalog.h"
#include "config.h"
#include "game_service.h"
#include "python_runtime.h"
#include "release_manifest.h"
#include "state.h"

namespace oreo {

struct UpdateCheck {
    bool ok = false;              // the manifest was fetched and understood
    bool update_available = false;
    std::string installed;        // what is here now, empty when nothing is
    std::string latest;           // what the manifest offers
    std::string notes_url;
    std::string error;            // human sentence; "no network" lands here and is not fatal
    ReleaseManifest manifest;
};

struct InstallResult {
    bool ok = false;
    std::string message;          // one human line, success or failure
    // Set when the launcher replaced itself and the caller has to hand over: close the game
    // gate, start `restart_exe`, and exit.
    bool restart_required = false;
    std::string restart_exe;
    OperationDetails details;
};

class ComponentUpdateService {
public:
    ComponentUpdateService(const Config& cfg, const GameService& game,
                           const PythonRuntimeService& python)
        : cfg_(cfg), game_(game), python_(python) {}

    // `installed_version` is what the caller already knows is on disk - this class does not
    // detect versions, it only compares. Empty means "not installed", and then any manifest
    // version counts as an update.
    UpdateCheck check(const ApprovedCatalogEntry& entry, const std::string& installed_version) const;

    // Downloads and applies. `progress` is called with a short human sentence at each step so a
    // 30 MB download does not look like a hang.
    using ProgressFn = std::function<void(const std::string&)>;
    InstallResult install(const ApprovedCatalogEntry& entry, const ReleaseManifest& manifest,
                          ProgressFn progress = nullptr) const;

    // Where downloads are parked. Inside the launcher's own folder, never %TEMP%: a half
    // downloaded update should be findable and should not be swept up by a disk cleaner
    // mid-install.
    std::string download_dir() const;

private:
    InstallResult install_wheel(const ApprovedCatalogEntry& entry, const std::string& file,
                                ProgressFn progress) const;
    InstallResult install_mod_archive(const ApprovedCatalogEntry& entry, const std::string& file,
                                      ProgressFn progress) const;
    InstallResult install_launcher(const std::string& file, ProgressFn progress) const;

    const Config& cfg_;
    const GameService& game_;
    const PythonRuntimeService& python_;
};

// Unpacks a zip into `dest_dir` using Windows' own tar.exe, which has handled zip since
// Windows 10. Refuses entries with absolute paths or `..` in them before extracting, so an
// archive cannot write outside the folder it was aimed at.
bool extract_zip(const std::string& archive, const std::string& dest_dir, std::string& error);

// True when the archive contains no entry that would escape the folder it is unpacked into.
// Pure and testable: it reads the zip's own directory, it does not extract anything.
bool zip_entries_are_safe(const std::string& archive, std::string& offending_entry);

}  // namespace oreo
