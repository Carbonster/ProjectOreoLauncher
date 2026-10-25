#include "ui/thumbnails.h"

#include <windows.h>

#include "system/files.h"
#include "system/json.h"
#include "system/log.h"
#include "system/text.h"

namespace oreo {

namespace {

const unsigned long long kMissing = 1;
const unsigned long long kMaxBytes = 4 * 1024 * 1024;

std::string picture_file(const ModInfo& mod) {
    return mod.thumbnail.empty() ? "" : path_join(mod.path, mod.thumbnail);
}

const char* mime_type(const std::string& file) {
    const std::string ext = path_ext(file);
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    return "image/png";
}

}  // namespace

void Thumbnails::sync(const std::vector<ModInfo>& mods, const CallPage& call_page) {
    for (const ModInfo& m : mods) {
        const std::string file = picture_file(m);
        if (file.empty()) continue;
        Sent& sent = sent_[file];
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (!GetFileAttributesExW(to_wide(file).c_str(), GetFileExInfoStandard, &info)) {
            if (sent.stamp != kMissing) Log::warn("thumbnail missing: " + file);
            sent.stamp = kMissing;
            sent.ok = false;
            continue;
        }
        const ULONGLONG size = ((ULONGLONG)info.nFileSizeHigh << 32) | info.nFileSizeLow;
        const ULONGLONG written =
            ((ULONGLONG)info.ftLastWriteTime.dwHighDateTime << 32) | info.ftLastWriteTime.dwLowDateTime;
        const ULONGLONG stamp = (written ^ size) | 2;   // never 0 or kMissing
        if (sent.stamp == stamp) continue;
        sent.stamp = stamp;
        sent.ok = false;
        std::string data = size <= kMaxBytes ? read_file_base64(file) : std::string();
        if (data.empty()) {
            Log::warn("thumbnail skipped (unreadable or larger than 4 MB): " + file);
            continue;
        }
        call_page("setThumb(" + json_quote(file) + ", " +
                  json_quote(std::string("data:") + mime_type(file) + ";base64," + data) + ");");
        sent.ok = true;
    }
}

std::string Thumbnails::key(const ModInfo& mod) const {
    const std::string file = picture_file(mod);
    auto sent = sent_.find(file);
    return sent != sent_.end() && sent->second.ok ? file : "";
}

}  // namespace oreo
