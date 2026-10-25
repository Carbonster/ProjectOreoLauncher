#include "ui/card_order.h"

#include <algorithm>
#include <functional>

#include "app/mod_readiness.h"
#include "system/log.h"
#include "system/text.h"

namespace oreo {

namespace {

// Which mod a card is across rescans: its index is not kept when REFRESH scans again.
std::string mod_key(const ModInfo& mod) { return mod.path + "|" + mod.folder; }

int group(const AppSnapshot& snap, size_t i) {
    if (!snap.mods[i].enabled) return 3;
    switch (card_button(snap, i)) {
        case CardButton::Install: return 0;
        case CardButton::Update: return 1;
        default: return 2;
    }
}

std::vector<size_t> sorted_indices(const AppSnapshot& snap, const std::function<size_t(size_t)>& rank) {
    std::vector<size_t> order(snap.mods.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (rank(a) != rank(b)) return rank(a) < rank(b);
        return to_lower(snap.mods[a].name) < to_lower(snap.mods[b].name);
    });
    return order;
}

}  // namespace

std::vector<size_t> CardOrder::order(const AppSnapshot& snap) {
    if (resort_pending_ && snap.busy_with.empty() && !snap.checking) {
        resort_pending_ = false;
        keys_.clear();
        for (size_t i : sorted_indices(snap, [&](size_t i) { return (size_t)group(snap, i); }))
            keys_.push_back(mod_key(snap.mods[i]));
        Log::info("mod cards sorted: " + std::to_string(keys_.size()) + " mods");
    }
    return sorted_indices(snap, [&](size_t i) {
        return (size_t)(std::find(keys_.begin(), keys_.end(), mod_key(snap.mods[i])) - keys_.begin());
    });
}

}  // namespace oreo
