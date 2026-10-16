// link_filter.cpp — uBlock-style local link safety analysis and scam/phishing filtering.
#include "core/link_filter.hpp"

#include <cctype>
#include <string>

#include "core/i18n.hpp"

namespace gct {

std::string ExtractHost(const std::string& url_or_domain) {
    std::string s = url_or_domain;
    size_t start = 0;
    while (start < s.size() && static_cast<unsigned char>(s[start]) <= 32) ++start;
    size_t end = s.size();
    while (end > start && static_cast<unsigned char>(s[end - 1]) <= 32) --end;
    if (start >= end) return {};
    s = s.substr(start, end - start);

    // Protocol ("http://", "https://", etc.)
    const size_t proto = s.find("://");
    if (proto != std::string::npos) {
        s = s.substr(proto + 3);
    }

    // Path, query or fragment
    const size_t slash = s.find_first_of("/?#");
    if (slash != std::string::npos) {
        s = s.substr(0, slash);
    }

    // Userinfo (user:pass@host)
    const size_t at = s.find('@');
    if (at != std::string::npos) {
        s = s.substr(at + 1);
    }

    // Port (host:port or [::1]:port)
    if (!s.empty() && s[0] == '[') {
        const size_t close_bracket = s.find(']');
        if (close_bracket != std::string::npos) {
            s = s.substr(0, close_bracket + 1);
        }
    } else {
        const size_t colon = s.find(':');
        if (colon != std::string::npos) {
            s = s.substr(0, colon);
        }
    }

    // Lowercase ASCII
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }

    // Trailing dot
    while (!s.empty() && s.back() == '.') {
        s.pop_back();
    }

    return s;
}

bool IsDomainOrSubdomain(const std::string& host, const std::string& parentDomain) {
    if (host == parentDomain) return true;
    if (host.size() > parentDomain.size() + 1) {
        const size_t pos = host.size() - parentDomain.size();
        if (host[pos - 1] == '.' && host.compare(pos, parentDomain.size(), parentDomain) == 0) {
            return true;
        }
    }
    return false;
}

bool IsIpLiteral(const std::string& host) {
    if (host.empty()) return false;
    if (host.front() == '[' && host.back() == ']') return true;

    int dots = 0;
    int num = 0;
    bool hasDigits = false;
    for (char c : host) {
        if (c >= '0' && c <= '9') {
            num = num * 10 + (c - '0');
            if (num > 255) return false;
            hasDigits = true;
        } else if (c == '.') {
            if (!hasDigits) return false;
            hasDigits = false;
            num = 0;
            ++dots;
        } else {
            return false;
        }
    }
    return dots == 3 && hasDigits;
}

LinkSafetyInfo AssessLinkSafety(const std::string& url_or_domain) {
    const std::string host = ExtractHost(url_or_domain);
    if (host.empty()) {
        return {LinkSafety::Neutral, "", "empty", L"", L""};
    }

    // 1. Raw IP literals (e.g. http://192.168.1.1/...)
    if (IsIpLiteral(host)) {
        return {
            LinkSafety::Dangerous,
            host,
            "ip_literal",
            Tr(L"[Suspicious IP]"),
            Tr(L"WARNING: Raw IP addresses in game chat are often malicious.")
        };
    }

    // 2. Official Guild Wars 2 / ArenaNet / NCSoft
    static const char* const kOfficial[] = {
        "guildwars2.com",
        "arena.net",
        "ncsoft.com"
    };
    for (const char* off : kOfficial) {
        if (IsDomainOrSubdomain(host, off)) {
            return {
                LinkSafety::Safe,
                host,
                "official",
                Tr(L"[Safe]"),
                Tr(L"Verified official Guild Wars 2 website.")
            };
        }
    }

    // 3. GW2 / ArenaNet Impersonation & Phishing:
    // If domain contains these brand names, but is not on official servers (checked above),
    // it is almost certainly phishing (e.g. guildwars2-freegems.com, arenanet-auth.xyz).
    static const char* const kImpersonationKeywords[] = {
        "guildwars2",
        "guildwars",
        "arenanet",
        "ncsoft",
        "gemstore",
        "gw2gems",
        "gw2-gem",
        "gw2login",
        "arenanett"
    };
    for (const char* kw : kImpersonationKeywords) {
        if (host.find(kw) != std::string::npos) {
            return {
                LinkSafety::Dangerous,
                host,
                "gw2_impersonation",
                Tr(L"[Phishing]"),
                Tr(L"WARNING: This link appears to imitate Guild Wars 2 / ArenaNet!")
            };
        }
    }

    // 4. Recognized Community Sites
    static const char* const kCommunity[] = {
        "gw2efficiency.com",
        "snowcrows.com",
        "hardstuck.gg",
        "discretize.eu",
        "fast.farm",
        "gw2skills.net",
        "gw2tp.com",
        "gw2spidy.com",
        "gw2crafts.net",
        "gw2wingman.never-know.ch",
        "blishhud.com",
        "raidcore.gg",
        "gw2armory.com",
        "gw2bltc.com",
        "gw2lunchbox.com",
        "pecl.org"
    };
    for (const char* comm : kCommunity) {
        if (IsDomainOrSubdomain(host, comm)) {
            return {
                LinkSafety::Safe,
                host,
                "community",
                Tr(L"[Safe]"),
                Tr(L"Recognized Guild Wars 2 community website.")
            };
        }
    }

    // 5. Common Trusted Platforms
    static const char* const kPlatforms[] = {
        "youtube.com",
        "youtu.be",
        "twitch.tv",
        "discord.gg",
        "discord.com",
        "reddit.com",
        "github.com",
        "wikipedia.org",
        "imgur.com",
        "twitter.com",
        "x.com"
    };
    for (const char* plat : kPlatforms) {
        if (IsDomainOrSubdomain(host, plat)) {
            return {
                LinkSafety::Safe,
                host,
                "trusted_platform",
                Tr(L"[Safe]"),
                Tr(L"Trusted external platform.")
            };
        }
    }

    // 6. URL Shorteners
    static const char* const kShorteners[] = {
        "bit.ly",
        "tinyurl.com",
        "is.gd",
        "cutt.ly",
        "t.co",
        "rb.gy",
        "shorturl.at",
        "goo.gl",
        "ow.ly",
        "buff.ly",
        "rebrand.ly",
        "shorte.st",
        "adf.ly",
        "bc.vc",
        "v.gd",
        "clck.ru",
        "rotf.lol",
        "s.id",
        "linktr.ee"
    };
    for (const char* sh : kShorteners) {
        if (IsDomainOrSubdomain(host, sh)) {
            return {
                LinkSafety::Suspicious,
                host,
                "shortener",
                Tr(L"[Shortener]"),
                Tr(L"URL shortener: The actual target address is concealed.")
            };
        }
    }

    // 7. Neutral (Unclassified regular web links)
    return {
        LinkSafety::Neutral,
        host,
        "unclassified",
        L"",
        Tr(L"Links in the chat come from other players – open only what you trust.")
    };
}

}  // namespace gct
