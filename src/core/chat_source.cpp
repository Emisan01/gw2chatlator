// chat_source.cpp — see chat_source.hpp.

#include "core/chat_source.hpp"

#include <cstdio>
#include <cstring>

namespace gct {

bool ChatSourceTypeHasName(int32_t type) {
    switch (type) {
        case kSrcGuild: case kSrcLocal: case kSrcMap: case kSrcParty: case kSrcSquad:
        case kSrcTeamPvP: case kSrcTeamWvW: case kSrcWhisper: return true;
        default: return false;
    }
}

bool ChatSourceTypeTextOnly(int32_t type) {
    return type == kSrcSquadMessage || type == kSrcSquadBroadcast;
}

bool IsValidUtf8(const char* s) {  // strict: no overlong forms, no surrogates, nothing above U+10FFFF
    const auto* p = reinterpret_cast<const unsigned char*>(s);
    while (*p) {
        const unsigned c = *p;
        int n;
        uint32_t cp;
        if (c < 0x80) { ++p; continue; }
        if ((c & 0xE0) == 0xC0) { n = 1; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { n = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { n = 3; cp = c & 0x07; }
        else return false;
        for (int i = 1; i <= n; ++i) {
            if ((p[i] & 0xC0) != 0x80) return false;  // also stops at the terminating 0
            cp = (cp << 6) | (p[i] & 0x3F);
        }
        if ((n == 1 && cp < 0x80) || (n == 2 && cp < 0x800) || (n == 3 && cp < 0x10000)) return false;
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        p += n + 1;
    }
    return true;
}

namespace {

size_t CodePoints(const char* s) {
    size_t n = 0;
    for (const auto* p = reinterpret_cast<const unsigned char*>(s); *p; ++p)
        if ((*p & 0xC0) != 0x80) ++n;
    return n;
}

bool HasControl(const char* s) {
    for (const auto* p = reinterpret_cast<const unsigned char*>(s); *p; ++p)
        if (*p < 0x20 || *p == 0x7F) return true;
    return false;
}

bool LooksLikeAccount(const char* s) {  // "Name.1234" (a leading ':' as GW2 writes it is fine)
    const char* dot = std::strrchr(s, '.');
    if (!dot || dot - s < 3) return false;
    for (int i = 1; i <= 4; ++i)
        if (dot[i] < '0' || dot[i] > '9') return false;
    return dot[5] == 0;
}

uint32_t KnownFlags(int32_t type) {  // MetadataFlags of the public header, per type
    switch (type) {
        case kSrcGuild: return 1u << 0;
        case kSrcLocal: case kSrcMap: return 1u << 2;
        case kSrcSquad: case kSrcParty: return 1u << 1;
        case kSrcSquadMessage: case kSrcSquadBroadcast: return 1u << 3;
        case kSrcWhisper: return 1u << 4;
        default: return 0;
    }
}

void CheckString(const char* s, bool isNull, bool badPtr, bool cut, bool isName, uint32_t& rules) {
    if (badPtr) {
        rules |= kRulePointer;
        return;
    }
    if (isNull) return;
    if (cut) rules |= kRuleLength;
    if (!IsValidUtf8(s)) rules |= kRuleUtf8;
    if (isName && HasControl(s)) rules |= kRuleControl;
    if (isName && CodePoints(s) > 40) rules |= kRuleNameLength;
}

}  // namespace

ChatLineVerdict CheckChatLine(const ChatSourceLine& l, uint64_t now) {
    ChatLineVerdict v;
    if (l.type < 0 || l.type >= kSrcTypeCount) {
        v.rules |= kRuleType;
        return v;
    }
    constexpr uint64_t k36h = 36ull * 3600ull * 10000000ull;  // FILETIME counts 100 ns
    if (l.time == 0 || (l.time > now ? l.time - now : now - l.time) > k36h) v.rules |= kNoteTime;
    if (l.flags & ~KnownFlags(l.type)) v.rules |= kNoteFlags;
    if (!l.read) {
        v.rules |= kNoteNotShown;
        return v;
    }
    CheckString(l.content, l.contentNull, l.contentBadPtr, l.contentCut, false, v.rules);
    if (ChatSourceTypeHasName(l.type)) {
        CheckString(l.name, l.nameNull, l.nameBadPtr, l.nameCut, true, v.rules);
        CheckString(l.account, l.accountNull, l.accountBadPtr, l.accountCut, true, v.rules);
        if (!l.accountNull && !l.accountBadPtr && !l.accountCut && !LooksLikeAccount(l.account)) v.rules |= kNoteAccount;
        if (l.nameNull || (!l.nameBadPtr && !l.name[0])) v.rules |= kNoteEmpty;
    }
    if (l.contentNull || (!l.contentBadPtr && !l.content[0])) v.rules |= kNoteEmpty;
    return v;
}

std::string ChatRuleNames(uint32_t rules) {
    static const struct { uint32_t bit; const char* name; } kNames[] = {
        {kRuleType, "type"}, {kRulePointer, "pointer"}, {kRuleUtf8, "utf8"}, {kRuleLength, "length"},
        {kRuleControl, "control"}, {kRuleNameLength, "name-length"}, {kRuleUnreadable, "unreadable"},
        {kNoteTime, "(time)"}, {kNoteAccount, "(account)"}, {kNoteFlags, "(flags)"}, {kNoteEmpty, "(empty)"},
        {kNoteNotShown, "(not shown)"},
    };
    std::string out;
    for (const auto& n : kNames)
        if (rules & n.bit) out += (out.empty() ? "" : ", ") + std::string(n.name);
    return out.empty() ? "ok" : out;
}

ChatSourceProbe::Result ChatSourceProbe::Feed(bool failed) {
    ++lines_;
    if (failed) ++fails_;
    if (fails_ >= 2) return Result::Failed;
    if (fails_ == 0 && lines_ >= kLines) return Result::Passed;
    if (lines_ >= 2 * kLines) return Result::Passed;  // one bad line among ten
    return Result::Running;
}

bool ChatSourceWatch::Feed(bool failed) {
    last_.push_back(failed);
    if (last_.size() > 20) last_.pop_front();
    int bad = 0;
    for (bool b : last_) bad += b ? 1 : 0;
    return bad >= 5;
}

ChatSourceRecord RecordOf(const ChatSourceLine& l, const ChatLineVerdict& v) {
    ChatSourceRecord r;
    r.type = l.type;
    r.flags = l.flags;
    r.rules = v.rules;
    if (l.read) {
        auto len = [](const char* s, bool isNull, bool bad) { return isNull || bad ? -1 : static_cast<int>(std::strlen(s)); };
        r.nameLen = len(l.name, l.nameNull, l.nameBadPtr);
        r.accountLen = len(l.account, l.accountNull, l.accountBadPtr);
        r.contentLen = len(l.content, l.contentNull, l.contentBadPtr);
    }
    return r;
}

std::string ChatSourceProtocol(const ChatSourceReport& r) {
    std::string out = "GW2Chatlator " + r.addonVersion + " - chat source check\n";
    out += "Events: Chat " + (r.sourceVersion.empty() ? std::string("?") : r.sourceVersion);
    out += " (last checked: " + (r.verifiedVersion.empty() ? std::string("none") : r.verifiedVersion) + ")\n";
    out += "State: " + r.state + "\n";
    out += "Lines (newest last; lengths in bytes, no text, no names):\n";
    if (r.lines.empty()) out += "  none yet\n";
    for (const ChatSourceRecord& l : r.lines) {
        char head[96];
        std::snprintf(head, sizeof(head), "  type %d flags 0x%X name %d account %d text %d: ", static_cast<int>(l.type),
                      static_cast<unsigned>(l.flags), l.nameLen, l.accountLen, l.contentLen);
        out += head + ChatRuleNames(l.rules) + "\n";
    }
    return out;
}

}  // namespace gct
