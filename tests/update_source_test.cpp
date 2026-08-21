// update_source_test.cpp - where the launcher is willing to download from, and what it accepts
// as a description of an update.
//
// Both are pure decisions made before a single byte is fetched, so they can be checked here
// without a server. They are also the two places where getting it wrong means running someone
// else's code, which is why they are tested at all.
#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "core/approved_catalog.h"
#include "core/component_update.h"
#include "core/http.h"
#include "core/release_manifest.h"
#include "core/util.h"

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

// ---- where we will download from --------------------------------------------------------

void github_over_https_is_allowed() {
    expect(oreo::url_is_allowed("https://github.com/Carbonster/ProjectOreoRuntime/releases/download/"
                                "v0.1.0/project_oreo_runtime-0.1.0-py3-none-win_amd64.whl"),
           "a GitHub release asset should be allowed");
    expect(oreo::url_is_allowed("https://raw.githubusercontent.com/Carbonster/X/main/latest.json"),
           "raw.githubusercontent.com should be allowed - manifests live there");
    // GitHub hands release downloads off to this host, so refusing it would break every asset.
    expect(oreo::url_is_allowed("https://objects.githubusercontent.com/github-production-release/1"),
           "the host GitHub redirects asset downloads to should be allowed");
}

void anything_else_is_refused() {
    const char* refused[] = {
        "http://github.com/Carbonster/X/releases/download/v1/a.zip",  // no TLS
        "https://evil.example/payload.zip",                           // not GitHub
        "https://github.com.evil.example/payload.zip",                // lookalike domain
        "https://notgithub.com/x.zip",
        "https://githubusercontent.com.evil.example/x.zip",
        "ftp://github.com/x.zip",
        "file:///C:/Windows/System32/cmd.exe",
        "",
    };
    for (const char* url : refused) {
        std::string why;
        if (!expect(!oreo::url_is_allowed(url, &why),
                    (std::string("should have been refused: ") + url).c_str())) {
            continue;
        }
        expect(!why.empty(), "a refusal has to say why");
    }
}

// "https://github.com@evil.example/x" reads as github.com to anything that stops at the first
// dot and does not know about userinfo. The real host is evil.example.
void a_userinfo_url_cannot_pretend_to_be_github() {
    std::string why;
    expect(!oreo::url_is_allowed("https://github.com@evil.example/payload.zip", &why),
           "a url with userinfo must not pass as GitHub");
    expect(!oreo::url_is_allowed("https://github.com:token@evil.example/payload.zip"),
           "the same with a password part");
}

// ---- what we accept as an update description ---------------------------------------------

const char* kGoodManifest = R"({
  "schema_version": 1,
  "version": "0.2.0",
  "asset": {
    "file": "ProjectOreoRuntime-0.2.0.zip",
    "url": "https://github.com/Carbonster/ProjectOreoRuntime/releases/download/v0.2.0/ProjectOreoRuntime-0.2.0.zip",
    "size": 973717,
    "sha256": "46cebe42d0b411eeb07cfc05540f4e8daee2555e8a9195109ea047e9c81e0fe8"
  },
  "notes_url": "https://github.com/Carbonster/ProjectOreoRuntime/releases/tag/v0.2.0"
})";

void a_good_manifest_is_read_whole() {
    oreo::ReleaseManifest m = oreo::parse_release_manifest(kGoodManifest);
    expect(m.ok, m.error.empty() ? "a valid manifest should be accepted" : m.error.c_str());
    expect(m.version == "0.2.0", "version");
    expect(m.file == "ProjectOreoRuntime-0.2.0.zip", "asset file name");
    expect(contains(m.url, "releases/download/v0.2.0/"), "asset url");
    expect(m.size == 973717, "asset size");
    expect(m.sha256.size() == 64, "sha256 is carried through even though nothing checks it yet");
    expect(contains(m.notes_url, "releases/tag/v0.2.0"), "notes url");
}

