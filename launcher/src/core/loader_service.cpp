#include "loader_service.h"

#include <windows.h>

#include "log.h"
#include "util.h"

namespace oreo {

namespace {
const char* kProductName = "ProjectOreo Loader";
}

const char* LoaderService::product_name() { return kProductName; }

void LoaderService::detect() {
    installed_.clear();
    error_.clear();
    present_ = false;
    foreign_ = false;

    source_path_ = path_join(path_join(cfg_.oreo_dir, "loader"), "version.dll");
    shipped_ = file_exists(source_path_) ? file_version_field(source_path_, "FileVersion") : "";

    if (!game_.found()) {
        error_ = "No Man's Sky was not found, so the loader state is unknown.";
        return;
    }
    installed_path_ = path_join(game_.binaries_dir(), "version.dll");

    if (!file_exists(installed_path_)) {
        Log::info("loader: not installed (" + installed_path_ + " missing)");
        return;
    }
    present_ = true;

    std::string product = file_version_field(installed_path_, "ProductName");
    if (product != kProductName) {
        foreign_ = true;
        error_ = "Another program already uses " + installed_path_ +
                 (product.empty() ? "." : " (" + product + ").") +
                 " ProjectOreo cannot start with the game until that is resolved.";
        Log::warn("loader: foreign version.dll in Binaries, product=" + product);
        return;
    }

    installed_ = file_version_field(installed_path_, "FileVersion");
    Log::info("loader installed: " + (installed_.empty() ? std::string("(unknown version)") : installed_) +
              ", shipped: " + (shipped_.empty() ? std::string("(missing)") : shipped_));
}

LoaderOpResult LoaderService::install() { return copy_into_place("install"); }
LoaderOpResult LoaderService::update() { return copy_into_place("update"); }
LoaderOpResult LoaderService::repair() { return copy_into_place("repair"); }

LoaderOpResult LoaderService::copy_into_place(const char* verb) {
    LoaderOpResult out;
    out.details.valid = true;
    out.details.timestamp = timestamp_now();
    out.details.command = "copy \"" + source_path_ + "\" -> \"" + installed_path_ + "\"";

    if (!game_.found()) {
        out.message = "No Man's Sky was not found, so the loader cannot be installed. Set GamePath in "
                      "config.ini.";
        return out;
    }
    if (!file_exists(source_path_)) {
        out.message = std::string("ProjectOreo's own loader file is missing (") + source_path_ +
                      "). Reinstall ProjectOreo.";
        out.details.err = "source file not found";
        return out;
    }
    if (foreign_) {
        out.message = error_;
        return out;
    }
    // The game holds version.dll open while it runs; overwriting it would fail with a sharing
    // violation, so say what to do instead of surfacing a Windows error code.
    if (game_.running()) {
        out.message = "No Man's Sky is running and is using the current loader. Close the game, then "
                      "try again.";
        out.details.err = "target file is in use by NMS.exe";
        return out;
    }

    if (!CopyFileW(to_wide(source_path_).c_str(), to_wide(installed_path_).c_str(), FALSE)) {
        DWORD code = GetLastError();
        out.details.exit_code = std::to_string(code);
        out.details.err = "CopyFile failed with Windows error " + std::to_string(code);
        out.message = code == ERROR_ACCESS_DENIED
                          ? "Windows denied writing to " + installed_path_ +
                                ". Close No Man's Sky (and any anti-cheat/antivirus prompt) and try again."
                          : std::string("Could not ") + verb + " the loader. See Details.";
        Log::error("loader " + std::string(verb) + " failed: " + out.details.err);
        return out;
    }

    detect();
    out.success = present_ && !foreign_;
    out.message = out.success ? "ProjectOreo Loader " + (installed_.empty() ? std::string("") : installed_ + " ") +
                                    "is installed in the game's Binaries folder."
                              : "The loader was copied but could not be verified afterwards.";
    out.details.out = "installed version after copy: " + (installed_.empty() ? "(unknown)" : installed_);
    Log::info("loader " + std::string(verb) + ": " + out.message);
    return out;
}

}  // namespace oreo
