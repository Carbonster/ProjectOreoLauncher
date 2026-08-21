// approved_catalog.h - Compile-time allowlist of software ProjectOreo may manage.
//
// Local mod metadata is never a source of download URLs or executable package names. Adding a
// new managed component requires a reviewed launcher build.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace oreo {

enum class ApprovedSource {
    LauncherBundle = 0,
    PyPI,
    GitHubWheel,
    GitHubModArchive,
};

struct ApprovedCatalogEntry {
    std::string id;
    std::string name;
    std::string subtitle;
    ApprovedSource source = ApprovedSource::LauncherBundle;
    std::string package_name;
    std::string import_name;
    std::string install_folder;
    std::string version;
    std::string repository_url;
    // Where the launcher asks "what is the newest version, and where is its file". Fixed here
    // so a component can be updated without shipping a new launcher, while the SERVER it may
    // ask stays something only a reviewed launcher build can change.
    std::string manifest_url;
    // Filled from the manifest at run time. The values below are the v2 hooks for a signed
    // manifest; nothing verifies them today.
    std::string asset_url;
    std::string sha256;
    std::uint64_t asset_size = 0;
    bool release_ready = false;
};

const std::vector<ApprovedCatalogEntry>& approved_catalog();
const ApprovedCatalogEntry* find_approved(const std::string& id);

}  // namespace oreo
