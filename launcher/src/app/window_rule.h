// window_rule.h - whether the launcher window opens on its own when the game starts.
#pragma once

#include "app/snapshot.h"

namespace oreo {

struct WindowRule {
    bool always_show = false;     // AlwaysShowWindow in config.ini
    bool debug_hotkeys = false;
    bool started_by_user = false;
};

// Opens when a mod card has INSTALL or UPDATE to press, for a mod switched on or off, or when
// the rule says to always open. Nothing else the player could not act on opens it.
ShowReason decide_window(const AppSnapshot& snap, const WindowRule& rule);

}  // namespace oreo
