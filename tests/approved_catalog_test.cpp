#include "core/approved_catalog.h"

#include <cassert>
#include <set>
#include <string>

namespace {
bool starts_with(const std::string& value, const std::string& prefix) {
    return value.rfind(prefix, 0) == 0;
}
}  // namespace

int main() {
    const auto& catalog = oreo::approved_catalog();
    assert(catalog.size() == 5);
    std::set<std::string> ids;
    for (const auto& entry : catalog) {
        assert(!entry.id.empty());
        assert(ids.insert(entry.id).second);
        assert(starts_with(entry.repository_url, "https://github.com/"));
        // The asset url and its hash are no longer written down here: they come from the
        // component's own manifest at run time, so a release does not need a new launcher.
        // What stays fixed in the binary is WHERE the launcher may ask, and that is checked in
        // update_source_test.
        assert(entry.asset_url.empty());
        assert(entry.sha256.empty());
        assert(entry.asset_size == 0);
    }
    const auto* pymhf = oreo::find_approved("pymhf");
    const auto* nmspy = oreo::find_approved("NMSPY");
    const auto* runtime = oreo::find_approved("runtime");
    const auto* fishing = oreo::find_approved("fishing_minigame");
    assert(pymhf && pymhf->package_name == "pymhf");
    assert(nmspy && nmspy->package_name == "nmspy");
    assert(runtime && runtime->release_ready && !runtime->manifest_url.empty());
    assert(fishing && !fishing->manifest_url.empty());
    assert(oreo::find_approved("random_user_mod") == nullptr);
    return 0;
}
