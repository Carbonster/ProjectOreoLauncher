// settings_test.cpp - the Settings window's model: validation, saving, and the ini writer.
//
// The window itself is HTML, but every decision it makes lives in SettingsService, so the parts
// that can be wrong are checked here: a game folder without NMS.exe, a mods folder that does not
// exist, Save writing config.ini without eating its comments, and Cancel writing nothing at all.
#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>

#include "core/settings_service.h"
#include "core/util.h"

namespace fs = std::filesystem;

namespace {

int g_failures = 0;

bool expect(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << "FAIL: " << message << "\n";
    ++g_failures;
    return false;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// The commented file the launcher writes on a first run, shortened to the keys under test.
const char* kConfigTemplate =
    "; ProjectOreo launcher configuration.\r\n"
    "\r\n"
    "[Launcher]\r\n"
    "; AlwaysShowWindow=1 opens the manager on every game start.\r\n"
    "AlwaysShowWindow=1\r\n"
    "HighContrast=0\r\n"
    "; Leave empty to detect No Man's Sky automatically.\r\n"
    "GamePath=\r\n"
    "ModsPath=\r\n"
    "\r\n"
    "[Debug]\r\n"
    "DebugHotkeys=false\r\n"
    "\r\n"
    "[Updates]\r\n"
    "CheckForUpdates=1\r\n"
    "TimeoutSeconds=6\r\n"
    "\r\n"
    "[Logging]\r\n"
    "Verbose=0\r\n";

std::string utf8(const fs::path& path) { return oreo::to_utf8(path.wstring()); }

void ini_writer_keeps_the_rest_of_the_file() {
    std::string text = kConfigTemplate;
    text = oreo::ini_set(text, "Launcher", "HighContrast", "1");

    expect(contains(text, "HighContrast=1"), "the value should be replaced");
    expect(!contains(text, "HighContrast=0"), "the old value should be gone");
    expect(contains(text, "; AlwaysShowWindow=1 opens the manager on every game start."),
           "comments must survive a write - config.ini is still a file people open");
    expect(contains(text, "TimeoutSeconds=6"),
           "keys the Settings window does not own must be left alone");

    // A key that is not in the file yet belongs in its section, not at the end of the file.
    std::string added = oreo::ini_set(text, "Updates", "CacheMinutes", "180");
    size_t updates = added.find("[Updates]");
    size_t logging = added.find("[Logging]");
    size_t added_key = added.find("CacheMinutes=180");
    expect(added_key != std::string::npos, "a missing key should be added");
    expect(updates < added_key && added_key < logging,
           "a missing key should land inside its own section");

    // A missing section is appended whole.
    std::string with_section = oreo::ini_set(added, "Python", "Dir", "C:\\Oreo\\python");
    expect(contains(with_section, "[Python]") && contains(with_section, "Dir=C:\\Oreo\\python"),
           "a missing section should be appended");

    // Values are read back exactly as written.
    oreo::IniMap parsed = oreo::ini_parse(with_section);
    expect(oreo::ini_get(parsed, "launcher.highcontrast") == "1", "the new value should parse back");
    expect(oreo::ini_get(parsed, "python.dir") == "C:\\Oreo\\python",
           "a Windows path should survive the round trip");
}

void empty_paths_mean_automatic_and_are_valid(const fs::path&) {
    oreo::SettingsValues values = oreo::SettingsService::defaults();
    oreo::SettingsErrors errors = oreo::SettingsService::validate(values);
    expect(errors.ok(), "empty paths mean 'detect automatically' and are a valid setting");

    // "Restore defaults" has to hand back the automatic paths, not some second set of defaults.
    expect(values.game_path.empty(), "the default game folder is the auto-detected one");
    expect(values.mods_path.empty(), "the default mods folder is the auto-detected one");
    expect(values.always_show_window, "AlwaysShowWindow defaults to on");
    expect(values.check_for_updates, "CheckForUpdates defaults to on");
    expect(!values.high_contrast && !values.debug_hotkeys && !values.verbose,
           "the developer and accessibility switches default to off");
}

void a_folder_without_the_game_is_refused(const fs::path& root) {
    fs::path not_the_game = root / L"NotTheGame";
    fs::create_directories(not_the_game);

    oreo::SettingsValues values = oreo::SettingsService::defaults();
    values.game_path = utf8(not_the_game);
    oreo::SettingsErrors errors = oreo::SettingsService::validate(values);

    expect(!errors.ok(), "a folder without NMS.exe must not be accepted");
    expect(!errors.game_path.empty(), "the game folder is the field that should be flagged");
    expect(errors.mods_path.empty(), "the mods folder was fine and must not be flagged too");
    expect(contains(errors.game_path, "Binaries"),
           "the message should say what the launcher was looking for");

    values.game_path = utf8(root / L"DoesNotExistAtAll");
    expect(!oreo::SettingsService::validate(values).ok(), "a folder that does not exist is refused");
}

void a_real_game_folder_is_accepted(const fs::path& root) {
    fs::path game = root / L"Game";
    oreo::write_file(utf8(game / L"Binaries" / L"NMS.exe"), "not really an exe");

    oreo::SettingsValues values = oreo::SettingsService::defaults();
    values.game_path = utf8(game);
    expect(oreo::SettingsService::validate(values).ok(),
           "a folder with Binaries\\NMS.exe is a game folder");
    expect(oreo::SettingsService::is_game_folder(utf8(game)), "is_game_folder should agree");
    expect(oreo::SettingsService::default_mods_path(utf8(game)) ==
               utf8(game / L"GAMEDATA" / L"MODS"),
           "the default mods folder is <game>\\GAMEDATA\\MODS");
}

void a_missing_mods_folder_is_refused(const fs::path& root) {
    fs::path game = root / L"Game";
    oreo::SettingsValues values = oreo::SettingsService::defaults();
    values.game_path = utf8(game);
    values.mods_path = utf8(root / L"NoSuchModsFolder");

    oreo::SettingsErrors errors = oreo::SettingsService::validate(values);
    expect(!errors.ok(), "a mods folder that does not exist must not be accepted");
    expect(!errors.mods_path.empty(), "the mods folder is the field that should be flagged");
    expect(errors.game_path.empty(), "the game folder was fine and must not be flagged too");

    fs::path real_mods = root / L"CustomMods";
    fs::create_directories(real_mods);
    values.mods_path = utf8(real_mods);
    expect(oreo::SettingsService::validate(values).ok(), "any existing folder may be the mods folder");
}

void save_writes_every_value(const fs::path& root) {
    fs::path config = root / L"config.ini";
    oreo::write_file(utf8(config), kConfigTemplate);

    fs::path game = root / L"Game";
    fs::path mods = root / L"CustomMods";

    oreo::SettingsValues values;
    values.game_path = utf8(game);
    values.mods_path = utf8(mods);
    values.always_show_window = false;
    values.check_for_updates = false;
    values.high_contrast = true;
    values.debug_hotkeys = true;
    values.verbose = true;

    std::string error;
    expect(oreo::SettingsService::save(utf8(config), values, error), error.c_str());

    oreo::IniMap saved;
    oreo::ini_load(utf8(config), saved);
    expect(oreo::ini_get(saved, "launcher.gamepath") == utf8(game), "GamePath was not saved");
    expect(oreo::ini_get(saved, "launcher.modspath") == utf8(mods), "ModsPath was not saved");
    expect(!oreo::ini_get_bool(saved, "launcher.alwaysshowwindow", true),
           "AlwaysShowWindow was not saved");
    expect(!oreo::ini_get_bool(saved, "updates.checkforupdates", true),
           "CheckForUpdates was not saved");
    expect(oreo::ini_get_bool(saved, "launcher.highcontrast", false), "HighContrast was not saved");
    expect(oreo::ini_get_bool(saved, "debug.debughotkeys", false), "DebugHotkeys was not saved");
    expect(oreo::ini_get_bool(saved, "logging.verbose", false), "Verbose was not saved");

    std::string text;
    oreo::read_file(utf8(config), text);
    expect(contains(text, "; ProjectOreo launcher configuration."),
           "saving must not strip the file's comments");
    expect(contains(text, "TimeoutSeconds=6"), "saving must not drop keys it does not own");

    // Clearing the boxes goes back to the automatic paths rather than writing an empty folder.
    oreo::SettingsValues reset = oreo::SettingsService::defaults();
    expect(oreo::SettingsService::save(utf8(config), reset, error), error.c_str());
    oreo::ini_load(utf8(config), saved);
    expect(oreo::ini_get(saved, "launcher.gamepath").empty(),
           "resetting the game folder should clear the override");
    expect(oreo::ini_get(saved, "launcher.modspath").empty(),
           "resetting the mods folder should clear the override");
}

void validation_alone_never_touches_the_file(const fs::path& root) {
    // Cancel is "do not call save". Nothing on the way to the Save button may write anything, so
    // validating a rejected form must leave config.ini byte for byte as it was.
    fs::path config = root / L"cancel.ini";
    oreo::write_file(utf8(config), kConfigTemplate);
    std::string before;
    oreo::read_file(utf8(config), before);

    oreo::SettingsValues values = oreo::SettingsService::defaults();
    values.game_path = utf8(root / L"NotTheGame");
    values.high_contrast = true;
    values.debug_hotkeys = true;
    oreo::SettingsService::validate(values);

    std::string after;
    oreo::read_file(utf8(config), after);
    expect(before == after, "validating settings must not write anything - Cancel changes nothing");
}

}  // namespace

int main() {
    wchar_t temp_path[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp_path);
    fs::path root =
        fs::path(temp_path) / (L"ProjectOreoSettingsTest_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);

    ini_writer_keeps_the_rest_of_the_file();
    empty_paths_mean_automatic_and_are_valid(root);
    a_folder_without_the_game_is_refused(root);
    a_real_game_folder_is_accepted(root);
    a_missing_mods_folder_is_refused(root);
    save_writes_every_value(root);
    validation_alone_never_touches_the_file(root);

    fs::remove_all(root, ec);
    if (g_failures) std::cerr << g_failures << " check(s) failed\n";
    return g_failures ? 1 : 0;
}
