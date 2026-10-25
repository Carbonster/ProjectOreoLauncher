#include "ui/component_texts.h"

#include "model/component_ids.h"

namespace oreo {

namespace {

struct ComponentText {
    const char* id;
    const char* why_needed;
};

const ComponentText kTexts[] = {
    {kIdPymhf,
     "A Python mod is not a game file - it is a program that has to run inside No Man's Sky while "
     "you play. pyMHF is what puts it there and lets it read and change what the game is doing, "
     "moment to moment."},
    {kIdNmspy,
     "Regular mods change game files. Mods built on NMSpy work inside the game: they can see your "
     "ship's speed, the weather, how your fishing is going, and change them while you play."},
    {kIdRuntime,
     "Some mods need more than game logic: their own interface drawn on top of the game, and a "
     "controller they can read while the game reads it too. Project Oreo Runtime provides both, so "
     "every mod does not have to build them again."},
    {kIdLoader,
     "The loader is a small file next to the game. The game loads it on its own at startup, which "
     "is what lets ProjectOreo start with your game and not need a separate shortcut or Steam "
     "launch option."},
};

}  // namespace

const char* why_needed(const std::string& component_id) {
    for (const ComponentText& text : kTexts)
        if (component_id == text.id) return text.why_needed;
    return "";
}

std::string repo_owner(const std::string& url) {
    const std::string marker = "github.com/";
    size_t start = url.find(marker);
    if (start == std::string::npos) return "";
    start += marker.size();
    size_t end = url.find('/', start);
    return end == std::string::npos ? url.substr(start) : url.substr(start, end - start);
}

}  // namespace oreo
