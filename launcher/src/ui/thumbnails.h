// thumbnails.h - mod pictures, handed to the page once each.
//
// The page cannot open local files, so a picture travels as a data URI - a 1.3 MB PNG is 1.7 MB
// of text. Inside every state that was ~10 ms per refresh on the window's thread; now a file is
// read again only when its size or time stamp changes, and the state names it by its path.
#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "model/state.h"

namespace oreo {

class Thumbnails {
public:
    using CallPage = std::function<void(const std::string& javascript)>;

    // Sends the page every picture that is new or changed. Before the state that names them.
    void sync(const std::vector<ModInfo>& mods, const CallPage& call_page);
    // The name the page knows the mod's picture by; "" when it has none.
    std::string key(const ModInfo& mod) const;

private:
    struct Sent {
        unsigned long long stamp = 0;   // from size and write time; kMissing when the file is gone
        bool ok = false;                // the page has it
    };
    std::map<std::string, Sent> sent_;   // by file path
};

}  // namespace oreo
