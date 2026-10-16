// http.cpp
#include "core/i18n.hpp"
#include "http.hpp"
#include "win/hash.hpp"

#include <windows.h>
#include <winhttp.h>

#include <mutex>
#include <vector>

#ifdef _MSC_VER
#pragma comment(lib, "winhttp.lib")
#endif

#ifndef WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY
#define WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY 4
#endif
#ifndef WINHTTP_QUERY_FLAG_NUMBER64
#define WINHTTP_QUERY_FLAG_NUMBER64 0x08000000
#endif

namespace gct {
namespace {

HINTERNET g_session = nullptr;
std::once_flag g_sessionOnce;

HINTERNET Session() {
    std::call_once(g_sessionOnce, [] {
        // AUTOMATIC_PROXY (Windows 8.1+) honours system/PAC proxies; fall back for older systems.
        g_session = WinHttpOpen(L"GW2ChatTranslator/0.5", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!g_session)
            g_session = WinHttpOpen(L"GW2ChatTranslator/0.5", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
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
        case ERROR_WINHTTP_TIMEOUT: return Tr(L"Timeout – the server does not answer");
        case ERROR_WINHTTP_NAME_NOT_RESOLVED: return Tr(L"Server not found – internet connection?");
        case ERROR_WINHTTP_CANNOT_CONNECT: return Tr(L"No connection to the server");
        case ERROR_WINHTTP_SECURE_FAILURE: return Tr(L"TLS error on the connection");
        default: return TrF(L"Network error ({1})", {std::to_wstring(code)});
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
    if (receiveTimeoutMs > 0) {
        const int connTimeout = std::min(receiveTimeoutMs, 2000);
        WinHttpSetTimeouts(request.h, connTimeout, connTimeout, 3000, receiveTimeoutMs);
    }

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
            r.error = Tr(L"Answer too large");
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
                          size_t maxBytes, int receiveTimeoutMs) {
    return Request(method, host, INTERNET_DEFAULT_HTTPS_PORT, true, pathAndQuery, headers, body, extraHeaderName,
                   maxBytes, receiveTimeoutMs);
}

HttpResponse HttpRequestUrl(const wchar_t* method, const std::wstring& url, const std::wstring& headers,
                            const std::string& body, int receiveTimeoutMs, size_t maxBytes) {
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
        r.error = TrF(L"Invalid address: {1}", {url});
        return r;
    }
    const bool secure = uc.nScheme == INTERNET_SCHEME_HTTPS;
    std::wstring p = std::wstring(path) + extra;
    if (p.empty()) p = L"/";
    return Request(method, host, uc.nPort, secure, p, headers, body, nullptr, maxBytes, receiveTimeoutMs);
}

namespace {

// The ".tmp" file of a download: removed on every way out unless Keep() was reached.
struct TmpFile {
    std::wstring path;
    HANDLE h = INVALID_HANDLE_VALUE;
    bool keep = false;
    ~TmpFile() {
        Close();
        if (!keep) DeleteFileW(path.c_str());
    }
    void Close() {
        if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
};

}  // namespace

bool HttpDownloadToFile(const std::wstring& url, const std::wstring& path, uint64_t expectedBytes,
                        const std::string& expectedSha256Hex,
                        const std::function<bool(uint64_t done, uint64_t total)>& progress, std::wstring* error) {
    auto fail = [error](const std::wstring& why) {
        if (error) *error = why;
        return false;
    };
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {}, urlPath[2048] = {}, extra[2048] = {};
    uc.lpszHostName = host;
    uc.dwHostNameLength = 256;
    uc.lpszUrlPath = urlPath;
    uc.dwUrlPathLength = 2048;
    uc.lpszExtraInfo = extra;
    uc.dwExtraInfoLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &uc)) return fail(TrF(L"Invalid address: {1}", {url}));
    std::wstring object = std::wstring(urlPath) + extra;
    if (object.empty()) object = L"/";

    HINTERNET session = Session();
    if (!session) return fail(TransportError(GetLastError()));
    Handle connect{WinHttpConnect(session, host, uc.nPort, 0)};
    if (!connect.h) return fail(TransportError(GetLastError()));
    Handle request{WinHttpOpenRequest(connect.h, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
    if (!request.h) return fail(TransportError(GetLastError()));
    // Generous receive timeout per read: a CDN may stall a few seconds on a large file. Redirects (Hugging Face ->
    // its CDN) are followed by WinHTTP itself, never from https to http.
    WinHttpSetTimeouts(request.h, 10000, 10000, 30000, 60000);
    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.h, nullptr))
        return fail(TransportError(GetLastError()));

    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &size, WINHTTP_NO_HEADER_INDEX);
    if (status != 200) return fail(TrF(L"Download failed (HTTP {1}).", {std::to_wstring(status)}));
    ULONGLONG length = 0;
    size = sizeof(length);
    if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER64,
                             WINHTTP_HEADER_NAME_BY_INDEX, &length, &size, WINHTTP_NO_HEADER_INDEX))
        length = 0;  // not sent (chunked): unknown
    const std::wstring sizeWrong = Tr(L"The download is incomplete or wrong (size does not match) – please try again.");
    if (expectedBytes && length && length != expectedBytes) return fail(sizeWrong);  // another file: do not fetch it
    const uint64_t total = length ? length : expectedBytes;

