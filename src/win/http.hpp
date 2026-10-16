// http.hpp — small blocking HTTPS client on WinHTTP (call from worker threads).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace gct {

struct HttpResponse {
    bool transportOk = false;   // false: no HTTP status at all (DNS, TLS, timeout ...)
    unsigned long status = 0;   // HTTP status code
    std::string body;
    std::wstring extraHeader;   // value of the header asked for, if any
    std::wstring error;         // German text for transport errors
};

HttpResponse HttpsRequest(const wchar_t* method, const std::wstring& host, const std::wstring& pathAndQuery,
                          const std::wstring& headers, const std::string& body,
                          const wchar_t* extraHeaderName = nullptr, size_t maxBytes = 16u << 20,
                          int receiveTimeoutMs = 0);

// Any http:// or https:// URL incl. port — e.g. a local LLM server
// (http://localhost:11434/v1/chat/completions). `receiveTimeoutMs` covers
// slow answers (local models loading).
HttpResponse HttpRequestUrl(const wchar_t* method, const std::wstring& url, const std::wstring& headers,
                            const std::string& body, int receiveTimeoutMs = 15000, size_t maxBytes = 16u << 20);

// GET a large file straight to disk (any size): written to `path` + ".tmp", hashed while it arrives, renamed to
// `path` only when the status is 200, the size matches `expectedBytes` (0 = not checked) and the SHA-256 matches
// `expectedSha256Hex` (lower-case hex; empty = not checked). progress(done, total) runs on the calling thread
// (total 0 = unknown); returning false cancels. On failure the partial file is removed and `error` says why.
bool HttpDownloadToFile(const std::wstring& url, const std::wstring& path, uint64_t expectedBytes,
                        const std::string& expectedSha256Hex,
                        const std::function<bool(uint64_t done, uint64_t total)>& progress, std::wstring* error);

}  // namespace gct
