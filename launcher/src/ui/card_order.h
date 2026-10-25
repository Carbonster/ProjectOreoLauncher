// card_order.h - the order of the mod cards.
//
// Worked out when the window opens and again after REFRESH, never in between: a card must not
// jump away from under the pointer because its switch was flipped or its install went through.
#pragma once

#include <string>
#include <vector>

#include "app/snapshot.h"

namespace oreo {

class CardOrder {
public:
    // The next quiet snapshot is sorted again.
    void resort() { resort_pending_ = true; }

    // Indices into snap.mods, in the order the cards are shown. Sorting, when one is due and
    // nothing is running: switched on and missing something, switched on with an update,
    // switched on, switched off - each by name. A mod that turned up since goes last, by name.
    std::vector<size_t> order(const AppSnapshot& snap);

private:
    bool resort_pending_ = true;
    std::vector<std::string> keys_;   // mod_key() of each card, in order
};

}  // namespace oreo
