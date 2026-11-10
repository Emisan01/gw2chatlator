// whisper_peer.hpp — whom a whisper tab talks to: a player, not one of their characters. A whisper carries the
// account of who wrote it; once a tab knows it, the same player's lines land in it whichever character they are on.
// Memory only, for the session – never saved (AGENTS.md, privacy). Portable, unit-tested.
#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace gct {

struct WhisperPeer {
    std::wstring account;             // case-folded; "" until a whisper of theirs told it
    std::vector<std::wstring> names;  // their characters of this session, case-folded

    bool Has(const std::wstring& name) const { return std::find(names.begin(), names.end(), name) != names.end(); }
    void Add(const std::wstring& name) {
        if (!name.empty() && !Has(name)) names.push_back(name);
    }
    // A whisper line is theirs: one of their characters wrote or got it, or it carries their account.
    bool Shows(const std::wstring& speaker, const std::wstring& lineAccount) const {
        return Has(speaker) || (!account.empty() && lineAccount == account);
    }
};

// A whisper came in from `name` of `account`. `keep`: the tab that is this player's (it knows the account and the name
// now); `drop`: a second tab of the same player, opened under another character before the account was known – its
// names went over to `keep`, the caller closes it. Of two such tabs the one in front stays (what you write is in it).
// No tab of this player: both -1, nothing changes. A null entry is a tab that is no whisper tab.
struct WhisperBinding {
    int keep = -1, drop = -1;
};
inline WhisperBinding BindWhisper(const std::vector<WhisperPeer*>& tabs, int front, const std::wstring& name,
                                  const std::wstring& account) {
    WhisperBinding b;
    if (account.empty() || name.empty()) return b;
    int known = -1, named = -1;
    for (size_t i = 0; i < tabs.size(); ++i) {
        if (!tabs[i]) continue;
        if (known < 0 && tabs[i]->account == account) known = static_cast<int>(i);
        else if (named < 0 && tabs[i]->account.empty() && tabs[i]->Has(name)) named = static_cast<int>(i);
    }
    if (known < 0 && named < 0) return b;
    if (known >= 0 && named >= 0) {
        b.keep = named == front ? named : known;
        b.drop = named == front ? known : named;
        for (const std::wstring& n : tabs[b.drop]->names) tabs[b.keep]->Add(n);
    } else {
        b.keep = known >= 0 ? known : named;
    }
    tabs[b.keep]->account = account;
    tabs[b.keep]->Add(name);
    return b;
}

}  // namespace gct
