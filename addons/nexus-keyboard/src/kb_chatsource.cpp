// kb_chatsource.cpp — the guard between "Events: Chat" and us. That add-on is updated by Nexus whenever its author
// publishes (over night, unasked); if its layout changed, reading it the old way could take the game down. Four layers:
//   1. A shield: the only code that touches the other add-on's memory copies a line into our own buffer, pointers checked,
//      strings bounded, behind SEH. Unreadable memory stops reading for the session – the game and writing go on.
//   2. Every copied line is checked (core/chat_source): a line that fails a hard rule is never shown.
//   3. A version guard: a new version of Events: Chat is on probation (5 good lines). Before its first line is copied a
//      marker file names it; if the game still ended during the probation, the next start keeps reading off for it.
//      A version that passed is watched: 5 bad lines among the last 20 pause reading for the session.
//   4. By hand: Options → Modules – reading on / off (kept), "Read anyway", "Check again", "Copy protocol".
// Nothing here sends anything, downloads anything or stores chat: the protocol counts lengths, never text or names.

#include "kb_shared.hpp"
#include "core/chat_source.hpp"

namespace kb {

namespace {

bool UserPtr(const void* p) {  // an address user code can own (x64); anything else is never read
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= 0x10000 && v < 0x00007FFFFFFF0000ull;
}

void CopyString(const char* src, char* dst, size_t max, bool* isNull, bool* badPtr, bool* cut) {
    *isNull = src == nullptr;
    *badPtr = false;
    *cut = false;
    dst[0] = 0;
    if (!src) return;
    if (!UserPtr(src)) {
        *badPtr = true;
        return;
    }
    size_t i = 0;
    for (; i < max && src[i]; ++i) dst[i] = src[i];  // never a byte past the bound
    dst[i] = 0;
    *cut = i == max;
}

// Layer 1: plain copies behind SEH (nothing here needs unwinding). 1 = copied, 0 = no line, -1 = not readable.
int CopyLineGuarded(const void* raw, gct::ChatSourceLine* out) {
    __try {
        if (!raw) return 0;
        if (!UserPtr(raw)) return -1;
        const auto* m = static_cast<const bridge::ChatEventsMessage*>(raw);
        out->time = (static_cast<uint64_t>(m->timeHigh) << 32) | m->timeLow;
        out->type = static_cast<int32_t>(m->type);
        out->flags = m->flags;
        if (gct::ChatSourceTypeHasName(out->type)) {
            out->read = true;
            CopyString(m->generic.characterName, out->name, gct::ChatSourceLine::kNameMax, &out->nameNull,
                       &out->nameBadPtr, &out->nameCut);
            CopyString(m->generic.accountName, out->account, gct::ChatSourceLine::kAccountMax, &out->accountNull,
                       &out->accountBadPtr, &out->accountCut);
            CopyString(m->generic.content, out->content, gct::ChatSourceLine::kContentMax, &out->contentNull,
                       &out->contentBadPtr, &out->contentCut);
        } else if (gct::ChatSourceTypeTextOnly(out->type)) {
            out->read = true;
            CopyString(m->squadMessage, out->content, gct::ChatSourceLine::kContentMax, &out->contentNull,
                       &out->contentBadPtr, &out->contentCut);
        }
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

enum Pause { kPauseNone = 0, kPauseFailed = 1, kPauseCrashed = 2, kPauseUnreadable = 3, kPauseDrift = 4 };

struct SourceState {
    std::mutex mu;  // never held while g_mu is taken (g_mu first, then this)
    bool reading = true;       // [ChatSource] Reading – the switch in the options
    std::wstring version;      // the loaded Events: Chat ("" = not found)
    std::wstring verified;     // [ChatSource] VerifiedVersion: passed its probation (or you said "Read anyway")
    std::wstring bad;          // [ChatSource] BadVersion: failed (or the game ended during its probation)
    int badReason = kPauseFailed;  // [ChatSource] BadReason
    std::wstring probing;      // on probation now (the marker file names it)
    std::wstring forced;       // "Read anyway" for this version, this session: the watch does not pause it
    std::wstring paused;       // paused for this version, this session (unreadable, or lines stopped fitting)
    int pauseReason = kPauseNone;
    bool announced = false;    // the pause was said once in the window (also in the mini mode)
    gct::ChatSourceProbe probe;
    gct::ChatSourceWatch watch;
    std::deque<gct::ChatSourceRecord> recent;  // the last 20 lines: types, lengths, rules – never text
    std::wstring markerPath;
    int shown = 0, dropped = 0;
};
SourceState g_src;

struct Save {  // what goes into the ini – written after the source's lock is let go, under g_mu
    const wchar_t* key;
    std::wstring value;
};

void Persist(const std::vector<Save>& saves, bool muHeld) {
    if (saves.empty() || g_wiped) return;
    std::unique_lock<std::mutex> lock(g_mu, std::defer_lock);
    if (!muHeld) lock.lock();
    for (const Save& s : saves) g_cfg.SaveValue(L"ChatSource", s.key, s.value);
}

void WriteMarker(const std::wstring& path, const std::wstring& version) {
    if (!g_wiped && !path.empty()) WriteFileAtomic(path, U8(version));
}
void DeleteMarker(const std::wstring& path) {
    if (!path.empty()) DeleteFileW(path.c_str());
}

uint64_t NowFiletime() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

std::wstring Ver(const SourceState& s) { return s.version.empty() ? L"?" : s.version; }

std::wstring PauseText(int reason, const std::wstring& ver) {
    switch (reason) {
        case kPauseCrashed:
            return TrF(L"The game ended while Events: Chat {1} was being checked – reading stays off for this version.", {ver});
        case kPauseUnreadable:
            return TrF(L"Events: Chat {1} handed over unreadable data – reading paused for this session.", {ver});
        case kPauseDrift:
            return TrF(L"Lines from Events: Chat {1} stopped fitting – reading paused for this session.", {ver});
        default:
            return TrF(L"Events: Chat {1} does not fit – reading paused, writing works.", {ver});
    }
}

int CurrentPause(const SourceState& s) {  // why reading is paused for the loaded version (0: it is not)
    const std::wstring ver = Ver(s);
    if (s.forced == ver) return kPauseNone;
    if (s.paused == ver && s.pauseReason != kPauseNone) return s.pauseReason;
    if (!s.bad.empty() && s.bad == ver) return s.badReason;
    return kPauseNone;
}

void Dispatch(const gct::ChatSourceLine& l) {
    Channel ch;
    switch (l.type) {
        case gct::kSrcParty: ch = Channel::Party; break;
        case gct::kSrcSquad: ch = Channel::Squad; break;
        case gct::kSrcSquadMessage:
        case gct::kSrcSquadBroadcast: AddLine(Channel::Squad, false, "", l.content); return;
        case gct::kSrcMap: ch = Channel::Map; break;
        case gct::kSrcLocal: ch = Channel::Say; break;
        case gct::kSrcWhisper: ch = Channel::Whisper; break;
        case gct::kSrcGuild: ch = Channel::Guild; break;
        case gct::kSrcTeamPvP:
        case gct::kSrcTeamWvW: ch = Channel::Team; break;
        default: return;
    }
    AddLine(ch, l.type == gct::kSrcWhisper && (l.flags & bridge::kChatEventsWhisperFromMe) != 0,
            l.nameNull ? "" : l.name, l.content);
}

}  // namespace

// Calls another add-on's GetAddonDef behind SEH (they run foreign code): its signature and version, false if it failed.
bool ReadAddonDefGuarded(void* proc, uint32_t* signature, AddonVersion_t* version) {
    __try {
        using GetDef = AddonDefinition_t* (*)();
        const AddonDefinition_t* d = reinterpret_cast<GetDef>(proc)();
        if (!d || !UserPtr(d)) return false;
        *signature = d->Signature;
        *version = d->Version;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void ChatSourceLoad() {  // g_mu held, after the ini was read
    std::lock_guard<std::mutex> lock(g_src.mu);
    const std::wstring& ini = g_cfg.iniPath;
    wchar_t buf[64] = {};
    g_src.reading = GetPrivateProfileIntW(L"ChatSource", L"Reading", 1, ini.c_str()) != 0;
    GetPrivateProfileStringW(L"ChatSource", L"VerifiedVersion", L"", buf, 64, ini.c_str());
    g_src.verified = buf;
    GetPrivateProfileStringW(L"ChatSource", L"BadVersion", L"", buf, 64, ini.c_str());
    g_src.bad = buf;
    g_src.badReason = GetPrivateProfileIntW(L"ChatSource", L"BadReason", kPauseFailed, ini.c_str());
    if (g_src.badReason != kPauseCrashed) g_src.badReason = kPauseFailed;
    g_src.markerPath = g_cfg.dataDir + L"\\chat-source-check.txt";
    std::string marked;
    if (ReadFileBytes(g_src.markerPath, marked)) {  // the last session ended during a probation: no Unload ran
        std::wstring v = W(marked);
        v.erase(std::remove_if(v.begin(), v.end(), [](wchar_t c) { return c < 0x20; }), v.end());
        if (!v.empty() && v.size() < 40) {
            g_src.bad = v;
            g_src.badReason = kPauseCrashed;
            g_cfg.SaveValue(L"ChatSource", L"BadVersion", v);
            g_cfg.SaveValue(L"ChatSource", L"BadReason", std::to_wstring(kPauseCrashed));
        }
        DeleteMarker(g_src.markerPath);
    }
    g_src.probing.clear();
    g_src.forced.clear();
    g_src.paused.clear();
    g_src.pauseReason = kPauseNone;
    g_src.announced = false;
    g_src.probe.Reset();
    g_src.watch.Reset();
    g_src.recent.clear();
    g_src.shown = g_src.dropped = 0;
}

void ChatSourceUnload() {  // the add-on goes cleanly: a probation still running is no crash
    std::lock_guard<std::mutex> lock(g_src.mu);
    if (!g_src.probing.empty()) DeleteMarker(g_src.markerPath);
    g_src.probing.clear();
}

void ChatSourceSetVersion(const std::wstring& version) {  // from the module scan (every 3 s)
    std::lock_guard<std::mutex> lock(g_src.mu);
    if (version == g_src.version) return;
    g_src.version = version;
    g_src.announced = false;
    g_src.watch.Reset();
}

void OnChatEvents(void* args) {
    if (g_disabled || g_wiped) return;
    g_chatEventsLive = true;
    enum class Mode { Drop, Verified, Probation, Forced };
    Mode mode = Mode::Drop;
    std::wstring ver, marker, status;
    bool startMark = false;
    {
        std::lock_guard<std::mutex> lock(g_src.mu);
        ver = Ver(g_src);
        marker = g_src.markerPath;
        const int pause = CurrentPause(g_src);
        if (!g_src.reading) {
            ++g_src.dropped;
            return;  // switched off: not a byte of the line is touched
        }
        if (pause != kPauseNone) {
            ++g_src.dropped;
            if (!g_src.announced) {
                g_src.announced = true;
                status = PauseText(pause, ver);
            }
        } else if (g_src.forced == ver) {
            mode = Mode::Forced;
        } else if (!g_src.verified.empty() && g_src.verified == ver) {
            mode = Mode::Verified;
        } else {
            mode = Mode::Probation;
            if (g_src.probing != ver) {
                g_src.probing = ver;
                g_src.probe.Reset();
                startMark = true;
            }
        }
    }
    if (mode == Mode::Drop) {
        if (!status.empty()) {
            std::lock_guard<std::mutex> lock(g_mu);
            SetStatus(status, 15000);
        }
        return;
    }
    // Before the first line of a new version is copied: if anything still took the game down, the next start knows.
    if (startMark) WriteMarker(marker, ver);
    gct::ChatSourceLine line;
    const int got = CopyLineGuarded(args, &line);
    if (got == 0) return;
    gct::ChatLineVerdict v;
    if (got < 0) v.rules = gct::kRuleUnreadable;
    else v = gct::CheckChatLine(line, NowFiletime());
    const bool show = got > 0 && line.read && !v.Hard();
    std::vector<Save> saves;
    {
        std::lock_guard<std::mutex> lock(g_src.mu);
        g_src.recent.push_back(gct::RecordOf(line, v));
        if (g_src.recent.size() > 20) g_src.recent.pop_front();
        if (got > 0 && !line.read && !v.Hard()) {
            // a known type we do not show: only its head was read – it neither passes nor fails anything (an unknown
            // type is not that: it fails)
        } else if (mode == Mode::Probation && g_src.probing == ver) {
            const auto r = got < 0 ? gct::ChatSourceProbe::Result::Failed : g_src.probe.Feed(v.Hard());
            if (r == gct::ChatSourceProbe::Result::Passed) {
                const bool update = !g_src.verified.empty();
                g_src.verified = ver;
                g_src.probing.clear();
                DeleteMarker(marker);
                saves.push_back({L"VerifiedVersion", ver});
                if (update) status = TrF(L"Events: Chat {1} checked – the chat is read.", {ver});
            } else if (r == gct::ChatSourceProbe::Result::Failed) {
                g_src.bad = ver;
                g_src.badReason = kPauseFailed;
                g_src.probing.clear();
                g_src.announced = true;
                DeleteMarker(marker);
                saves.push_back({L"BadVersion", ver});
                saves.push_back({L"BadReason", std::to_wstring(kPauseFailed)});
                status = PauseText(got < 0 ? kPauseUnreadable : kPauseFailed, ver);
            }
        } else if (got < 0) {  // unreadable memory always stops reading (also after "Read anyway")
            g_src.paused = ver;
            g_src.pauseReason = kPauseUnreadable;
            g_src.forced.clear();
            g_src.announced = true;
            status = PauseText(kPauseUnreadable, ver);
        } else if (mode == Mode::Verified && g_src.watch.Feed(v.Hard())) {
            g_src.paused = ver;
            g_src.pauseReason = kPauseDrift;
            g_src.announced = true;
            status = PauseText(kPauseDrift, ver);
        }
        if (show) ++g_src.shown;
        else ++g_src.dropped;
    }
    Persist(saves, false);
    if (!status.empty()) {
        std::lock_guard<std::mutex> lock(g_mu);
        SetStatus(status, 15000);
    }
    if (show) Dispatch(line);
}

// Layer 4, by hand.
void ChatSourceSetReading(bool on, bool muHeld) {
    {
        std::lock_guard<std::mutex> lock(g_src.mu);
        g_src.reading = on;
        g_src.announced = false;
    }
    Persist({{L"Reading", on ? L"1" : L"0"}}, muHeld);
}

void ChatSourceReadAnyway(bool muHeld) {  // this version is read: no probation, the watch does not pause it
    std::vector<Save> saves;
    {
        std::lock_guard<std::mutex> lock(g_src.mu);
        const std::wstring ver = Ver(g_src);
        g_src.forced = ver;
        if (g_src.paused == ver) g_src.paused.clear();
        g_src.pauseReason = kPauseNone;
        if (g_src.bad == ver) {
            g_src.bad.clear();
            saves.push_back({L"BadVersion", L""});
        }
        if (!g_src.probing.empty()) DeleteMarker(g_src.markerPath);
        g_src.probing.clear();
        g_src.verified = ver;
        saves.push_back({L"VerifiedVersion", ver});
        g_src.watch.Reset();
        g_src.announced = false;
    }
    Persist(saves, muHeld);
}

void ChatSourceCheckAgain(bool muHeld) {  // a new probation (a version that passed: reading resumes, watched)
    std::vector<Save> saves;
    {
        std::lock_guard<std::mutex> lock(g_src.mu);
        const std::wstring ver = Ver(g_src);
        const bool passedBefore = !g_src.verified.empty() && g_src.verified == ver;
        if (g_src.forced == ver) g_src.forced.clear();
        if (g_src.paused == ver) g_src.paused.clear();
        g_src.pauseReason = kPauseNone;
        if (g_src.bad == ver) {
            g_src.bad.clear();
            saves.push_back({L"BadVersion", L""});
        }
        if (!passedBefore) {  // the next line starts its probation (passed before: watched again from a clean start)
            if (!g_src.probing.empty()) DeleteMarker(g_src.markerPath);
            g_src.probing.clear();
        }
        g_src.watch.Reset();
        g_src.probe.Reset();
        g_src.announced = false;
    }
    Persist(saves, muHeld);
}

std::string ChatSourceProtocolText() {
    std::lock_guard<std::mutex> lock(g_src.mu);
    gct::ChatSourceReport r;
    r.addonVersion = std::to_string(GCT_VER_MAJOR) + "." + std::to_string(GCT_VER_MINOR) + "." + std::to_string(GCT_VER_PATCH);
    r.sourceVersion = U8(g_src.version);
    r.verifiedVersion = U8(g_src.verified);
    const int pause = CurrentPause(g_src);
    const std::wstring ver = Ver(g_src);
    r.state = !g_src.reading            ? "switched off by hand"
              : pause == kPauseFailed     ? "paused: failed its check"
              : pause == kPauseCrashed    ? "off: the game ended during its check"
              : pause == kPauseUnreadable ? "paused: unreadable data"
              : pause == kPauseDrift      ? "paused: lines stopped fitting"
              : g_src.forced == ver       ? "read anyway (by hand)"
              : g_src.probing == ver      ? "checking: " + std::to_string(g_src.probe.Lines()) + " lines, " +
                                                std::to_string(g_src.probe.Fails()) + " failed"
              : g_src.verified == ver     ? "reading (checked)"
                                          : "new, no line yet";
    r.state += " - shown " + std::to_string(g_src.shown) + ", dropped " + std::to_string(g_src.dropped);
    r.lines.assign(g_src.recent.begin(), g_src.recent.end());
    return gct::ChatSourceProtocol(r);
}

namespace {

// The state in one line for the options and the chat area. level: 2 fine, 1 busy, 0 a problem.
std::wstring StateText(int& level, int& pause) {
    std::lock_guard<std::mutex> lock(g_src.mu);
    const std::wstring ver = Ver(g_src);
    pause = CurrentPause(g_src);
    if (!g_src.reading) {
        level = 0;
        return Tr(L"off – GW2Chatlator does not touch what Events: Chat sends");
    }
    if (pause != kPauseNone) {
        level = 0;
        return PauseText(pause, ver);
    }
    if (g_src.forced == ver) {
        level = 1;
        return TrF(L"reading Events: Chat {1} without the check", {ver});
    }
    if (g_src.probing == ver) {
        level = 1;
        return TrF(L"checking Events: Chat {1} – {2} of 5 lines", {ver, std::to_wstring(g_src.probe.Lines())});
    }
    if (!g_src.verified.empty() && g_src.verified == ver) {
        level = 2;
        return TrF(L"reading – Events: Chat {1} checked", {ver});
    }
    level = 1;
    return TrF(L"Events: Chat {1} is new – checked with the first chat lines", {ver});
}

}  // namespace

void RenderChatSourceRow() {  // the options (g_mu NOT held): the state and the buttons
    int level = 0, pause = kPauseNone;
    const std::wstring text = StateText(level, pause);
    bool reading;
    {
        std::lock_guard<std::mutex> lock(g_src.mu);
        reading = g_src.reading;
    }
    ImGui::PushID("chatsource");
    StatusRow(UI(Tr(L"Reading")), level, UI(text));
    if (ImGui::Checkbox(UI(Tr(L"Read the chat")).c_str(), &reading)) ChatSourceSetReading(reading, false);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", UI(Tr(L"Off: GW2Chatlator does not touch what Events: Chat sends – writing and sending keep working.")).c_str());
    if (reading && pause != kPauseNone) {
        ImGui::SameLine();
        if (ImGui::SmallButton(UI(Tr(L"Read anyway")).c_str())) ChatSourceReadAnyway(false);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", UI(Tr(L"Reads this version without the check pausing it. Unreadable data still stops it – the game is never at risk.")).c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(UI(Tr(L"Check again")).c_str())) ChatSourceCheckAgain(false);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(UI(Tr(L"Copy protocol")).c_str())) ImGui::SetClipboardText(ChatSourceProtocolText().c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", UI(Tr(L"Versions and the rule each line failed – no chat text, no names. For a GitHub issue.")).c_str());
    ImGui::PopID();
}

void RenderChatSourceNotice() {  // the chat area (g_mu held): only while reading is off or paused
    int level = 0, pause = kPauseNone;
    const std::wstring text = StateText(level, pause);
    bool reading;
    {
        std::lock_guard<std::mutex> lock(g_src.mu);
        reading = g_src.reading;
    }
    if (reading && pause == kPauseNone) return;
    ImGui::PushID("chatsourcenote");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.75f, 0.35f, 1.f));
    ImGui::TextWrapped("%s", UI(text).c_str());
    ImGui::PopStyleColor();
    if (reading && ImGui::SmallButton(UI(Tr(L"Read anyway")).c_str())) ChatSourceReadAnyway(true);
    if (reading) ImGui::SameLine();
    if (ImGui::SmallButton(UI(Tr(L"Options")).c_str())) g_showOptions = true;
    ImGui::PopID();
}

// nexus_kb_selftest: the state, and the buttons pressed by code.
void ChatSourceTestState(ChatSourceTest* t) {
    std::lock_guard<std::mutex> lock(g_src.mu);
    t->reading = g_src.reading ? 1 : 0;
    t->pause = CurrentPause(g_src);
    t->probeLines = g_src.probe.Lines();
    t->probeFails = g_src.probe.Fails();
    t->shown = g_src.shown;
    t->dropped = g_src.dropped;
    auto put = [](char* dst, const std::wstring& s) { strncpy_s(dst, 32, U8(s).c_str(), _TRUNCATE); };
    put(t->version, g_src.version);
    put(t->verified, g_src.verified);
    put(t->bad, g_src.bad);
    put(t->probing, g_src.probing);
    t->marker = GetFileAttributesW(g_src.markerPath.c_str()) != INVALID_FILE_ATTRIBUTES ? 1 : 0;
    t->disabled = g_disabled ? 1 : 0;
    t->decoderFault = gct::DecoderRingFaulted() ? 1 : 0;
}

}  // namespace kb