    TmpFile tmp;
    tmp.path = path + L".tmp";
    tmp.h = CreateFileW(tmp.path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (tmp.h == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        tmp.keep = code == ERROR_SHARING_VIOLATION;  // another download writes it right now: leave it alone
        return fail(TrF(L"Cannot write {1} (error {2}).", {tmp.path, std::to_wstring(code)}));
    }
    const std::wstring writeFailed = Tr(L"Writing the download failed – is the disk full?");

    Sha256 sha;
    if (!sha.Ok()) return fail(Tr(L"The checksum cannot be computed on this PC."));
    // Reads fill a 1 MB buffer; it goes to disk (and to progress) when full, at the end, or after half a second, so a
    // slow line still shows progress and can be cancelled.
    std::vector<unsigned char> buf(1u << 20);
    size_t filled = 0;
    uint64_t done = 0;
    ULONGLONG lastFlush = GetTickCount64();
    for (;;) {
        DWORD read = 0;
        if (!WinHttpReadData(request.h, buf.data() + filled, static_cast<DWORD>(buf.size() - filled), &read))
            return fail(TransportError(GetLastError()));
        filled += read;
        const bool end = read == 0;
        if (filled < buf.size() && !end && GetTickCount64() - lastFlush < 500) continue;
        if (filled > 0) {
            DWORD written = 0;
            if (!WriteFile(tmp.h, buf.data(), static_cast<DWORD>(filled), &written, nullptr) || written != filled)
                return fail(writeFailed);
            sha.Update(buf.data(), filled);
            done += filled;
            filled = 0;
        }
        lastFlush = GetTickCount64();
        // More than announced: not the file we want (and it must never fill the disk).
        if ((expectedBytes && done > expectedBytes) || (length && done > length)) return fail(sizeWrong);
        if (progress && !progress(done, total)) return fail(Tr(L"Download cancelled."));
        if (end) break;
    }
    tmp.Close();

    if ((expectedBytes && done != expectedBytes) || (length && done != length)) return fail(sizeWrong);
    if (!expectedSha256Hex.empty()) {
        std::string want = expectedSha256Hex;
        for (char& c : want)
            if (c >= 'A' && c <= 'F') c = static_cast<char>(c - 'A' + 'a');
        if (sha.FinishHex() != want)
            return fail(Tr(L"The download is damaged (checksum does not match) – please try again."));
    }
    if (!MoveFileExW(tmp.path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return fail(TrF(L"Cannot replace {1} (error {2}) – is it in use?", {path, std::to_wstring(GetLastError())}));
    tmp.keep = true;  // it is the real file now
    return true;
}

}  // namespace gct
