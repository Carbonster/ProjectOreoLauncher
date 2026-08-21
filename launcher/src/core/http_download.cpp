// http_download.cpp - fetching files the launcher is going to run.
//
// Kept apart from http.cpp on purpose. That file answers "what is the newest version" and a
// failed answer costs nothing. This one brings back a wheel, an archive or an executable, and
// getting it wrong means running someone else's code. The rules live here, in one place.
#include <windows.h>
#include <winhttp.h>

#include <vector>

#include "http.h"
#include "log.h"
#include "util.h"

namespace oreo {

namespace {

// The only hosts the launcher will download from. Not configurable, not read from a manifest,
// not extendable by a mod: a compromised manifest can name a different FILE, never a different
// SERVER. GitHub hands release assets off to *.githubusercontent.com, so that has to be here
// too, otherwise every asset download dies on the redirect.
bool host_is_allowed(const std::string& host) {
    if (host == "github.com" || host == "api.github.com" || host == "codeload.github.com")
        return true;
    const std::string suffix = ".githubusercontent.com";
    return host.size() > suffix.size() &&
           host.compare(host.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Deliberately hand-written rather than WinHttpCrackUrl: this runs in tests, and the exact
// answer for a hostile URL matters more than convenience. Anything unexpected is a refusal.
std::string host_of(const std::string& url) {
    const std::string scheme = "https://";
    if (url.size() <= scheme.size()) return "";
    if (to_lower(url.substr(0, scheme.size())) != scheme) return "";
    size_t start = scheme.size();
    size_t end = url.find_first_of("/?#", start);
    std::string authority = url.substr(start, end == std::string::npos ? std::string::npos : end - start);
    // "https://github.com@evil.example/x" - the real host is the part after the @, and reading
    // it as github.com is exactly the mistake this check exists to avoid.
    if (authority.find('@') != std::string::npos) return "";
    size_t colon = authority.find(':');
    if (colon != std::string::npos) authority = authority.substr(0, colon);
    return to_lower(authority);
}

struct HandleGuard {
    HINTERNET h = nullptr;
    ~HandleGuard() {
        if (h) WinHttpCloseHandle(h);
    }
};

std::string error_text(DWORD code) {
    switch (code) {
        case ERROR_WINHTTP_TIMEOUT: return "The download timed out.";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
            return "Could not resolve the server name (no internet connection?).";
        case ERROR_WINHTTP_CANNOT_CONNECT: return "Could not connect to the server.";
        case ERROR_WINHTTP_CONNECTION_ERROR: return "The connection was interrupted.";
        case ERROR_WINHTTP_SECURE_FAILURE:
            return "The secure connection could not be established.";
        default: return "Network error " + std::to_string(code) + ".";
    }
}

// Where the request actually ended up. GitHub redirects asset downloads, and a redirect is a
// second chance to send us somewhere else - so the destination is checked, not just the start.
std::string final_url(HINTERNET request) {
    DWORD size = 0;
    WinHttpQueryOption(request, WINHTTP_OPTION_URL, nullptr, &size);
    if (size == 0) return "";
    std::wstring buffer(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_URL, &buffer[0], &size)) return "";
    buffer.resize(wcslen(buffer.c_str()));
    return to_utf8(buffer);
}

}  // namespace

bool url_is_allowed(const std::string& url, std::string* why) {
    std::string host = host_of(url);
    if (host.empty()) {
        if (why) *why = "Only https addresses are accepted, and this one is not usable: " + url;
        return false;
    }
    if (!host_is_allowed(host)) {
        if (why)
            *why = "ProjectOreo only downloads from GitHub, and this address points at " + host +
                   ". Nothing was fetched.";
        return false;
    }
    return true;
}

DownloadResult http_download(const std::string& url, const std::string& dest_path,
                             int timeout_seconds, unsigned long long expected_size,
                             DownloadProgressFn progress) {
    DownloadResult result;

    if (!url_is_allowed(url, &result.error)) {
        Log::error("download refused: " + result.error);
        return result;
    }

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {0};
    wchar_t path[2048] = {0};
    parts.lpszHostName = host;
    parts.dwHostNameLength = (DWORD)(sizeof(host) / sizeof(host[0]));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = (DWORD)(sizeof(path) / sizeof(path[0]));

    std::wstring url_w = to_wide(url);
    if (!WinHttpCrackUrl(url_w.c_str(), (DWORD)url_w.size(), 0, &parts)) {
        result.error = "Malformed URL: " + url;
        return result;
    }

    const int ms = timeout_seconds * 1000;
    HandleGuard session;
    session.h = WinHttpOpen(L"ProjectOreo-Launcher/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) {
        result.error = error_text(GetLastError());
        return result;
    }
    // Generous receive timeout: this is a file, not a version string, and a slow connection is
    // not a failure. The other three stay short so a dead server is noticed quickly.
    WinHttpSetTimeouts(session.h, ms, ms, ms, ms * 10);

    HandleGuard connect;
    connect.h = WinHttpConnect(session.h, host, parts.nPort, 0);
    if (!connect.h) {
        result.error = error_text(GetLastError());
        return result;
    }

    HandleGuard request;
    request.h = WinHttpOpenRequest(connect.h, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request.h) {
        result.error = error_text(GetLastError());
        return result;
    }

    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                            0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        result.error = error_text(GetLastError());
        return result;
    }