void a_manifest_from_the_future_is_refused() {
    oreo::ReleaseManifest m = oreo::parse_release_manifest(
        R"({"schema_version": 99, "version": "9.9.9",
            "asset": {"url": "https://github.com/x/y/releases/download/v1/a.zip"}})");
    expect(!m.ok, "a newer schema must not be acted on");
    expect(contains(m.error, "newer ProjectOreo"), "and should say what to do about it");
}

void a_manifest_pointing_off_github_is_refused() {
    oreo::ReleaseManifest m = oreo::parse_release_manifest(
        R"({"schema_version": 1, "version": "0.2.0",
            "asset": {"url": "https://evil.example/payload.zip", "size": 10}})");
    expect(!m.ok, "a manifest must not be able to send the download somewhere else");
    expect(contains(m.error, "GitHub"), "and should name the reason");
}

void incomplete_manifests_are_refused() {
    struct Case { const char* json; const char* what; };
    const Case cases[] = {
        {"", "empty"},
        {"   ", "blank"},
        {R"({"version": "1.0", "asset": {"url": "https://github.com/a/b/c.zip"}})", "no schema"},
        {R"({"schema_version": 1, "asset": {"url": "https://github.com/a/b/c.zip"}})", "no version"},
        {R"({"schema_version": 1, "version": "1.0"})", "no url"},
    };
    for (const Case& c : cases) {
        oreo::ReleaseManifest m = oreo::parse_release_manifest(c.json);
        expect(!m.ok, (std::string("should be refused: ") + c.what).c_str());
        expect(!m.error.empty(), "a refusal has to say why");
    }
}

// The runtime published its first manifest before the format settled, calling the field
// runtime_version. It still has to be readable.
void the_older_runtime_field_name_still_works() {
    oreo::ReleaseManifest m = oreo::parse_release_manifest(
        R"({"schema_version": 1, "runtime_version": "0.1.0",
            "asset": {"url": "https://github.com/Carbonster/ProjectOreoRuntime/releases/download/v0.1.0/w.whl",
                      "size": 971214}})");
    expect(m.ok, m.error.empty() ? "the older field name should still parse" : m.error.c_str());
    expect(m.version == "0.1.0", "version read from runtime_version");
    expect(m.file == "w.whl", "a missing file name is taken from the url");
}

// ---- the catalog ---------------------------------------------------------------------------

void every_manifest_url_is_one_we_would_download_from() {
    for (const oreo::ApprovedCatalogEntry& entry : oreo::approved_catalog()) {
        if (entry.manifest_url.empty()) continue;
        std::string why;
        expect(oreo::url_is_allowed(entry.manifest_url, &why),
               (entry.id + ": manifest url must pass the same check as any download: " + why).c_str());
    }
}

void pypi_components_have_no_manifest() {
    // pyMHF and NMSpy come from PyPI through pip, which does its own fetching. A manifest url
    // on them would mean two ways to install the same thing.
    for (const char* id : {"pymhf", "nmspy"}) {
        const oreo::ApprovedCatalogEntry* entry = oreo::find_approved(id);
        expect(entry != nullptr, "catalog entry should exist");
        if (entry) expect(entry->manifest_url.empty(), "a PyPI component needs no manifest url");
    }
}

void downloadable_components_have_a_manifest() {
    for (const char* id : {"launcher", "runtime", "fishing_minigame"}) {
        const oreo::ApprovedCatalogEntry* entry = oreo::find_approved(id);
        expect(entry != nullptr, "catalog entry should exist");
        if (entry) {
            expect(!entry->manifest_url.empty(),
                   "a component the launcher updates needs somewhere to ask about versions");
        }
    }
}

// ---- what a mod archive is allowed to contain -------------------------------------------
//
// A zip carries the path each file should be written to, and nothing stops that path from
// being "..\..\Windows\System32\evil.dll". Since tar.exe will not tell us what it is about
// to write, the archive's own directory is read first and the whole thing refused if any entry
// would land outside the target folder. These build zips by hand to check exactly that.

void put16(std::string& out, unsigned value) {
    out.push_back((char)(value & 0xff));
    out.push_back((char)((value >> 8) & 0xff));
}
void put32(std::string& out, unsigned value) {
    put16(out, value & 0xffff);
    put16(out, (value >> 16) & 0xffff);
}

// A store-only zip: no compression, no data. zip_entries_are_safe only reads the central
// directory, so the entries can be empty and the test stays readable.
std::string make_zip(const std::vector<std::string>& names) {
    std::string local, central;
    for (const std::string& name : names) {
        unsigned offset = (unsigned)local.size();
        local += "PK";
        put16(local, 20); put16(local, 0); put16(local, 0); put16(local, 0); put16(local, 0);
        put32(local, 0); put32(local, 0); put32(local, 0);
        put16(local, (unsigned)name.size()); put16(local, 0);
        local += name;

        central += "PK";
        put16(central, 20); put16(central, 20); put16(central, 0); put16(central, 0);
        put16(central, 0); put16(central, 0);
        put32(central, 0); put32(central, 0); put32(central, 0);
        put16(central, (unsigned)name.size()); put16(central, 0); put16(central, 0);
        put16(central, 0); put16(central, 0); put32(central, 0); put32(central, offset);
        central += name;
    }
    std::string out = local + central;
    out += "PK";
    put16(out, 0); put16(out, 0);
    put16(out, (unsigned)names.size()); put16(out, (unsigned)names.size());
    put32(out, (unsigned)central.size()); put32(out, (unsigned)local.size());
    put16(out, 0);
    return out;
}

std::string g_zip_dir;

bool zip_is_safe(const std::vector<std::string>& names) {
    std::string path = g_zip_dir + "\\case.zip";
    oreo::write_file(path, make_zip(names));
    std::string offending;
    return oreo::zip_entries_are_safe(path, offending);
}

void an_ordinary_mod_archive_is_accepted() {
    expect(zip_is_safe({"fishing_mod/", "fishing_mod/fishing_minigame_mod.py",
                        "fishing_mod/data/fish_names.json", "fishing_mod/logs/README.md"}),
           "a normal mod archive should be accepted");
}

void an_archive_that_escapes_its_folder_is_refused() {
    const std::vector<std::vector<std::string>> refused = {
        {"fishing_mod/ok.py", "../../Windows/System32/evil.dll"},
        {"..\\..\\evil.dll"},
        {"/etc/passwd"},
        {"C:\\Windows\\System32\\evil.dll"},
        {"fishing_mod/../../evil.dll"},
        {"fishing_mod/sub/.."},
        {""},
    };
    for (const std::vector<std::string>& names : refused) {
        expect(!zip_is_safe(names),
               (std::string("should be refused: ") + (names.empty() ? "" : names[0])).c_str());
    }
}

void something_that_is_not_a_zip_is_refused() {
    std::string path = g_zip_dir + "\notazip.bin";
    oreo::write_file(path, "this is not an archive at all");
    std::string offending;
    expect(!oreo::zip_entries_are_safe(path, offending), "a non-zip must not be accepted");
    expect(!offending.empty(), "and should say what was wrong");

    expect(!oreo::zip_entries_are_safe(g_zip_dir + "\\does_not_exist.zip", offending),
           "a missing archive must not be accepted");
}

}  // namespace

int main() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    std::filesystem::path root =
        std::filesystem::path(temp) / (L"ProjectOreoUpdateTest_" + std::to_wstring(GetCurrentProcessId()));
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);
    g_zip_dir = oreo::to_utf8(root.wstring());

    github_over_https_is_allowed();
    anything_else_is_refused();
    a_userinfo_url_cannot_pretend_to_be_github();

    a_good_manifest_is_read_whole();
    a_manifest_from_the_future_is_refused();
    a_manifest_pointing_off_github_is_refused();
    incomplete_manifests_are_refused();
    the_older_runtime_field_name_still_works();

    every_manifest_url_is_one_we_would_download_from();
    pypi_components_have_no_manifest();
    downloadable_components_have_a_manifest();

    an_ordinary_mod_archive_is_accepted();
    an_archive_that_escapes_its_folder_is_refused();
    something_that_is_not_a_zip_is_refused();

    std::filesystem::remove_all(root, ec);
    if (g_failures) std::cerr << g_failures << " check(s) failed\n";
    return g_failures ? 1 : 0;
}
