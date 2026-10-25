// view_state.h - what the page is told to draw, as the JSON its render functions take.
#pragma once

#include <string>
#include <vector>

#include "app/snapshot.h"
#include "settings/settings_service.h"

namespace oreo {

class Thumbnails;

struct ViewOptions {
    bool maximized = false;
    bool debug_hotkeys = false;
    std::vector<size_t> card_order;   // indices into the snapshot's mods
    const Thumbnails* thumbnails = nullptr;
};

// The argument of render(state).
std::string state_json(const AppSnapshot& snap, const ViewOptions& options);

// The argument of showSettings(state): the values, the defaults, and what "automatic" found.
std::string settings_json(const SettingsValues& values, const std::string& auto_game_path,
                          const std::string& auto_mods_path);

}  // namespace oreo
