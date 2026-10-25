#include "updates/component_update.h"

#include <windows.h>

#include <vector>

#include "net/http.h"
#include "system/log.h"
#include "system/process.h"
#include "system/text.h"
#include "system/files.h"
#include "system/clock.h"
#include "updates/mod_files.h"
#include "updates/version.h"

namespace oreo {

namespace {

const int kManifestTimeoutSeconds = 8;
const int kDownloadTimeoutSeconds = 30;
const int kPipTimeoutMs = 10 * 60 * 1000;

unsigned read_u16(const std::string& d, size_t at) {
    return (unsigned char)d[at] | ((unsigned char)d[at + 1] << 8);
}
unsigned read_u32(const std::string& d, size_t at) {
    return (unsigned)((unsigned char)d[at]) | ((unsigned)((unsigned char)d[at + 1]) << 8) |
           ((unsigned)((unsigned char)d[at + 2]) << 16) | ((unsigned)((unsigned char)d[at + 3]) << 24);
}

}  // namespace

std::string ComponentUpdateService::backup_dir(const std::string& what) const {
    std::string stamp = timestamp_now();
    for (char& c : stamp)
        if (c == ' ') c = '_';
        else if (c == ':') c = '-';
    const std::string base = path_join(path_join(path_join(cfg_.oreo_dir, "backups"), what), stamp);
    std::string dir = base;
    for (int n = 2; dir_exists(dir); ++n) dir = base + "-" + std::to_string(n);
    return dir;
}

bool ComponentUpdateService::back_up_package(const ApprovedCatalogEntry& entry, std::string& error) const {
    const std::string site = python_.site_packages();
    std::string dist = to_lower(entry.package_name);
    for (char& c : dist)
        if (c == '-' || c == '.') c = '_';
    std::vector<std::string> folders;
    if (!entry.import_name.empty() && dir_exists(path_join(site, entry.import_name)))
        folders.push_back(entry.import_name);
    for (const std::string& name : list_dirs(site))
        if (starts_with(to_lower(name), dist + "-") && ends_with(to_lower(name), ".dist-info"))
            folders.push_back(name);
    if (folders.empty()) return true;

    const std::string backup = backup_dir(entry.id);
    for (const std::string& folder : folders) {
        std::string failed;
        if (!copy_tree(path_join(site, folder), path_join(backup, folder), &failed)) {
            error = "Could not keep a copy of " + path_join(folder, failed) + " before updating.";
            return false;
        }
    }
    Log::info("backed up " + entry.id + " to " + backup);
    return true;
}

bool zip_entries_are_safe(const std::string& archive, std::string& offending_entry) {
    offending_entry.clear();
    std::string data;
    if (!read_file(archive, data) || data.size() < 22) {
        offending_entry = "(the archive could not be read)";
        return false;
    }

    // The end-of-central-directory record sits at the very end, after a comment of unknown
    // length, so it is searched for backwards.
    size_t eocd = std::string::npos;
    size_t search_from = data.size() >= 22 ? data.size() - 22 : 0;
    for (size_t i = search_from + 1; i-- > 0;) {
        if (data.compare(i, 4, "PK\x05\x06", 4) == 0) {
            eocd = i;
            break;
        }
        if (search_from - i > 66000) break;   // comment field cannot be longer than 64 KB
    }
    if (eocd == std::string::npos || eocd + 22 > data.size()) {
        offending_entry = "(this is not a zip archive)";
        return false;
    }

    unsigned count = read_u16(data, eocd + 10);
    size_t offset = read_u32(data, eocd + 16);
    for (unsigned i = 0; i < count; ++i) {
        if (offset + 46 > data.size() || data.compare(offset, 4, "PK\x01\x02", 4) != 0) {
            offending_entry = "(the archive directory is damaged)";
            return false;
        }
        unsigned name_length = read_u16(data, offset + 28);
        unsigned extra_length = read_u16(data, offset + 30);
        unsigned comment_length = read_u16(data, offset + 32);
        if (offset + 46 + name_length > data.size()) {
            offending_entry = "(the archive directory is damaged)";
            return false;
        }
        std::string name = data.substr(offset + 46, name_length);
        if (((read_u32(data, offset + 38) >> 16) & 0170000) == 0120000) {
            offending_entry = name + " (a link)";
            return false;
        }
        if (!relative_path_is_safe(name)) {
            offending_entry = name;
            return false;
        }
        offset += 46 + name_length + extra_length + comment_length;
    }
    return true;
}

bool extract_zip(const std::string& archive, const std::string& dest_dir, std::string& error) {
    error.clear();

    std::string offending;
    if (!zip_entries_are_safe(archive, offending)) {
        error = "The archive was rejected because of the entry " + offending +
                ", which would write outside the folder it is meant for.";
        Log::error("extract refused: " + error);
        return false;
    }

    if (!make_dirs(dest_dir)) {
        error = "Could not create " + dest_dir + ".";
        return false;
    }

    // tar has shipped with Windows since Windows 10 and reads zip. Called by full path so a
    // tar.exe somewhere on PATH cannot stand in for it.
    wchar_t system_dir[MAX_PATH] = {0};
    GetSystemDirectoryW(system_dir, MAX_PATH);
    std::string tar = path_join(to_utf8(system_dir), "tar.exe");
    if (!file_exists(tar)) {
        error = "This version of Windows has no tar.exe, so the archive cannot be unpacked.";
        return false;
    }

    ProcResult r = run_capture(tar, {"-xf", archive, "-C", dest_dir}, 5 * 60 * 1000);
    if (!r.ok()) {
        error = "The archive could not be unpacked. " + (r.error.empty() ? trim(r.err) : r.error);
        Log::error("tar failed: exit=" + std::to_string(r.exit_code) + " " + trim(r.err));
        return false;
    }
    return true;
}

std::string ComponentUpdateService::download_dir() const {
    return path_join(cfg_.oreo_dir, "downloads");
}

UpdateCheck ComponentUpdateService::check(const ApprovedCatalogEntry& entry,
                                          const std::string& installed_version) const {
    UpdateCheck out;
    out.installed = installed_version;

    if (entry.manifest_url.empty()) {
        out.error = entry.name + " is not something the launcher updates.";
        return out;
    }
    if (!cfg_.check_for_updates) {
        out.error = "Update checks are off.";
        return out;
    }

    HttpResult response = http_get(entry.manifest_url, kManifestTimeoutSeconds);
    if (!response.ok) {
        // Not being able to ask is not a problem with the component, and must never look like
        // one. The installed version stays whatever it is.
        out.error = response.error;
        Log::info("update check for " + entry.id + " failed: " + response.error);
        return out;
    }

    out.manifest = parse_release_manifest(response.body);
    if (!out.manifest.ok) {
        out.error = out.manifest.error;
        Log::warn("update check for " + entry.id + ": " + out.error);
        return out;
    }

    out.ok = true;
    out.latest = out.manifest.version;
    out.notes_url = out.manifest.notes_url;
    out.update_available =
        installed_version.empty() || version_newer(out.manifest.version, installed_version);
    Log::info("update check " + entry.id + ": installed=" +
              (installed_version.empty() ? std::string("(none)") : installed_version) +
              " latest=" + out.latest + (out.update_available ? " (update available)" : ""));
    return out;
}

InstallResult ComponentUpdateService::install(const ApprovedCatalogEntry& entry,
                                              const ReleaseManifest& manifest,
                                              ProgressFn progress) const {
    InstallResult result;
    auto say = [&progress](const std::string& text) {
        if (progress) progress(text);
    };

    if (!manifest.ok) {
        result.message = manifest.error.empty() ? "There is nothing to install." : manifest.error;
        return result;
    }

    std::string name = path_filename(manifest.file);
    if (!relative_path_is_safe(name)) name = "download";
    const std::string target = path_join(download_dir(), name);
    result.details.valid = true;
    result.details.command = manifest.url;
    result.details.timestamp = timestamp_now();

    // No percentage on purpose, even though this download is ours and could report one:
    // pyMHF and NMSpy are installed by pip, whose output only arrives once it has finished, so
    // those rows would sit at a bare "Installing..." while these counted up. Half the
    // components reporting progress and half not reads as a bug. Showing it everywhere means
    // teaching run_capture to stream and pip to emit --progress-bar raw - v2, see TODO.md.
    say("Downloading " + entry.name + " " + manifest.version + "...");
    DownloadResult download =
        http_download(manifest.url, target, kDownloadTimeoutSeconds, manifest.size);
    if (!download.ok) {
        result.message = "Could not download " + entry.name + ". " + download.error;
        result.details.err = download.error;
        return result;
    }

    say("Installing " + entry.name + "...");
    switch (entry.source) {
        case ApprovedSource::GitHubWheel:
            result = install_wheel(entry, target, progress);
            break;
        case ApprovedSource::GitHubModArchive:
            result = install_mod_archive(entry, target, progress);
            break;
        case ApprovedSource::LauncherBundle:
            result = install_launcher(target, progress);
            break;
        default:
            result.message = entry.name + " cannot be installed this way.";
            break;
    }

    // The download has done its job either way; leaving 30 MB behind after every update adds up.
    if (result.ok) DeleteFileW(to_wide(target).c_str());
    return result;
}

InstallResult ComponentUpdateService::install_wheel(const ApprovedCatalogEntry& entry,
                                                    const std::string& file,
                                                    ProgressFn progress) const {
    InstallResult result;
    if (!python_.available()) {
        result.message = "ProjectOreo's private Python is unavailable, so " + entry.name +
                         " cannot be installed.";
        return result;
    }

    std::string error;
    if (!back_up_package(entry, error)) {
        result.message = error + " " + entry.name + " was not updated.";
        Log::error("backup failed for " + entry.id + ": " + error);
        return result;
    }

    // --no-index and --no-deps: the wheel is self contained and was just fetched from a place
    // we trust. Without them pip would go to PyPI on its own, which is a second download from a
    // second source in the middle of installing the first.
    // --no-cache-dir keeps ProjectOreo out of the machine-wide pip cache.
    ProcResult r = python_.run_pip({"install", "--no-index", "--no-deps", "--no-cache-dir",
                                    "--upgrade", file},
                                   kPipTimeoutMs);
    result.details.valid = true;
    result.details.command = r.command_line;
    result.details.exit_code = std::to_string(r.exit_code);
    result.details.out = r.out;
    result.details.err = r.err;
    result.details.python_path = python_.exe();
    result.details.timestamp = timestamp_now();

    if (!r.ok()) {
        result.message = entry.name + " could not be installed. " +
                         (r.error.empty() ? trim(r.err) : r.error);
        Log::error("pip install failed for " + entry.id + ": exit=" + std::to_string(r.exit_code));
        return result;
    }

    result.ok = true;
    result.message = entry.name + " installed.";
    Log::info("installed " + entry.id + " from " + path_filename(file));
    return result;
}

InstallResult ComponentUpdateService::install_mod_archive(const ApprovedCatalogEntry& entry,
                                                          const std::string& file,
                                                          ProgressFn progress) const {
    InstallResult result;
    auto say = [&progress](const std::string& text) {
        if (progress) progress(text);
    };

    const std::string& folder = entry.install_folder;
    if (!relative_path_is_safe(folder) || folder.find_first_of("\\/") != std::string::npos ||
        folder == ".") {
        result.message = entry.name + " does not name a usable folder to install into.";
        return result;
    }
    if (game_.mods_dir().empty()) {
        result.message = "The mods folder is unknown, so " + entry.name + " cannot be installed.";
        return result;
    }

    const std::string staging = path_join(download_dir(), "staging");
    const std::string unpacked = path_join(staging, entry.install_folder);
    const std::string installed = path_join(game_.mods_dir(), entry.install_folder);
    const std::string backup = backup_dir(entry.install_folder);

    remove_tree(staging);
    std::string error;
    if (!extract_zip(file, staging, error)) {
        result.message = "Could not unpack " + entry.name + ". " + error;
        result.details.valid = true;
        result.details.err = error;
        return result;
    }
    if (!dir_exists(unpacked)) {
        remove_tree(staging);
        result.message = "The downloaded archive does not contain a " + entry.install_folder +
                         " folder, so nothing was changed.";
        return result;
    }

    say("Replacing " + entry.name + "...");
    if (!replace_mod_files(unpacked, installed, backup, error)) {
        remove_tree(staging);
        result.message = "Could not update " + entry.name + ". " + error;
        result.details.valid = true;
        result.details.err = error;
        Log::error("mod update failed for " + entry.id + ": " + error);
        return result;
    }

    remove_tree(staging);
    result.ok = true;
    result.message = entry.name + " updated.";
    Log::info("installed mod " + entry.id + " into " + installed);
    return result;
}

InstallResult ComponentUpdateService::install_launcher(const std::string& file,
                                                       ProgressFn progress) const {
    InstallResult result;
    auto say = [&progress](const std::string& text) {
        if (progress) progress(text);
    };

    const std::string staging = path_join(download_dir(), "launcher");
    remove_tree(staging);
    std::string error;
    if (!extract_zip(file, staging, error)) {
        result.message = "Could not unpack the update. " + error;
        return result;
    }

    // The archive is the same one a player installs by hand, so the exe is under OreoLauncher\.
    std::string new_exe = path_join(path_join(staging, "OreoLauncher"), "OreoLauncher.exe");
    if (!file_exists(new_exe)) new_exe = path_join(staging, "OreoLauncher.exe");
    if (!file_exists(new_exe)) {
        remove_tree(staging);
        result.message = "The update does not contain a launcher, so nothing was changed.";
        return result;
    }

    const std::string current = exe_path();
    const std::string retired = current + ".old";

    const std::string backup = backup_dir("launcher");
    const std::string reference_loader = path_join(path_join(cfg_.oreo_dir, "loader"), "version.dll");
    make_dirs(backup);
    if (!CopyFileW(to_wide(current).c_str(), to_wide(path_join(backup, path_filename(current))).c_str(), FALSE) ||
        (file_exists(reference_loader) &&
         !CopyFileW(to_wide(reference_loader).c_str(), to_wide(path_join(backup, "version.dll")).c_str(), FALSE))) {
        remove_tree(staging);
        result.message = "Could not keep a copy of the current launcher, so it was not replaced.";
        return result;
    }

    say("Replacing the launcher...");
    // A running exe cannot be overwritten, but it CAN be renamed - the file keeps running from
    // its new name. So: step aside, move the new one into the vacated name, and hand over.
    DeleteFileW(to_wide(retired).c_str());
    if (!MoveFileW(to_wide(current).c_str(), to_wide(retired).c_str())) {
        remove_tree(staging);
        result.message = "The launcher could not be replaced while it is running.";
        return result;
    }
    if (!CopyFileW(to_wide(new_exe).c_str(), to_wide(current).c_str(), FALSE)) {
        // Nothing is installed and the old name is free again - put ourselves back.
        MoveFileW(to_wide(retired).c_str(), to_wide(current).c_str());
        remove_tree(staging);
        result.message = "The new launcher could not be put in place. Nothing was changed.";
        return result;
    }

    // The reference copy of the loader travels with the launcher and is never loaded, so it is
    // always safe to replace. The one inside the game's Binaries is a different matter and is
    // left to the loader's own Install button, which knows whether the game is running.
    std::string new_loader = path_join(path_join(path_join(staging, "OreoLauncher"), "loader"),
                                       "version.dll");
    if (file_exists(new_loader)) {
        make_dirs(path_dir(reference_loader));
        CopyFileW(to_wide(new_loader).c_str(), to_wide(reference_loader).c_str(), FALSE);
    }

    remove_tree(staging);
    result.ok = true;
    result.restart_required = true;
    result.restart_exe = current;
    result.message = "The launcher has been updated and is restarting.";
    Log::info("launcher replaced; restarting from " + current);
    return result;
}

}  // namespace oreo
