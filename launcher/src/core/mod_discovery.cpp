#include "mod_discovery.h"

#include <algorithm>

#include "log.h"
#include "util.h"

namespace oreo {

namespace {

const char* kManifestName = "projectoreo.ini";

// The card shows one line, the rest waits in Details. A mod that declares no Summary still gets
// a sensible line: the first sentence of its description, which is what a summary would have
// said anyway. Cut at a sentence end, never mid-word, and never longer than the card can show.
std::string first_sentence(const std::string& text) {
    const size_t kMaxChars = 140;
    size_t stop = text.find_first_of(".!?");
    if (stop != std::string::npos && stop + 1 <= kMaxChars) return trim(text.substr(0, stop + 1));
    if (text.size() <= kMaxChars) return trim(text);
    size_t cut = text.rfind(' ', kMaxChars);
    if (cut == std::string::npos) cut = kMaxChars;
    return trim(text.substr(0, cut)) + "...";
}

// Mods in this project are versioned by filename (mod_v35.py, mod_v36.py, ...). When a folder
// holds several candidates and says nothing about which to run, the highest number wins.
long long version_suffix(const std::string& filename) {
    std::string stem = to_lower(path_stem(filename));
    size_t pos = stem.rfind("_v");
    if (pos == std::string::npos) return -1;
    std::string digits = stem.substr(pos + 2);
    if (digits.empty()) return -1;
    for (char c : digits)
        if (c < '0' || c > '9') return -1;
    return std::stoll(digits);
}

bool looks_like_pymhf_mod(const std::string& path) {
    std::string text;
    if (!read_file(path, text)) return false;
    // pyMHF single-file mods carry their configuration in a header comment block.
    std::string head = text.substr(0, 4096);
    return head.find("[tool.pymhf]") != std::string::npos;
}

std::string state_key(const std::string& folder) { return "mods." + to_lower(folder); }

std::string game_settings_path(const std::string& mods_dir) {
    return path_join(path_join(path_dir(path_dir(mods_dir)), "Binaries\\SETTINGS"),
                     "GCMODSETTINGS.MXML");
}

std::string xml_property_value(const std::string& block, const std::string& property) {
    std::string lower = to_lower(block);
    size_t pos = lower.find("name=\"" + to_lower(property) + "\"");
    if (pos == std::string::npos) return "";
    pos = lower.find("value=\"", pos);
    if (pos == std::string::npos) return "";
    pos += 7;
    size_t end = block.find('"', pos);
    return end == std::string::npos ? "" : block.substr(pos, end - pos);
}

struct GameModStates {
    bool disable_all = false;
    std::map<std::string, bool> enabled;
};

GameModStates read_game_states(const std::string& path) {
    GameModStates result;
    std::string xml;
    if (!read_file(path, xml)) return result;
    result.disable_all = to_lower(xml_property_value(xml, "DisableAllMods")) == "true";
    std::string lower = to_lower(xml);
    const std::string marker = "value=\"gcmodsettingsinfo\"";
    size_t start = 0;
    while ((start = lower.find(marker, start)) != std::string::npos) {
        size_t next = lower.find(marker, start + marker.size());
        std::string block = xml.substr(start, next == std::string::npos ? std::string::npos : next - start);
        std::string name = xml_property_value(block, "Name");
        std::string enabled = to_lower(xml_property_value(block, "Enabled"));
        if (!name.empty()) result.enabled[to_lower(name)] = enabled != "false";
        if (next == std::string::npos) break;
        start = next;
    }
    return result;
}

std::string xml_escape(const std::string& value) {
    std::string out;
    for (char c : value) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '\"': out += "&quot;"; break;
            default: out.push_back(c); break;
        }
    }
    return out;
}

bool set_xml_bool(std::string& xml, size_t begin, size_t end,
                  const std::string& property, bool value) {
    std::string block = xml.substr(begin, end - begin);
    std::string lower = to_lower(block);
    size_t prop = lower.find("name=\"" + to_lower(property) + "\"");
    if (prop == std::string::npos) return false;
    size_t attr = lower.find("value=\"", prop);
    if (attr == std::string::npos) return false;
    size_t value_start = begin + attr + 7;
    size_t value_end = xml.find('"', value_start);
    if (value_end == std::string::npos) return false;
    xml.replace(value_start, value_end - value_start, value ? "true" : "false");
    return true;
}

