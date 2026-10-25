#include "ui/view_state.h"

#include "app/mod_readiness.h"
#include "model/component_ids.h"
#include "system/json.h"
#include "ui/component_texts.h"
#include "ui/thumbnails.h"
#include "updates/approved_catalog.h"

namespace oreo {

namespace {

// Writes one JSON object, field by field, in the order they are added.
class JsonObject {
public:
    JsonObject& raw(const char* key, const std::string& value) {
        out_ += (out_.empty() ? "{" : ",") + std::string("\"") + key + "\":" + value;
        return *this;
    }
    JsonObject& str(const char* key, const std::string& value) { return raw(key, json_quote(value)); }
    JsonObject& flag(const char* key, bool value) { return raw(key, json_bool(value)); }
    std::string done() const { return (out_.empty() ? "{" : out_) + "}"; }

private:
    std::string out_;
};

template <typename T, typename F>
std::string json_array(const std::vector<T>& items, F to_json) {
    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) out += (i ? "," : "") + to_json(items[i]);
    return out + "]";
}

// The components listed under a mod's REQUIREMENTS. pyMHF only with DebugHotkeys: pip picks
// it, so a player is never asked about it - but which one pip picked is the first thing to know
// when a mod fails to load.
std::vector<std::string> requirement_ids(const ModInfo& mod, bool debug) {
    std::vector<std::string> ids;
    if (!mod.is_python) return ids;
    ids.push_back(kIdNmspy);
    if (debug) ids.push_back(kIdPymhf);
    if (mod.needs_runtime()) ids.push_back(kIdRuntime);
    return ids;
}

// The line under a requirement: only what its versions and button do not already say. Not a
// build mismatch: the player cannot act on it.
std::string requirement_note(const Component& c) {
    if (c.busy) return "";
    if (!c.error.empty() && c.needs_attention()) return c.error;
    switch (c.state) {
        case CompState::Ready:
        case CompState::UpdateAvailable:
        case CompState::NotInstalled:
        case CompState::InstalledNewerThanTested:
        case CompState::InstalledOlderThanTested: return "";
        default: return c.message;
    }
}

// Nothing to do and nothing wrong: the row says UP TO DATE where its button would be.
bool requirement_up_to_date(const Component& c) {
    if (c.busy || c.action != ActionKind::None) return false;
    return c.state == CompState::Ready || c.state == CompState::InstalledNewerThanTested ||
           c.state == CompState::InstalledOlderThanTested;
}

std::string component_json(const Component& c) {
    const ApprovedCatalogEntry* entry = find_approved(c.id);
    const std::string repo = entry ? entry->repository_url : "";
    return JsonObject()
        .str("id", c.id)
        .str("name", c.name)
        .str("subtitle", c.subtitle)
        .str("about", why_needed(c.id))
        .str("repo", repo)
        .str("owner", repo_owner(repo))
        .str("installed", c.installed)
        .str("latest", c.latest)
        .flag("upToDate", requirement_up_to_date(c))
        .str("note", requirement_note(c))
        .flag("details", c.details.valid)
        .flag("busy", c.busy)
        .str("action", to_string(c.action))
        .done();
}

std::string mod_json(const AppSnapshot& snap, size_t index, const ViewOptions& options) {
    const ModInfo& m = snap.mods[index];
    const bool preparing = !snap.mod_update_folder.empty() && m.folder == snap.mod_update_folder;
    return JsonObject()
        .raw("index", std::to_string(index))
        .str("name", m.name)
        .str("version", m.version)
        .flag("enabled", m.enabled)
        .str("kind", m.is_python ? "Python mod" : "Mod")
        .str("description", m.description)
        .str("entryPoint", m.entry_point)
        .str("manifest", m.has_manifest ? m.manifest_path : "")
        .raw("reqs", json_array(requirement_ids(m, options.debug_hotkeys), json_quote))
        .str("prep", to_string(card_button(snap, index)))
        .flag("preparing", preparing)
        .str("thumb", options.thumbnails ? options.thumbnails->key(m) : "")
        .done();
}

std::string settings_values_json(const SettingsValues& values) {
    return JsonObject()
        .str("gamePath", values.game_path)
        .str("modsPath", values.mods_path)
        .flag("alwaysShow", values.always_show_window)
        .flag("checkUpdates", values.check_for_updates)
        .flag("highContrast", values.high_contrast)
        .flag("debugHotkeys", values.debug_hotkeys)
        .flag("verbose", values.verbose)
        .done();
}

}  // namespace

std::string state_json(const AppSnapshot& snap, const ViewOptions& options) {
    std::string mods = "[";
    for (size_t i = 0; i < options.card_order.size(); ++i)
        mods += (i ? "," : "") + mod_json(snap, options.card_order[i], options);
    mods += "]";

    return JsonObject()
        .flag("playRunning", snap.play_running || snap.play_done)
        .str("busyWith", snap.busy_with)
        .flag("refreshing", snap.busy_with == operation::kCheckingVersions)
        .flag("updatingLauncher", snap.busy_with == operation::kUpdatingLauncher)
        .flag("highContrast", snap.high_contrast)
        .flag("lightTheme", snap.light_theme)
        .flag("maximized", options.maximized)
        .str("version", PROJECTOREO_VERSION)
        .flag("testMode", snap.test_mode)
        .flag("debugHotkeys", options.debug_hotkeys)
        .str("gameVersion", snap.game_version)
        .str("pythonVersion", snap.python_version)
        .flag("launcherUpdate", snap.launcher_update == AppSnapshot::UpdateState::Available)
        .str("launcherLatest", snap.launcher_latest)
        .raw("components", json_array(snap.components, component_json))
        .raw("mods", mods)
        .done();
}

std::string settings_json(const SettingsValues& values, const std::string& auto_game_path,
                          const std::string& auto_mods_path) {
    return JsonObject()
        .raw("values", settings_values_json(values))
        .raw("defaults", settings_values_json(SettingsService::defaults()))
        .str("autoGamePath", auto_game_path)
        .str("autoModsPath", auto_mods_path)
        .done();
}

}  // namespace oreo
