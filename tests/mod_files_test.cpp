#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "system/files.h"
#include "system/text.h"
#include "updates/mod_files.h"

namespace fs = std::filesystem;

namespace {

bool expect(bool condition, const char* message) {
    if (condition) return true;
    std::cerr << message << "\n";
    return false;
}

std::string u8(const fs::path& p) { return oreo::to_utf8(p.wstring()); }

void put(const fs::path& p, const std::string& text) { oreo::write_file(u8(p), text); }

std::string get(const fs::path& p) {
    std::string text;
    oreo::read_file(u8(p), text);
    return text;
}

bool junction(const fs::path& link, const fs::path& to) {
    std::wstring cmd = L"cmd /c mklink /J \"" + link.wstring() + L"\" \"" + to.wstring() + L"\" >nul";
    return _wsystem(cmd.c_str()) == 0 && fs::exists(link);
}

}  // namespace

int main() {
    wchar_t temp_path[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp_path);
    fs::path root = fs::path(temp_path) /
                    (L"ProjectOreoModFilesTest_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    fs::remove_all(root, ec);

    const fs::path mod = root / L"MODS" / L"fishing_mod";
    const fs::path backups = root / L"backups" / L"fishing_mod";
    const fs::path victim = root / L"Documents";
    std::string error;
    bool ok = true;

    fs::path v1 = root / L"v1";
    put(v1 / L"mod.py", "v1");
    put(v1 / L"old.py", "v1");
    put(v1 / L"data" / L"fish.json", "v1");
    ok &= expect(oreo::replace_mod_files(u8(v1), u8(mod), u8(backups / L"1"), error), "fresh install failed");
    ok &= expect(get(mod / L"mod.py") == "v1", "fresh install did not copy the files");
    ok &= expect(fs::exists(mod / oreo::install_record_name()), "fresh install wrote no record");

    put(mod / L"photo.jpg", "family");
    put(mod / L"mine" / L"notes.txt", "notes");
    put(root / L"outside.txt", "keep");
    put(mod / oreo::install_record_name(),
        get(mod / oreo::install_record_name()) + "..\\..\\outside.txt\r\nC:\\Windows\\win.ini\r\n");
    fs::path v2 = root / L"v2";
    put(v2 / L"mod.py", "v2");
    put(v2 / L"data" / L"fish.json", "v2");
    ok &= expect(oreo::replace_mod_files(u8(v2), u8(mod), u8(backups / L"2"), error), "update failed");
    ok &= expect(get(mod / L"mod.py") == "v2", "update did not replace the mod's file");
    ok &= expect(!fs::exists(mod / L"old.py"), "a file the new version no longer has was left");
    ok &= expect(get(mod / L"photo.jpg") == "family", "the player's file was touched");
    ok &= expect(get(mod / L"mine" / L"notes.txt") == "notes", "the player's folder was touched");
    ok &= expect(get(root / L"outside.txt") == "keep", "a record line outside the folder was obeyed");
    ok &= expect(get(backups / L"2" / L"old.py") == "v1", "the removed file was not kept in the backup");
    ok &= expect(get(backups / L"2" / L"mod.py") == "v1", "the replaced file was not kept in the backup");
    ok &= expect(get(backups / L"2" / L"photo.jpg") == "family", "the backup is not the whole old folder");

    fs::remove(mod / oreo::install_record_name(), ec);
    put(mod / L"extra.py", "player");
    bool updated = oreo::replace_mod_files(u8(v2), u8(mod), u8(backups / L"3"), error);
    ok &= expect(updated, ("update without record failed: " + error).c_str());
    ok &= expect(get(mod / L"extra.py") == "player", "without a record an unknown file was deleted");
    ok &= expect(get(backups / L"2" / L"old.py") == "v1", "a later update removed an earlier backup");

    put(root / L"manager_copy.py", "manager");
    fs::remove(mod / L"mod.py", ec);
    ok &= expect(CreateHardLinkW((mod / L"mod.py").c_str(), (root / L"manager_copy.py").c_str(), nullptr) != 0,
                 "could not create a hard link for the test");
    ok &= expect(oreo::replace_mod_files(u8(v2), u8(mod), u8(backups / L"3b"), error), "update over a hard link failed");
    ok &= expect(get(mod / L"mod.py") == "v2", "the hard-linked file was not updated");
    ok &= expect(get(root / L"manager_copy.py") == "manager", "updating a hard link rewrote its twin");

    fs::path v3 = root / L"v3";
    put(v3 / L"a.txt", "v3");
    put(v3 / L"mod.py", "v3");
    put(mod / L"a.txt", "v2");
    HANDLE held = CreateFileW((mod / L"mod.py").c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    ok &= expect(!oreo::replace_mod_files(u8(v3), u8(mod), u8(backups / L"4"), error), "a held file did not fail the update");
    CloseHandle(held);
    ok &= expect(get(mod / L"a.txt") == "v2", "a failed update did not put the old file back");

    put(victim / L"tax.pdf", "important");
    fs::path linked = root / L"MODS2" / L"fishing_mod";
    fs::create_directories(linked.parent_path());
    if (expect(junction(linked, victim), "could not create a junction for the test")) {
        ok &= expect(!oreo::replace_mod_files(u8(v2), u8(linked), u8(backups / L"5"), error), "a linked mod folder was updated");
        ok &= expect(get(victim / L"tax.pdf") == "important", "the linked folder's files were touched");
        ok &= expect(!fs::exists(victim / L"mod.py"), "files were written into the linked folder");
    } else {
        ok = false;
    }

    if (expect(junction(mod / L"pictures", victim), "could not create a junction for the test")) {
        ok &= expect(!oreo::replace_mod_files(u8(v2), u8(mod), u8(backups / L"5"), error), "a mod holding a link was updated");
        ok &= expect(get(victim / L"tax.pdf") == "important", "a link inside the mod was followed");
    } else {
        ok = false;
    }

    ok &= expect(oreo::remove_tree(u8(mod)), "remove_tree failed on a folder holding a link");
    ok &= expect(!fs::exists(mod), "remove_tree left the folder behind");
    ok &= expect(get(victim / L"tax.pdf") == "important", "remove_tree followed a link inside the folder");
    ok &= expect(oreo::remove_tree(u8(linked)), "remove_tree failed on a link");
    ok &= expect(get(victim / L"tax.pdf") == "important", "remove_tree emptied the folder a link points at");

    ok &= expect(oreo::relative_path_is_safe("data\\fish.json"), "a plain path was refused");
    ok &= expect(oreo::relative_path_is_safe("fishing_mod/"), "a folder entry was refused");
    ok &= expect(oreo::relative_path_is_safe("data\\console.txt"), "a name starting like a device was refused");
    for (const char* bad : {"..\\x", "a\\..\\..\\x", "C:\\x", "\\x", "/x", "a:stream", "",
                            "a\\.. \\x", "a\\...\\x", "x.", "x ", "CON", "data\\nul.txt", "COM1.log",
                            "a\\b?c", "a\x01b"})
        ok &= expect(!oreo::relative_path_is_safe(bad), bad);

    fs::remove_all(root, ec);
    return ok ? 0 : 1;
}