bool write_game_state(const std::string& path, const std::string& folder, bool enabled) {
    std::string xml;
    if (!read_file(path, xml)) {
        xml = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
              "<Data template=\"GcModSettings\">\r\n"
              "\t<Property name=\"DisableAllMods\" value=\"false\" />\r\n"
              "\t<Property name=\"Data\">\r\n\t</Property>\r\n</Data>\r\n";
    }

    // Enabling one mod also clears the global "disable all" switch.
    if (enabled) set_xml_bool(xml, 0, xml.size(), "DisableAllMods", false);

    std::string lower = to_lower(xml);
    const std::string marker = "value=\"gcmodsettingsinfo\"";
    size_t start = 0;
    while ((start = lower.find(marker, start)) != std::string::npos) {
        size_t next = lower.find(marker, start + marker.size());
        size_t end = next == std::string::npos ? xml.size() : next;
        std::string block = xml.substr(start, end - start);
        if (to_lower(xml_property_value(block, "Name")) == to_lower(folder)) {
            if (!set_xml_bool(xml, start, end, "Enabled", enabled)) return false;
            set_xml_bool(xml, start, xml.size(), "EnabledVR", enabled);
            return write_file(path, xml);
        }
        if (next == std::string::npos) break;
        start = next;
    }

    size_t data_close = xml.rfind("</Property>");
    if (data_close == std::string::npos) return false;
    std::string name = xml_escape(folder);
    std::string flag = enabled ? "true" : "false";
    std::string entry =
        "\t\t<Property name=\"Data\" value=\"GcModSettingsInfo\">\r\n"
        "\t\t\t<Property name=\"Name\" value=\"" + name + "\" />\r\n"
        "\t\t\t<Property name=\"Author\" value=\"\" />\r\n"
        "\t\t\t<Property name=\"ID\" value=\"0\" />\r\n"
        "\t\t\t<Property name=\"AuthorID\" value=\"0\" />\r\n"
        "\t\t\t<Property name=\"LastUpdated\" value=\"0\" />\r\n"
        "\t\t\t<Property name=\"ModPriority\" value=\"0\" />\r\n"
        "\t\t\t<Property name=\"Enabled\" value=\"" + flag + "\" />\r\n"
        "\t\t\t<Property name=\"EnabledVR\" value=\"" + flag + "\" />\r\n"
        "\t\t\t<Property name=\"Dependencies\" />\r\n"
        "\t\t</Property>\r\n";
    xml.insert(data_close, entry);
    return write_file(path, xml);
}

bool write_mod_state(const std::string& state_path, const IniMap& states) {
    std::vector<std::pair<std::string, std::string>> rows;
    for (const auto& item : states) {
        if (!starts_with(item.first, "mods.")) continue;
        rows.push_back({item.first.substr(5), item.second});
    }
    std::sort(rows.begin(), rows.end());
    std::string text = "; ProjectOreo per-mod switches. Managed by the launcher.\r\n[Mods]\r\n";
    for (const auto& row : rows) text += row.first + "=" + row.second + "\r\n";
    return write_file(state_path, text);
}

}  // namespace

const char* ModDiscoveryService::manifest_name() { return kManifestName; }

void ModDiscoveryService::scan(const std::string& mods_dir, const std::string& state_path) {
    mods_.clear();
    error_.clear();
    scanned_ok_ = false;

    if (mods_dir.empty()) {
        error_ = "The game's MODS folder is unknown because No Man's Sky was not found.";
        return;
    }
    if (!dir_exists(mods_dir))
        Log::info("MODS folder does not exist yet: " + mods_dir);

    IniMap states;
    ini_load(state_path, states);
    scan_root(mods_dir, states);

    std::sort(mods_.begin(), mods_.end(),
              [](const ModInfo& a, const ModInfo& b) { return to_lower(a.name) < to_lower(b.name); });
    scanned_ok_ = true;
}

