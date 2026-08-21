#include "update_service.h"

#include "http.h"
#include "log.h"
#include "util.h"

namespace oreo {

UpdateService::UpdateService(const Config& cfg) : cfg_(cfg) {
    cache_path_ = path_join(cfg.oreo_dir, "cache.ini");
    load_cache();
}

void UpdateService::load_cache() {
    IniMap ini;
    if (!ini_load(cache_path_, ini)) return;
    for (const auto& kv : ini) {
        if (!starts_with(kv.first, "pypi.")) continue;
        std::string key = kv.first.substr(5);
        if (ends_with(key, "_time")) continue;
        Entry entry;
        entry.version = kv.second;
        entry.time = (long long)ini_get_int(ini, "pypi." + key + "_time", 0);
        cache_[key] = entry;
    }
}

void UpdateService::save_cache() {
    std::string text =
        "; Cached results of the last successful version lookups.\r\n"
        "; Safe to delete - ProjectOreo will simply ask again.\r\n"
        "\r\n[pypi]\r\n";
    for (const auto& kv : cache_) {
        text += kv.first + "=" + kv.second.version + "\r\n";
        text += kv.first + "_time=" + std::to_string(kv.second.time) + "\r\n";
    }
    write_file(cache_path_, text);
}

LatestInfo UpdateService::latest_pypi(const std::string& package) {
    LatestInfo out;
    std::string key = to_lower(package);

    if (!cfg_.check_for_updates) {
        out.error = "Update checks are turned off in config.ini (CheckForUpdates=0).";
        return out;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end() && !it->second.version.empty()) {
            long long age_minutes = (unix_time_now() - it->second.time) / 60;
            if (age_minutes >= 0 && age_minutes < cfg_.cache_minutes) {
                out.ok = true;
                out.from_cache = true;
                out.version = it->second.version;
                Log::debug("latest " + package + " = " + out.version + " (cached, " +
                           std::to_string(age_minutes) + " min old)");
                return out;
            }
        }
    }

    HttpResult response = http_get("https://pypi.org/pypi/" + package + "/json", cfg_.timeout_seconds);
    if (!response.ok) {
        out.error = response.error;
        Log::warn("latest " + package + ": lookup failed - " + response.error);
        // Fall back to a stale cache entry rather than showing nothing at all.
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end() && !it->second.version.empty()) {
            out.ok = true;
            out.from_cache = true;
            out.version = it->second.version;
        }
        return out;
    }

    // The current release sits in the "info" object; look for "version" after it so a field of
    // the same name elsewhere in the document cannot be picked up by mistake.
    size_t info_pos = json_find_key(response.body, "info");
    std::string version = json_string_field(response.body, "version", info_pos);
    if (version.empty()) {
        out.error = "The answer from PyPI could not be understood.";
        Log::warn("latest " + package + ": could not parse the PyPI response");
        return out;
    }

    out.ok = true;
    out.version = version;
    Log::info("latest " + package + " = " + version);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cache_[key] = Entry{version, unix_time_now()};
        save_cache();
    }
    return out;
}

}  // namespace oreo
