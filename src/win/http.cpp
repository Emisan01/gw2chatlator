// http.cpp
#include "http.hpp"

#include <windows.h>
#include <winhttp.h>

#include <mutex>

#ifdef _MSC_VER
#pragma comment(lib, "winhttp.lib")
#endif

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif

namespace gct {
namespace {

HINTERNET g_session = nullptr;
std::once_flag g_sessionOnce;

HINTERNET Session() {
    std::call_once(g_sessionOnce, [] {
        // AUTOMATIC_PROXY (Windows 8.1+) honours system/PAC proxies; fall back for older systems.
        g_session = WinHttpOpen(L"GW2ChatTranslator/0.3", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!g_session)
            g_session = WinHttpOpen(L"GW2ChatTranslator/0.3", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (g_session) WinHttpSetTimeouts(g_session, 5000, 5000, 10000, 15000);
    });
    return g_session;
}

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() {
        if (h) WinHttpCloseHandle(h);
    }
};

std::wstring TransportError(DWORD code) {
    switch (code) {
        case ERROR_WINHTTP_TIMEOUT: return L"Zeit\u00fcberschreitung \u2013 Server antwortet nicht";
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: return L"Server nicht gefunden \u2013 Internetverbindung?";
        case ERROR_WINHTTP_CANNOT_CONNECT: return L"Keine Verbindung zum Server";
        case ERROR_WINHTTP_SECURE_FAILURE: return L"TLS-Fehler bei der Verbindung";
        default: return L"Netzwerkfehler (" + std::to_wstring(code) + L")";
    }
}

}  // namespace

namespace {

HttpResponse Request(const wchar_t* method, const std::wstring& host, INTERNET_PORT port, bool secure,
                     const std::wstring& pathAndQuery, const std::wstring& headers, const std::string& body,
                     const wchar_t* extraHeaderName, size_t maxBytes, int receiveTimeoutMs) {
    HttpResponse r;
    auto fail = [&r](DWORD code) {
        r.error = TransportError(code);
        return r;
    };

    HINTERNET session = Session();
    if (!session) return fail(GetLastError());

    Handle connect{WinHttpConnect(session, host.c_str(), port, 0)};
    if (!connect.h) return fail(GetLastError());

    Handle request{WinHttpOpenRequest(connect.h, method, pathAndQuery.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)};
    if (!request.h) return fail(GetLastError());
    if (receiveTimeoutMs > 0) WinHttpSetTimeouts(request.h, 5000, 5000, 15000, receiveTimeoutMs);

    const wchar_t* hdr = headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str();
    const DWORD hdrLen = headers.empty() ? 0 : static_cast<DWORD>(-1L);
    void* data = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
    const DWORD len = static_cast<DWORD>(body.size());
    if (!WinHttpSendRequest(request.h, hdr, hdrLen, data, len, len, 0) || !WinHttpReceiveResponse(request.h, nullptr))
        return fail(GetLastError());

    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    r.status = status;

    if (extraHeaderName) {
        wchar_t buf[256];
        DWORD bytes = sizeof(buf);
        if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CUSTOM, extraHeaderName, buf, &bytes,
                                WINHTTP_NO_HEADER_INDEX))
            r.extraHeader.assign(buf, bytes / sizeof(wchar_t));
    }

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail)) return fail(GetLastError());
        if (avail == 0) break;
        if (r.body.size() + avail > maxBytes) {
            r.error = L"Antwort zu gro\u00df";
            return r;
        }
        const size_t old = r.body.size();
        r.body.resize(old + avail);
        DWORD read = 0;
        if (!WinHttpReadData(request.h, r.body.data() + old, avail, &read)) return fail(GetLastError());
        r.body.resize(old + read);
    }
    r.transportOk = true;
    return r;
}

}  // namespace

HttpResponse HttpsRequest(const wchar_t* method, const std::wstring& host, const std::wstring& pathAndQuery,
                          const std::wstring& headers, const std::string& body, const wchar_t* extraHeaderName,
                          size_t maxBytes) {
    return Request(method, host, INTERNET_DEFAULT_HTTPS_PORT, true, pathAndQuery, headers, body, extraHeaderName,
                   maxBytes, 0);
}

HttpResponse HttpRequestUrl(const wchar_t* method, const std::wstring& url, const std::wstring& headers,
                            const std::string& body, int receiveTimeoutMs) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, path[2048] = {}, extra[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = path;
    uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) {
        HttpResponse r;
        r.error = L"Ung\u00fcltige Adresse: " + url;
        return r;
    }
    const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    std::wstring p = std::wstring(path) + extra;
    if (p.empty()) p = L"/";
    return Request(method, host, uc.nPort, secure, p, headers, body, nullptr, 16u << 20, receiveTimeoutMs);
}

}  // namespace gct
