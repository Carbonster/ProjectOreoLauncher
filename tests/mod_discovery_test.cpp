#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>

#include "mods/mod_discovery.h"
#include "system/text.h"
#include "system/files.h"

namespace fs = std::filesystem;

namespace {

bool expect(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << message << "\n";
    return false;
}

}  // namespace

int main() {
    wchar_t temp_path[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp_path);
    fs::path root = fs::path(temp_path) /
                    (L"ProjectOreoModDiscoveryTest_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);

    fs::path game = root / L"Game";
    fs::path mods = game / L"GAMEDATA" / L"MODS";
    fs::path loose = mods / L"LooseMod";
    fs::path empty_mod = mods / L"EmptyMod";
    fs::path settings = game / L"Binaries" / L"SETTINGS" / L"GCMODSETTINGS.MXML";
    fs::path states = root / L"OreoLauncher" / L"mod_states.ini";

    oreo::write_file(oreo::to_utf8((loose / L"METADATA" / L"TEST.EXML").wstring()), "<Data />");
    oreo::write_file(oreo::to_utf8((loose / L"projectoreo.ini").wstring()),
                     "[Mod]\r\nName=Loose Test\r\nDescription=Optional description\r\n");
    fs::create_directories(empty_mod);
    oreo::write_file(oreo::to_utf8(settings.wstring()),
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
        "<Data template=\"GcModSettings\">\r\n"
        "\t<Property name=\"DisableAllMods\" value=\"false\" />\r\n"
        "\t<Property name=\"Data\">\r\n"
        "\t\t<Property name=\"Data\" value=\"GcModSettingsInfo\">\r\n"
        "\t\t\t<Property name=\"Name\" value=\"LOOSEMOD\" />\r\n"
        "\t\t\t<Property name=\"Enabled\" value=\"true\" />\r\n"
        "\t\t\t<Property name=\"EnabledVR\" value=\"true\" />\r\n"
        "\t\t</Property>\r\n\t</Property>\r\n</Data>\r\n");

    oreo::ModDiscoveryService discovery;
    discovery.scan(oreo::to_utf8(mods.wstring()), oreo::to_utf8(states.wstring()));
    bool ok = true;
    ok &= expect(discovery.mods().size() == 2, "each first-level folder was not detected as a mod");
    const oreo::ModInfo* loose_info = nullptr;
    for (const oreo::ModInfo& mod : discovery.mods())
        if (mod.folder == "LooseMod") loose_info = &mod;
    ok &= expect(loose_info != nullptr, "LooseMod folder was not detected");
    if (loose_info) {
        const oreo::ModInfo mod = *loose_info;
        ok &= expect(mod.is_unpacked, "unpacked mod type was not set");
        ok &= expect(mod.enabled, "game Enabled=true was not read");
        ok &= expect(mod.description == "Optional description", "description was not read");
        std::string error;
        ok &= expect(discovery.set_enabled(mod, oreo::to_utf8(mods.wstring()),
                                           oreo::to_utf8(states.wstring()), false, error),
                     error.c_str());
        ok &= expect(fs::is_directory(loose), "mod folder was moved");
    }

    std::string xml;
    oreo::read_file(oreo::to_utf8(settings.wstring()), xml);
    ok &= expect(xml.find("name=\"Enabled\" value=\"false\"") != std::string::npos,
                 "Enabled was not changed");
    ok &= expect(xml.find("name=\"EnabledVR\" value=\"false\"") != std::string::npos,
                 "EnabledVR was not changed");

    discovery.scan(oreo::to_utf8(mods.wstring()), oreo::to_utf8(states.wstring()));
    bool loose_disabled = false;
    for (const oreo::ModInfo& mod : discovery.mods())
        if (mod.folder == "LooseMod") loose_disabled = !mod.enabled;
    ok &= expect(loose_disabled, "disabled game state was not read back");

    fs::path python_mod = mods / L"PythonMod";
    oreo::write_file(oreo::to_utf8((python_mod / L"main.py").wstring()),
                     "# [tool.pymhf]\nprint('test')\n");
    discovery.scan(oreo::to_utf8(mods.wstring()), oreo::to_utf8(states.wstring()));
    const oreo::ModInfo* found_python = nullptr;
    for (const oreo::ModInfo& mod : discovery.mods())
        if (mod.folder == "PythonMod") found_python = &mod;
    ok &= expect(found_python && found_python->is_python, "Python mod was not detected");
    if (found_python) {
        std::string error;
        ok &= expect(discovery.set_enabled(*found_python, oreo::to_utf8(mods.wstring()),
                                           oreo::to_utf8(states.wstring()), false, error),
                     error.c_str());
    }
    std::string state_text;
    oreo::read_file(oreo::to_utf8(states.wstring()), state_text);
    ok &= expect(state_text.find("pythonmod=0") != std::string::npos,
                 "Python state was not saved");
    oreo::read_file(oreo::to_utf8(settings.wstring()), xml);
    ok &= expect(oreo::to_lower(xml).find("value=\"pythonmod\"") == std::string::npos,
                 "a python mod's switch must not be written into the game's mod settings");

    discovery.scan(oreo::to_utf8(mods.wstring()), oreo::to_utf8(states.wstring()));
    bool python_off = false;
    for (const oreo::ModInfo& mod : discovery.mods())
        if (mod.folder == "PythonMod") python_off = !mod.enabled;
    ok &= expect(python_off, "the launcher's python switch was not read back");

    // The game's "disable all mods" is about its file mods. It must not switch python mods off.
    oreo::write_file(oreo::to_utf8(states.wstring()), "[Mods]\r\npythonmod=1\r\n");
    oreo::read_file(oreo::to_utf8(settings.wstring()), xml);
    size_t all = xml.find("\"DisableAllMods\" value=\"false\"");
    if (all != std::string::npos) xml.replace(all, 30, "\"DisableAllMods\" value=\"true\"");
    oreo::write_file(oreo::to_utf8(settings.wstring()), xml);
    discovery.scan(oreo::to_utf8(mods.wstring()), oreo::to_utf8(states.wstring()));
    bool python_on = false, loose_on = true;
    for (const oreo::ModInfo& mod : discovery.mods()) {
        if (mod.folder == "PythonMod") python_on = mod.enabled;
        if (mod.folder == "LooseMod") loose_on = mod.enabled;
    }
    ok &= expect(python_on, "the game's DisableAllMods switched a python mod off");
    ok &= expect(!loose_on, "the game's DisableAllMods no longer switches its file mods off");

    // PLAY WITHOUT MODS flips only that one switch, and says what it was, so it can be put back.
    {
        std::string error;
        bool was = false;
        ok &= expect(discovery.set_all_game_mods_disabled(oreo::to_utf8(mods.wstring()), false, &was, error),
                     error.c_str());
        ok &= expect(was, "the previous DisableAllMods=true was not reported");
        ok &= expect(discovery.set_all_game_mods_disabled(oreo::to_utf8(mods.wstring()), true, &was, error),
                     error.c_str());
        ok &= expect(!was, "the previous DisableAllMods=false was not reported");
        oreo::read_file(oreo::to_utf8(settings.wstring()), xml);
        ok &= expect(xml.find("\"DisableAllMods\" value=\"true\"") != std::string::npos,
                     "DisableAllMods was not switched on");
        ok &= expect(xml.find("name=\"Enabled\" value=\"false\"") != std::string::npos,
                     "a mod's own Enabled flag must be left alone");
    }

    // Needing Project Oreo Runtime is something a mod says, not something assumed of every
    // Python mod. Most of them only change game logic and never draw an overlay or read a
    // controller - the NMSpy example mods do not - so a mod that says nothing gets no such
    // requirement put in its name.
    ok &= expect(found_python && !found_python->needs_runtime(),
                 "a mod without a manifest must not be treated as needing the Runtime");

    {
        const std::string mods_u8 = oreo::to_utf8(mods.wstring());
        const fs::path backups = root / L"OreoLauncher" / L"backups" / L"game_settings";
        discovery.set_backup_dir(oreo::to_utf8(backups.wstring()));
        std::string before, error;
        oreo::read_file(oreo::to_utf8(settings.wstring()), before);
        ok &= expect(discovery.set_all_game_mods_disabled(mods_u8, false, nullptr, error), error.c_str());
        std::string first, previous;
        oreo::read_file(oreo::to_utf8((backups / L"GCMODSETTINGS.first.MXML").wstring()), first);
        oreo::read_file(oreo::to_utf8((backups / L"GCMODSETTINGS.previous.MXML").wstring()), previous);
        ok &= expect(!first.empty() && first == before, "the game's settings were not copied before a change");
        ok &= expect(previous == before, "the settings before the latest change were not kept");
        ok &= expect(!fs::exists(settings.wstring() + L".oreo-new"), "a temporary file was left beside the settings");

        oreo::read_file(oreo::to_utf8(settings.wstring()), before);
        HANDLE held = CreateFileW(settings.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        ok &= expect(!discovery.set_all_game_mods_disabled(mods_u8, true, nullptr, error),
                     "settings that could not be read were written anyway");
        CloseHandle(held);
        std::string after;
        oreo::read_file(oreo::to_utf8(settings.wstring()), after);
        ok &= expect(after == before, "settings that could not be read were replaced");

        fs::path elsewhere = root / L"MyMods";
        fs::create_directories(elsewhere);
        ok &= expect(!discovery.set_all_game_mods_disabled(oreo::to_utf8(elsewhere.wstring()), true, nullptr, error),
                     "a MODS folder outside the game was treated as the game's");
        ok &= expect(!fs::exists(root / L"Binaries"), "settings were written beside a MODS folder set elsewhere");
    }

    fs::path overlay_mod = mods / L"OverlayMod";
    oreo::write_file(oreo::to_utf8((overlay_mod / L"main.py").wstring()),
                     "# [tool.pymhf]\nprint('test')\n");
    oreo::write_file(oreo::to_utf8((overlay_mod / L"projectoreo.ini").wstring()),
                     "[Mod]\r\nName=Overlay Test\r\n\r\n[Compatibility]\r\nRuntime=0.1.2\r\n");
    discovery.scan(oreo::to_utf8(mods.wstring()), oreo::to_utf8(states.wstring()));
    const oreo::ModInfo* found_overlay = nullptr;
    for (const oreo::ModInfo& mod : discovery.mods())
        if (mod.folder == "OverlayMod") found_overlay = &mod;
    ok &= expect(found_overlay != nullptr, "OverlayMod folder was not detected");
    if (found_overlay) {
        ok &= expect(found_overlay->needs_runtime(),
                     "a mod declaring Runtime under [Compatibility] should be shown needing it");
        ok &= expect(found_overlay->tested_for("runtime") == "0.1.2",
                     "the declared Runtime version should be readable like the other components");
    }

    fs::remove_all(root, ec);
    return ok ? 0 : 1;
}
