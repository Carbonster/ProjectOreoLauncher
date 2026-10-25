// component_status.h - what state a component is in, and what its button offers, from what
// detection found.
#pragma once

#include <string>

#include "model/state.h"

namespace oreo {

// What detection found outside the component itself.
struct ComponentFacts {
    bool python_available = false;
    std::string python_error;
    bool game_found = false;
    std::string game_build;      // NMS.exe's build number, "" when unreadable
    bool loader_foreign = false;  // version.dll belongs to another program
    std::string loader_error;
};

// Sets state, message and action from installed/latest and the facts. Leaves a busy
// component alone: the operation running on it owns its state.
void evaluate_component(Component& c, const ComponentFacts& facts);

}  // namespace oreo
