// update_service.h - UpdateService: "what is the newest release out there?".
//
// Kept strictly separate from installed-version detection: a failed lookup only ever means
// "latest unknown". It never blocks PLAY, never turns into an error state for the component,
// and never makes startup wait - results are cached and requests use short timeouts.
#pragma once

#include <map>
#include <mutex>
#include <string>

#include "config.h"

namespace oreo {

struct LatestInfo {
    bool ok = false;
    std::string version;
    std::string error;        // human readable, shown only as "Unable to check updates"
    bool from_cache = false;
};

class UpdateService {
public:
    explicit UpdateService(const Config& cfg);

    // Newest release of a PyPI package. Answers from cache while it is fresh.
    LatestInfo latest_pypi(const std::string& package);

    bool enabled() const { return cfg_.check_for_updates; }

private:
    void load_cache();
    void save_cache();

    const Config& cfg_;
    std::string cache_path_;
    std::mutex mutex_;
    struct Entry {
        std::string version;
        long long time = 0;
    };
    std::map<std::string, Entry> cache_;
};

}  // namespace oreo
