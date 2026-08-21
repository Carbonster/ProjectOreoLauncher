#include "approved_catalog.h"

#include "util.h"

namespace oreo {

namespace {

// Every manifest lives on the default branch of its own repository, so a release is published
// by pushing one small file - no launcher rebuild, and no way for the file to name a different
// host than the one written down here.
const char* kLauncherManifest =
    "https://raw.githubusercontent.com/Carbonster/ProjectOreoLauncher/main/latest.json";
const char* kRuntimeManifest =
    "https://raw.githubusercontent.com/Carbonster/ProjectOreoRuntime/main/latest.json";
const char* kFishingManifest =
    "https://raw.githubusercontent.com/Carbonster/FishingMinigameMod/main/latest.json";

}  // namespace

const std::vector<ApprovedCatalogEntry>& approved_catalog() {
    static const std::vector<ApprovedCatalogEntry> entries = {
        {"launcher", "Project Oreo Launcher", "Mod manager and launcher",
         ApprovedSource::LauncherBundle, "", "", "", "",
         "https://github.com/Carbonster/ProjectOreoLauncher", kLauncherManifest, "", "", 0, true},
        {"pymhf", "pyMHF", "Python mod framework", ApprovedSource::PyPI,
         "pymhf", "pymhf", "", "", "https://github.com/monkeyman192/pyMHF", "", "", "", 0, true},
        {"nmspy", "NMSpy", "NMS Python bridge", ApprovedSource::PyPI,
         "nmspy", "nmspy", "", "", "https://github.com/monkeyman192/NMS.py", "", "", "", 0, true},
        {"runtime", "Project Oreo Runtime", "Native services for NMSpy mods",
         ApprovedSource::GitHubWheel, "project-oreo-runtime", "oreo_runtime", "", "",
         "https://github.com/Carbonster/ProjectOreoRuntime", kRuntimeManifest, "", "", 0, true},
        {"fishing_minigame", "Fishing Minigame Mod", "Fishing gameplay overhaul",
         ApprovedSource::GitHubModArchive, "", "", "fishing_mod", "",
         "https://github.com/Carbonster/FishingMinigameMod", kFishingManifest, "", "", 0, true},
    };
    return entries;
}

const ApprovedCatalogEntry* find_approved(const std::string& id) {
    const std::string wanted = to_lower(id);
    for (const ApprovedCatalogEntry& entry : approved_catalog()) {
        if (to_lower(entry.id) == wanted) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace oreo
