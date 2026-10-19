// chat_source.hpp — what GW2Chatlator decides about every chat line another add-on ("Events: Chat") hands it, before
// anything of it is shown. That add-on is updated by Nexus whenever its author publishes – over night, unasked – so a
// changed layout must never crash the game and never show garbage. The copy itself is made in the add-on (behind an
// exception guard, the only code that touches the other add-on's memory); this part, portable and unit-tested, says:
//   - is a copied line plausible (hard rules: a failing line is never shown; notes: only written into the protocol),
//   - does a new version of the source pass its probation (5 good lines; one bad one: 9 of 10; two bad ones: it fails),
//   - when does reading pause for a version that passed (5 bad lines among the last 20).
// The protocol counts lengths and rules only – never a name, never chat text (it may end up in a public issue).
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace gct {

// One line as copied from the source. Strings are cut at their bound; `*Cut` says the source had no end within it.
struct ChatSourceLine {
    static constexpr size_t kNameMax = 127, kAccountMax = 127, kContentMax = 2047;
    int32_t type = 0;
    uint32_t flags = 0;
    uint64_t time = 0;   // FILETIME, as the source gave it
    bool read = false;   // its strings were read (a type we show); else only its head
    bool nameNull = true, accountNull = true, contentNull = true;
    bool nameBadPtr = false, accountBadPtr = false, contentBadPtr = false;  // no user-mode address: never touched
    bool nameCut = false, accountCut = false, contentCut = false;
    char name[kNameMax + 1] = {};
    char account[kAccountMax + 1] = {};
    char content[kContentMax + 1] = {};
};

// The source's message types (Events: Chat, public header): which ones carry a name and a text, which only a text.
enum ChatSourceType : int32_t {
    kSrcError = 0, kSrcGuild, kSrcGuildMotD, kSrcLocal, kSrcMap, kSrcParty, kSrcSquad, kSrcSquadMessage,
    kSrcSquadBroadcast, kSrcTeamPvP, kSrcTeamWvW, kSrcWhisper, kSrcEmote, kSrcEmoteCustom, kSrcTypeCount
};
bool ChatSourceTypeHasName(int32_t type);  // name + account + text (the generic message)
bool ChatSourceTypeTextOnly(int32_t type);  // only a text (a commander's squad message)

enum ChatRule : uint32_t {
    // Hard: what garbage does and the real source never does – the line is not shown, it counts as a failure.
    kRuleType = 1u << 0,        // a type outside the known ones
    kRulePointer = 1u << 1,     // a string pointer outside user memory
    kRuleUtf8 = 1u << 2,        // a string that is not UTF-8
    kRuleLength = 1u << 3,      // a string without an end within its bound
    kRuleControl = 1u << 4,     // control characters in a name
    kRuleNameLength = 1u << 5,  // a name longer than 40 characters
    kRuleUnreadable = 1u << 6,  // the memory could not be read at all (the guard caught it)
    // Notes: written into the protocol only.
    kNoteTime = 1u << 16,       // the time is empty or more than 36 h from now
    kNoteAccount = 1u << 17,    // the account name is not "Name.1234"
    kNoteFlags = 1u << 18,      // flags this type does not know
    kNoteEmpty = 1u << 19,      // no name or no text (the line is not shown)
    kNoteNotShown = 1u << 20,   // a type GW2Chatlator does not show (only its head was read)
};
constexpr uint32_t kHardRules = 0xFFFFu;

struct ChatLineVerdict {
    uint32_t rules = 0;
    bool Hard() const { return (rules & kHardRules) != 0; }
};

bool IsValidUtf8(const char* s);
ChatLineVerdict CheckChatLine(const ChatSourceLine& l, uint64_t nowFiletime);
std::string ChatRuleNames(uint32_t rules);  // "utf8, length" – for the protocol

// Probation of a new version: Feed every line that was read (a type not shown does not count).
class ChatSourceProbe {
public:
    enum class Result { Running, Passed, Failed };
    void Reset() { lines_ = fails_ = 0; }
    Result Feed(bool failed);
    int Lines() const { return lines_; }
    int Fails() const { return fails_; }
    static constexpr int kLines = 5;

private:
    int lines_ = 0, fails_ = 0;
};

// A version that passed: reading pauses when 5 of the last 20 lines failed (the source changed under us, e.g. a GW2
// patch made it hand over garbage).
class ChatSourceWatch {
public:
    bool Feed(bool failed);  // true: pause
    void Reset() { last_.clear(); }

private:
    std::deque<bool> last_;
};

// What the protocol keeps of a line: never its text, never a name.
struct ChatSourceRecord {
    int32_t type = 0;
    uint32_t flags = 0;
    int nameLen = -1, accountLen = -1, contentLen = -1;  // bytes; -1 = none
    uint32_t rules = 0;
};
ChatSourceRecord RecordOf(const ChatSourceLine& l, const ChatLineVerdict& v);

struct ChatSourceReport {
    std::string addonVersion, sourceVersion, verifiedVersion, state;
    std::vector<ChatSourceRecord> lines;
};
std::string ChatSourceProtocol(const ChatSourceReport& r);

}  // namespace gct
