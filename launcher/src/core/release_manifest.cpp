#include "release_manifest.h"

#include "http.h"
#include "util.h"

namespace oreo {

namespace {

const int kSupportedSchema = 1;

// The json here is written by us and is a handful of fields deep, so a full parser would be
// more code than the thing it parses. What matters is that a malformed file is REFUSED rather
// than half-understood - every field is looked up by name and an absent one stays empty.
unsigned long long json_number(const std::string& json, const std::string& key) {
    size_t pos = json_find_key(json, key);
    if (pos == std::string::npos) return 0;
    pos = json.find(':', pos);
    if (pos == std::string::npos) return 0;
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos) return 0;
    unsigned long long value = 0;
    bool any = false;
    for (; pos < json.size() && json[pos] >= '0' && json[pos] <= '9'; ++pos) {
        value = value * 10 + (unsigned)(json[pos] - '0');
        any = true;
    }
    return any ? value : 0;
}

}  // namespace

int supported_manifest_schema() { return kSupportedSchema; }

ReleaseManifest parse_release_manifest(const std::string& json) {
    ReleaseManifest out;

    if (trim(json).empty()) {
        out.error = "The update information is empty.";
        return out;
    }

    out.schema_version = (int)json_number(json, "schema_version");
    if (out.schema_version == 0) {
        out.error = "The update information does not say which format it is in.";
        return out;
    }
    if (out.schema_version > kSupportedSchema) {
        // Refusing beats guessing: a later format may give an existing field a new meaning, and
        // acting on half of it is how an update installs the wrong thing.
        out.error = "This update needs a newer ProjectOreo. Update the launcher first.";
        return out;
    }

    out.version = trim(json_string_field(json, "version"));
    if (out.version.empty()) {
        // The runtime published its first manifest with this name before the format settled.
        out.version = trim(json_string_field(json, "runtime_version"));
    }
    out.file = trim(json_string_field(json, "file"));
    out.url = trim(json_string_field(json, "url"));
    out.sha256 = trim(json_string_field(json, "sha256"));
    out.size = json_number(json, "size");
    out.notes_url = trim(json_string_field(json, "notes_url"));

    if (out.version.empty()) {
        out.error = "The update information does not name a version.";
        return out;
    }
    if (out.url.empty()) {
        out.error = "The update information does not say where to download the file.";
        return out;
    }

    std::string why;
    if (!url_is_allowed(out.url, &why)) {
        out.error = why;
        return out;
    }

    if (out.file.empty()) {
        // Not fatal - the file name is only used to name what we saved. Take it off the url.
        size_t slash = out.url.find_last_of('/');
        out.file = slash == std::string::npos ? "download" : out.url.substr(slash + 1);
    }

    out.ok = true;
    return out;
}

}  // namespace oreo