    // WinHTTP followed any redirects on its own; make sure they did not walk us off GitHub.
    std::string landed = final_url(request.h);
    if (!landed.empty() && !url_is_allowed(landed, &result.error)) {
        Log::error("download refused after redirect: " + result.error);
        return result;
    }

    DWORD status = 0, status_size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr,
                        &status, &status_size, nullptr);
    result.status = (int)status;
    if (status < 200 || status >= 300) {
        result.error = status == 404 ? "The file is not there any more (HTTP 404)."
                                     : "The server answered with HTTP " + std::to_string(status) + ".";
        return result;
    }

    unsigned long long announced = 0;
    {
        DWORD length = 0, length_size = sizeof(length);
        if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                nullptr, &length, &length_size, nullptr)) {
            announced = length;
        }
    }
    if (expected_size && announced && announced != expected_size) {
        result.error = "The file on the server is a different size than expected (" +
                       std::to_string(announced) + " instead of " + std::to_string(expected_size) +
                       "). Nothing was downloaded.";
        Log::error("download refused: " + result.error);
        return result;
    }

    // Written under a temporary name and renamed at the very end: an interrupted download must
    // never look like a finished one to whatever installs it next.
    make_dirs(path_dir(dest_path));
    const std::string temp_path = dest_path + ".part";
    DeleteFileW(to_wide(temp_path).c_str());
    HANDLE file = CreateFileW(to_wide(temp_path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        result.error = "Could not write to " + temp_path + ".";
        return result;
    }

    std::vector<char> buffer(64 * 1024);
    bool write_failed = false;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available) || available == 0) break;
        while (available > 0) {
            DWORD want = available < buffer.size() ? available : (DWORD)buffer.size();
            DWORD read = 0;
            if (!WinHttpReadData(request.h, buffer.data(), want, &read) || read == 0) {
                available = 0;
                break;
            }
            DWORD written = 0;
            if (!WriteFile(file, buffer.data(), read, &written, nullptr) || written != read) {
                write_failed = true;
                available = 0;
                break;
            }
            result.bytes += read;
            available -= read;
        }
        if (write_failed) break;
        if (progress) progress(result.bytes, announced ? announced : expected_size);
    }
    CloseHandle(file);

    auto give_up = [&](const std::string& message) {
        DeleteFileW(to_wide(temp_path).c_str());
        result.ok = false;
        result.error = message;
        Log::error("download failed: " + message);
        return result;
    };

    if (write_failed) return give_up("The download could not be written to disk (out of space?).");
    if (result.bytes == 0) return give_up("The server sent an empty file.");
    if (expected_size && result.bytes != expected_size) {
        return give_up("The download is the wrong size: " + std::to_string(result.bytes) +
                       " bytes instead of " + std::to_string(expected_size) + ".");
    }

    DeleteFileW(to_wide(dest_path).c_str());
    if (!MoveFileW(to_wide(temp_path).c_str(), to_wide(dest_path).c_str())) {
        return give_up("The download finished but could not be moved into place: " + dest_path);
    }

    result.ok = true;
    Log::info("downloaded " + std::to_string(result.bytes) + " bytes to " + dest_path);
    return result;
}

}  // namespace oreo
