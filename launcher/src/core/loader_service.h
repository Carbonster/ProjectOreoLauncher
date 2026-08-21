// loader_service.h - LoaderService: the piece that makes the game start ProjectOreo at all.
//
// The loader is a proxy version.dll living in <No Man's Sky>\Binaries. Windows loads a dll
// from the executable's own folder before the system one, so NMS.exe loads ours, we forward
// every export to the real C:\Windows\System32\version.dll, and start this launcher on a
// separate thread. The game is never held up or blocked by it.
//
// The loader ships inside ProjectOreo (<ProjectOreo>\loader\version.dll), so "the latest
// loader version" is simply the one this ProjectOreo build carries - no network involved.
#pragma once

#include <string>

#include "config.h"
#include "game_service.h"
#include "state.h"

namespace oreo {

struct LoaderOpResult {
    bool success = false;
    std::string message;
    OperationDetails details;
};

class LoaderService {
public:
    LoaderService(const Config& cfg, const GameService& game) : cfg_(cfg), game_(game) {}

    void detect();

    const std::string& installed_version() const { return installed_; }
    const std::string& latest_version() const { return shipped_; }   // what we ship
    const std::string& installed_path() const { return installed_path_; }
    const std::string& source_path() const { return source_path_; }
    const std::string& error() const { return error_; }
    bool present() const { return present_; }
    bool foreign() const { return foreign_; }   // some other mod already owns version.dll

    LoaderOpResult install();   // copy our version.dll into <game>\Binaries
    LoaderOpResult update();    // same, overwriting an older one
    LoaderOpResult repair();    // same, unconditionally

    static const char* product_name();   // stamped in the dll's version resource

private:
    LoaderOpResult copy_into_place(const char* verb);

    const Config& cfg_;
    const GameService& game_;
    std::string installed_path_, source_path_, installed_, shipped_, error_;
    bool present_ = false;
    bool foreign_ = false;
};

}  // namespace oreo
