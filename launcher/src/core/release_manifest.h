// release_manifest.h - the small json a component publishes to say "this is the newest build".
//
// One file per component, on the default branch of its own repository. Publishing a release is
// pushing this file; the launcher does not need rebuilding for a component to move forward.
//
// Shape:
//
//     {
//       "schema_version": 1,
//       "version": "0.2.0",
//       "asset": {
//         "file":   "ProjectOreoRuntime-0.2.0.zip",
//         "url":    "https://github.com/<owner>/<repo>/releases/download/v0.2.0/...",
//         "size":   973717,
//         "sha256": "46cebe42..."
//       },
//       "notes_url": "https://github.com/<owner>/<repo>/releases/tag/v0.2.0"
//     }
//
// `sha256` is read and carried around but nothing verifies it yet - that arrives with signed
// manifests. `size` IS checked, which catches a truncated or swapped-for-something-else file
// for almost no code.
#pragma once

#include <string>

namespace oreo {

struct ReleaseManifest {
    bool ok = false;
    std::string error;          // human sentence when ok is false

    int schema_version = 0;
    std::string version;        // "0.2.0"
    std::string file;           // asset file name, for the log and the saved path
    std::string url;            // where to download it
    std::string sha256;         // carried, not yet checked
    unsigned long long size = 0;
    std::string notes_url;      // release page, so the UI can offer "what changed"
};

// Pure: no network, no disk. Everything that can be wrong with a manifest is decided here, so
// it can be tested without a server.
//
// A manifest is rejected when the schema version is newer than this launcher understands, when
// the version or url are missing, or when the url points anywhere but GitHub.
ReleaseManifest parse_release_manifest(const std::string& json);

// The highest `schema_version` this build knows. A manifest declaring more is refused rather
// than half-read: a future field could change the meaning of one we already understand.
int supported_manifest_schema();

}  // namespace oreo
