#include "app/window_rule.h"

#include "app/mod_readiness.h"

namespace oreo {

ShowReason decide_window(const AppSnapshot& snap, const WindowRule& rule) {
    ShowReason reason;
    auto add = [&reason](const std::string& line, const std::string& summary) {
        reason.reasons.push_back(line);
        if (reason.summary.empty()) reason.summary = summary;
    };

    for (size_t i = 0; i < snap.mods.size(); ++i) {
        CardButton button = card_button(snap, i);
        if (button == CardButton::None) continue;
        std::string line = snap.mods[i].name + (button == CardButton::Install ? ": INSTALL" : ": UPDATE");
        add(line, line);
    }
    reason.show = !reason.reasons.empty();

    if (rule.always_show) add("AlwaysShowWindow=1 in config.ini", "Everything is ready");
    if (rule.debug_hotkeys) add("DebugHotkeys=true in config.ini", "Debug hotkeys are on");
    if (rule.started_by_user) add("started by the user, not by the game", "");
    if (rule.always_show || rule.debug_hotkeys || rule.started_by_user) reason.show = true;
    return reason;
}

}  // namespace oreo
