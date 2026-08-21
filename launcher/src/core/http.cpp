#include "http.h"

#include <windows.h>
#include <winhttp.h>

#include "log.h"
#include "util.h"

namespace oreo {

namespace {

std::string winhttp_error_text(DWORD code) {
    switch (code) {
        case ERROR_WINHTTP_TIMEOUT:
            return "Request timed out.";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
            return "Could not resolve the server name (no internet connection?).";
        case ERROR_WINHTTP_CANNOT_CONNECT:
            return "Could not connect to the server.";
        case ERROR_WINHTTP_CONNECTION_ERROR:
            return "The connection was interrupted.";
        case ERROR_WINHTTP_SECURE_FAILURE:
            return "The secure connection could not be established.";
        default:
            return "Network error " + std::to_string(code) + ".";
    }
}

struct HandleGuard {
    HINTERNET h = nullptr;
    ~HandleGuard() {
        if (h) WinHttpCloseHandle(h);
    }
};

}  // namespace

HttpResult http_get(const std::string& url, int timeout_seconds) {
    HttpResult result;
    int ms = timeout_seconds * 1000;

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    wchar_t host[256] = {0};
    wchar_t path[1024] = {0};
    parts.lpszHostName = host;
    parts.dwHostNameLength = (DWORD)(sizeof(host) / sizeof(host[0]));
    parts.lpszUrlPath = path;
    parts.dwUrlPathLength = (DWORD)(sizeof(path) / sizeof(path[0]));

    std::wstring url_w = to_wide(url);
    if (!WinHttpCrackUrl(url_w.c_str(), (DWORD)url_w.size(), 0, &parts)) {
        result.error = "Malformed URL: " + url;
        return result;
    }

    HandleGuard session;
    session.h = WinHttpOpen(L"ProjectOreo-Launcher/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) {
        result.error = winhttp_error_text(GetLastError());
        return result;
    }
    WinHttpSetTimeouts(session.h, ms, ms, ms, ms);

    HandleGuard connect;
    connect.h = WinHttpConnect(session.h, host, parts.nPort, 0);
    if (!connect.h) {
        result.error = winhttp_error_text(GetLastError());
        return result;
    }

    DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
    HandleGuard request;
    request.h = WinHttpOpenRequest(connect.h, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request.h) {
        result.error = winhttp_error_text(GetLastError());
        return result;
    }

    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr)) {
        result.error = winhttp_error_text(GetLastError());
        return result;
    }

    DWORD status = 0, status_size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status,
                        &status_size, nullptr);
    result.status = (int)status;

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.h, &available) || available == 0) break;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request.h, &chunk[0], available, &read)) break;
        result.body.append(chunk.data(), read);
        if (result.body.size() > (4u << 20)) break;  // sanity cap: 4 MB
    }

    if (status >= 200 && status < 300) {
        result.ok = true;
    } else if (status == 404) {
        result.error = "The server does not know this package (HTTP 404).";
    } else {
        result.error = "The server answered with HTTP " + std::to_string(status) + ".";
    }
    return result;
}

size_t json_find_key(const std::string& json, const std::string& key, size_t from) {
    return json.find("\"" + key + "\"", from);
}

std::string json_string_field(const std::string& json, const std::string& key, size_t from) {
    size_t pos = json_find_key(json, key, from);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos);
    if (pos == std::string::npos) return "";
    pos = json.find_first_not_of(" \t\r\n", pos + 1);
    if (pos == std::string::npos || json[pos] != '"') return "";
    std::string out;
    for (size_t i = pos + 1; i < json.size(); ++i) {
        char c = json[i];
        if (c == '\\' && i + 1 < json.size()) {
            out.push_back(json[++i]);
            continue;
        }
        if (c == '"') break;
        out.push_back(c);
    }
    return out;
}

}  // namespace oreo
