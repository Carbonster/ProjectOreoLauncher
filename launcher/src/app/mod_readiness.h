// mod_readiness.h - what a mod still needs before it can run, and the button its card shows.
#pragma once

#include <string>
#include <vector>

#include "app/snapshot.h"

namespace oreo {

// One step of getting a mod ready: an install or update of a component it runs on, or - with
// an empty component id - an update of the mod itself.
struct PrepareStep {
    std::string component_id;
    ActionKind action = ActionKind::None;
};

// Everything behind a card's one INSTALL / UPDATE button, in the order it would run. Empty
// means the mod is ready.
std::vector<PrepareStep> prepare_steps(const AppSnapshot& snap, size_t mod_index);

enum class CardButton { None, Install, Update };

// INSTALL when any step installs something, UPDATE when every step updates.
CardButton card_button(const AppSnapshot& snap, size_t mod_index);
// The button's text: "Install", "Update", or "" for none.
const char* to_string(CardButton button);

}  // namespace oreo