void ModDiscoveryService::scan_root(const std::string& root, const IniMap& states) {
    if (!dir_exists(root)) return;
    GameModStates game_states = read_game_states(game_settings_path(root));
    for (const std::string& folder : list_dirs(root)) {
        if (starts_with(folder, ".")) continue;
        ModInfo mod;
        mod.folder = folder;
        mod.path = path_join(root, folder);
        mod.name = folder;

        read_manifest(mod.path, mod);
        bool has_entry = !mod.entry_point.empty() && file_exists(path_join(mod.path, mod.entry_point));
        if (!has_entry) has_entry = detect_pymhf_entry(mod.path, mod);
        mod.is_python = has_entry;
        mod.is_unpacked = !mod.is_python;
        bool python_enabled = ini_get_bool(states, state_key(mod.folder), mod.enabled);
        auto game_state = game_states.enabled.find(to_lower(mod.folder));
        bool game_enabled = !game_states.disable_all &&
                            (game_state == game_states.enabled.end() || game_state->second);
        mod.enabled = (!mod.is_python || python_enabled) && game_enabled;

        if (!mod.thumbnail.empty() && !file_exists(path_join(mod.path, mod.thumbnail))) mod.thumbnail.clear();
        if (mod.thumbnail.empty()) {
            for (const char* candidate : {"thumbnail.png", "thumbnail.jpg", "icon.png"}) {
                if (file_exists(path_join(mod.path, candidate))) {
                    mod.thumbnail = candidate;
                    break;
                }
            }
        }

        Log::info("mod detected: " + mod.name + " (" + mod.folder + ")" +
                  (mod.is_unpacked ? " [unpacked]" : "") +
                  (mod.has_manifest ? "" : " [no manifest]") +
                  (mod.enabled ? " [enabled]" : " [disabled]") +
                  (mod.entry_point.empty() ? " [no entry point]" : " entry=" + mod.entry_point));
        mods_.push_back(mod);
    }
}

bool ModDiscoveryService::read_manifest(const std::string& folder_path, ModInfo& mod) const {
    std::string manifest_path = path_join(folder_path, kManifestName);
    IniMap ini;
    if (!ini_load(manifest_path, ini)) return false;

    mod.has_manifest = true;
    mod.manifest_path = manifest_path;
    mod.name = ini_get(ini, "mod.name", mod.folder);
    mod.description = ini_get(ini, "mod.description", "");
    mod.summary = ini_get(ini, "mod.summary", "");
    if (mod.summary.empty()) mod.summary = first_sentence(mod.description);
    mod.version = ini_get(ini, "mod.version", "");
    mod.entry_point = ini_get(ini, "mod.entrypoint", "");
    mod.thumbnail = ini_get(ini, "mod.thumbnail", "");
    mod.enabled = ini_get_bool(ini, "mod.enabled", true);

    mod.tested_runtime = ini_get(ini, "compatibility.runtime", "");
    mod.tested_nms = ini_get(ini, "compatibility.nms", "");
    mod.tested_nmspy = ini_get(ini, "compatibility.nmspy", "");
    mod.tested_pymhf = ini_get(ini, "compatibility.pymhf", "");
    mod.tested_loader = ini_get(ini, "compatibility.loader", "");
    return true;
}

bool ModDiscoveryService::set_enabled(const ModInfo& mod, const std::string& mods_dir,
                                      const std::string& state_path, bool enabled,
                                      std::string& error) const {
    error.clear();
    if (!write_game_state(game_settings_path(mods_dir), mod.folder, enabled)) {
        error = "Could not update the game's mod settings.";
        return false;
    }
    if (mod.is_python) {
        IniMap states;
        ini_load(state_path, states);
        states[state_key(mod.folder)] = enabled ? "1" : "0";
        if (!write_mod_state(state_path, states)) {
            error = "The game setting was updated, but ProjectOreo could not save the Python mod switch.";
            return false;
        }
    }
    return true;
}

bool ModDiscoveryService::detect_pymhf_entry(const std::string& folder_path, ModInfo& mod) const {
    std::vector<std::string> candidates;
    for (const std::string& file : list_files(folder_path)) {
        if (path_ext(file) != ".py") continue;
        if (looks_like_pymhf_mod(path_join(folder_path, file))) candidates.push_back(file);
    }
    if (candidates.empty()) return false;

    std::sort(candidates.begin(), candidates.end(), [](const std::string& a, const std::string& b) {
        long long va = version_suffix(a), vb = version_suffix(b);
        if (va != vb) return va > vb;
        return to_lower(a) < to_lower(b);
    });
    mod.entry_point = candidates.front();
    if (candidates.size() > 1)
        Log::info("mod " + mod.folder + ": " + std::to_string(candidates.size()) +
                  " runnable python files, using " + mod.entry_point +
                  " (set EntryPoint in projectoreo.ini to choose another)");
    return true;
}

}  // namespace oreo
