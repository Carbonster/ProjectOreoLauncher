#include "game_service.h"

#include <windows.h>

#include <vector>

#include "log.h"
#include "process.h"
#include "util.h"

namespace oreo {

namespace {

// Build number -> marketing version. Shipped as an editable file so a new game release only
// needs a line here, never a rebuild. Unknown builds are shown as "build NNNNNN".
const char* kVersionTableName = "game_versions.ini";

const char* kDefaultVersionTable =
    "; Maps the build number inside NMS.exe to the version players talk about.\r\n"
    "; A build that is missing here is displayed as \"build <number>\" - that is not an error.\r\n"
    "\r\n"
    "[Builds]\r\n"
    "170671=6.6\r\n";

std::string read_registry_string(HKEY root, const wchar_t* subkey, const wchar_t* value) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root, subkey, 0, KEY_READ | KEY_WOW64_32KEY, &key) != ERROR_SUCCESS) {
        if (RegOpenKeyExW(root, subkey, 0, KEY_READ, &key) != ERROR_SUCCESS) return "";
    }
    wchar_t buf[1024];
    DWORD size = sizeof(buf);
    DWORD type = 0;
    std::string out;
    if (RegQueryValueExW(key, value, nullptr, &type, (LPBYTE)buf, &size) == ERROR_SUCCESS &&
        (type == REG_SZ || type == REG_EXPAND_SZ)) {
        out = to_utf8(std::wstring(buf, size / sizeof(wchar_t) > 0 ? size / sizeof(wchar_t) - 1 : 0));
    }
    RegCloseKey(key);
    return trim(out);
}

// Steam library roots: the install itself plus every path listed in libraryfolders.vdf.
std::vector<std::string> steam_library_roots() {
    std::vector<std::string> roots;
    std::string steam = read_registry_string(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (steam.empty())
        steam = read_registry_string(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath");
    if (steam.empty()) return roots;
    for (char& c : steam)
        if (c == '/') c = '\\';
    roots.push_back(steam);

    // The vdf lives in one of two places depending on the Steam client version.
    for (const char* rel : {"steamapps\\libraryfolders.vdf", "config\\libraryfolders.vdf"}) {
        std::string text;
        if (!read_file(path_join(steam, rel), text)) continue;
        // Entries look like:   "path"    "D:\\SteamLibrary"
        size_t pos = 0;
        while ((pos = text.find("\"path\"", pos)) != std::string::npos) {
            size_t open = text.find('"', pos + 6);
            if (open == std::string::npos) break;
            size_t close = text.find('"', open + 1);
            if (close == std::string::npos) break;
            std::string raw = text.substr(open + 1, close - open - 1);
            std::string clean;
            for (size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\') continue;
                clean.push_back(raw[i] == '/' ? '\\' : raw[i]);
            }
            if (!clean.empty()) roots.push_back(clean);
            pos = close;
        }
    }
    return roots;
}

bool looks_like_game_root(const std::string& root) {
    if (root.empty()) return false;
    return file_exists(path_join(path_join(root, "Binaries"), "NMS.exe"));
}

}  // namespace

std::string GameService::autodetect_root(const std::string& oreo_dir) {
    // 1. We normally live in <No Man's Sky>\Binaries\ProjectOreo, so the game is two levels up.
    std::string next_to_us = path_dir(path_dir(oreo_dir));
    if (looks_like_game_root(next_to_us)) return next_to_us;

    // 2. A running game knows its own path better than any registry key.
    std::string running_exe = process_exe_path(find_process_id("NMS.exe"));
    if (!running_exe.empty()) {
        std::string root = path_dir(path_dir(running_exe));
        if (looks_like_game_root(root)) return root;
    }

    // 3. Steam libraries.
    for (const std::string& lib : steam_library_roots()) {
        std::string candidate = path_join(path_join(path_join(lib, "steamapps"), "common"), "No Man's Sky");
        if (looks_like_game_root(candidate)) return candidate;
    }
    return "";
}

bool GameService::accept_root(const std::string& root, const std::string& how) {
    if (root.empty()) return false;
    std::string binaries = path_join(root, "Binaries");
    std::string exe = path_join(binaries, "NMS.exe");
    if (!file_exists(exe)) return false;
    root_ = root;
    binaries_ = binaries;
    exe_ = exe;
    mods_ = cfg_.mods_path.empty() ? path_join(path_join(root, "GAMEDATA"), "MODS") : cfg_.mods_path;
    how_ = how;
    found_ = true;
    return true;
}

void GameService::detect() {
    found_ = false;
    error_.clear();

    // 1. Explicit override always wins - a user who set it wants exactly that install.
    if (!cfg_.game_path.empty()) {
        if (accept_root(cfg_.game_path, "config.ini GamePath")) {
            Log::info("NMS path (config.ini): " + root_);
        } else {
            error_ = "The game folder is set to \"" + cfg_.game_path +
                     "\", but no Binaries\\NMS.exe was found there. Fix it in Settings.";
            Log::error(error_);
        }
    }

    // 2. Otherwise let detection do its job: next to the launcher, the running game, Steam.
    if (!found_) accept_root(autodetect_root(cfg_.oreo_dir), "automatic detection");

    if (!found_) {
        if (error_.empty())
            error_ = "No Man's Sky could not be found automatically. "
                     "Set the game folder in Settings.";
        Log::error("NMS not found");
        return;
    }

    Log::info("NMS path: " + root_ + "  (found via " + how_ + ")");
    Log::info("MODS path: " + mods_);
    read_build();
    resolve_marketing();
    refresh_running();
}

void GameService::read_build() {
    // NMS.exe's FileVersion string is the build number itself ("170671"). The numeric
    // FILEVERSION resource holds an unrelated internal number, so the string is what we read.
    build_ = file_version_field(exe_, "FileVersion");
    Log::info("NMS build: " + (build_.empty() ? std::string("(unreadable)") : build_));
}

void GameService::resolve_marketing() {
    marketing_.clear();
    if (build_.empty()) return;

    std::string table_path = path_join(cfg_.oreo_dir, kVersionTableName);
    if (!file_exists(table_path)) write_file(table_path, kDefaultVersionTable);

    IniMap table;
    if (!ini_load(table_path, table)) return;
    marketing_ = ini_get(table, "builds." + build_, "");
    if (marketing_.empty())
        Log::info("build " + build_ + " is not in " + std::string(kVersionTableName) +
                  " - showing the build number");
}

std::string GameService::display_version() const {
    if (!marketing_.empty()) return marketing_;
    if (!build_.empty()) return "build " + build_;
    return "";
}

void GameService::refresh_running() { pid_ = find_process_id("NMS.exe"); }

}  // namespace oreo
