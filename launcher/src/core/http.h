// http.h - minimal HTTPS GET (WinHTTP) plus just enough JSON reading for version lookups.
// Deliberately tiny: the launcher only ever fetches small JSON documents, and a failed fetch
// is never fatal - it downgrades a component to "latest version unknown".
#pragma once

#include <functional>
#include <string>

namespace oreo {

struct HttpResult {
    bool ok = false;
    int status = 0;
    std::string body;
    std::string error;   // human readable ("No internet connection", "Request timed out", ...)
};

HttpResult http_get(const std::string& url, int timeout_seconds);

// ---- downloading files ------------------------------------------------------------------
//
// Everything below fetches content the launcher will then RUN: a wheel it installs, an archive
// it unpacks over a mod, its own replacement executable. So the set of places it is willing to
// fetch from is fixed in this binary. A manifest cannot point somewhere else, and neither can a
// mod - the worst either can do is name a file on a host that is already trusted.

// https only, and only GitHub. Returns false and fills `why` with a sentence worth showing.
bool url_is_allowed(const std::string& url, std::string* why = nullptr);

struct DownloadResult {
    bool ok = false;
    int status = 0;
    unsigned long long bytes = 0;   // what actually landed on disk
    std::string error;
};

// Streams `url` into `dest_path`, creating parent directories. Writes to a temporary file and
// renames on success, so a failed or interrupted download never leaves a half file where a
// whole one is expected.
//
// `expected_size` of 0 means "unknown, accept whatever arrives". A non-zero value that does not
// match is an error and the file is discarded - the cheap half of verifying a download, worth
// having even before signatures land.
//
// `progress` is called with (bytes so far, total or 0 when the server did not say).
using DownloadProgressFn = std::function<void(unsigned long long, unsigned long long)>;
DownloadResult http_download(const std::string& url, const std::string& dest_path,
                             int timeout_seconds, unsigned long long expected_size = 0,
                             DownloadProgressFn progress = nullptr);

// Reads a string field out of a flat-ish JSON document: finds "key" at or after `from` and
// returns the string that follows it. Returns "" when absent.
std::string json_string_field(const std::string& json, const std::string& key, size_t from = 0);

// Position of `"key"` in the document, or std::string::npos.
size_t json_find_key(const std::string& json, const std::string& key, size_t from = 0);

}  // namespace oreo
