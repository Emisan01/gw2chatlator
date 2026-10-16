// link_filter.hpp — uBlock-style local link safety analysis and scam/phishing filtering.
// Portable, no Win32.
#pragma once

#include <string>

namespace gct {

enum class LinkSafety {
    Safe,        // Verified official GW2, community resource, or trusted platform
    Neutral,     // Unclassified web URL (normal browser prompt)
    Suspicious,  // URL shorteners (obscured target)
    Dangerous,   // Phishing / GW2 impersonation, raw IP literal, or known malicious host
};

struct LinkSafetyInfo {
    LinkSafety safety = LinkSafety::Neutral;
    std::string host;
    std::string reason;        // Internal tag: "official", "community", "trusted_platform", "shortener", "gw2_impersonation", "ip_literal", "known_malware"
    std::wstring label;        // Short UI badge / label: L"[Safe]", L"[Shortener]", L"[Phishing]", etc.
    std::wstring description;  // Explanatory warning / details for confirmation dialog
};

// Extracts the lowercase hostname from any URL or domain string.
// Removes protocol ("http://", "https://"), port numbers, user info, path, query, and trailing dots.
std::string ExtractHost(const std::string& url_or_domain);

// Returns true if host is an exact match or a subdomain of parentDomain.
bool IsDomainOrSubdomain(const std::string& host, const std::string& parentDomain);

// Returns true if host is a raw IPv4 or bracketed IPv6 address literal.
bool IsIpLiteral(const std::string& host);

// Evaluates the safety of a URL or domain using uBlock-style local heuristics and curated domain sets.
LinkSafetyInfo AssessLinkSafety(const std::string& url_or_domain);

}  // namespace gct
