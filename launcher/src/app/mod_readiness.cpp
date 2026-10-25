#include "app/mod_readiness.h"

#include "model/component_ids.h"

namespace oreo {

std::vector<PrepareStep> prepare_steps(const AppSnapshot& snap, size_t mod_index) {
    std::vector<PrepareStep> steps;
    if (mod_index >= snap.mods.size()) return steps;
    const ModInfo& mod = snap.mods[mod_index];

    // NMSpy loads every Python mod; the Runtime only the mods that ask for it. pyMHF comes with
    // NMSpy and pip picks which one: it is installed when missing but never updated from here,
    // because a pyMHF newer than the one NMSpy was built against breaks mods (0.2.4 removed a
    // file NMSpy 170671.3 imports).
    if (mod.is_python) {
        std::vector<std::string> ids{kIdNmspy, kIdPymhf};
        if (mod.needs_runtime()) ids.push_back(kIdRuntime);
        for (const std::string& id : ids) {
            for (const Component& c : snap.components) {
                if (c.id != id || c.busy) continue;
                const bool wanted = c.action == ActionKind::Install ||
                                    (c.action == ActionKind::Update && id != kIdPymhf);
                if (wanted) steps.push_back({id, c.action});
            }
        }
    }
    auto update = snap.mod_updates.find(mod_index);
    if (update != snap.mod_updates.end() && update->second == AppSnapshot::UpdateState::Available)
        steps.push_back({"", ActionKind::Update});
    return steps;
}

CardButton card_button(const AppSnapshot& snap, size_t mod_index) {
    std::vector<PrepareStep> steps = prepare_steps(snap, mod_index);
    if (steps.empty()) return CardButton::None;
    for (const PrepareStep& step : steps)
        if (step.action == ActionKind::Install) return CardButton::Install;
    return CardButton::Update;
}

const char* to_string(CardButton button) {
    switch (button) {
        case CardButton::Install: return "Install";
        case CardButton::Update: return "Update";
        default: return "";
    }
}

}  // namespace oreo
