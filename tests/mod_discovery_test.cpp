#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>

#include "core/mod_discovery.h"
#include "core/util.h"

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
    fs::path states = root / L"ProjectOreo" / L"mod_states.ini";

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
    ok &= expect(oreo::to_lower(xml).find("value=\"pythonmod\"") != std::string::npos,
                 "missing game settings entry was not created");

    // Needing Project Oreo Runtime is something a mod says, not something assumed of every
    // Python mod. Most of them only change game logic and never draw an overlay or read a
    // controller - the NMSpy example mods do not - so a mod that says nothing gets no such
    // requirement put in its name.
    ok &= expect(found_python && !found_python->needs_runtime(),
                 "a mod without a manifest must not be treated as needing the Runtime");

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
