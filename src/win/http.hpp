// http.hpp — small blocking HTTPS client on WinHTTP (call from worker threads).
#pragma once

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
                          const wchar_t* extraHeaderName = nullptr, size_t maxBytes = 16u << 20);

// Any http:// or https:// URL incl. port — e.g. a local LLM server
// (http://localhost:11434/v1/chat/completions). `receiveTimeoutMs` covers
// slow answers (local models loading).
HttpResponse HttpRequestUrl(const wchar_t* method, const std::wstring& url, const std::wstring& headers,
                            const std::string& body, int receiveTimeoutMs = 15000);

}  // namespace gct
