// kb_chatsource.cpp — "our module" between Events: Chat and us. Updates and compatibility are Nexus' and Events: Chat's
// business; ours is that our code never takes the game down and never shows garbage, whatever a new version hands us:
//   1. A shield: the only code that touches the other add-on's memory looks first (VirtualQuery: committed, readable, no
//      guard page) and copies a line into our own buffer – pointers checked, strings bounded, SEH as the last net.
//   2. Every copied line is checked (core/chat_source): a line that fails a hard rule is never shown.
//   3. Reading pauses by itself for the session on unreadable memory or when 5 of the last 20 lines fail; "Resume" by hand.
//   4. By hand: "Read the chat" on / off (kept). For the developer: Options > Technical – the version of Events: Chat
//      (and whether it is new since the last session; then a note once), the counts, the last lines as lengths and rules
//      ("Copy protocol": never text, never names).

#include "kb_shared.hpp"
#include "core/chat_source.hpp"

namespace kb {

namespace {

bool UserPtr(const void* p) {  // an address user code can own (x64); anything else is never read
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= 0x10000 && v < 0x00007FFFFFFF0000ull;
}

// Looks before it reads: [okFrom, okTo) is the last region VirtualQuery called readable. A guard page counts as not
// readable – touching it would take away another thread's stack guard (SEH would catch it, the thread die later).
bool Readable(uintptr_t a, uintptr_t& okFrom, uintptr_t& okTo) {
    if (a >= okFrom && a < okTo) return true;
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(reinterpret_cast<const void*>(a), &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) return false;
    const DWORD p = mbi.Protect & 0xFF;
    if (p != PAGE_READONLY && p != PAGE_READWRITE && p != PAGE_WRITECOPY && p != PAGE_EXECUTE_READ &&
        p != PAGE_EXECUTE_READWRITE && p != PAGE_EXECUTE_WRITECOPY)
        return false;
    okFrom = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
    okTo = okFrom + mbi.RegionSize;
    return true;
}

// false: the memory could not be read (the line is unreadable).
bool CopyString(const char* src, char* dst, size_t max, bool* isNull, bool* badPtr, bool* cut) {
    *isNull = src == nullptr;
    *badPtr = false;
    *cut = false;
    dst[0] = 0;
    if (!src) return true;
    if (!UserPtr(src)) {
        *badPtr = true;
        return true;
    }
    uintptr_t okFrom = 0, okTo = 0;
    size_t i = 0;
    for (; i < max; ++i) {  // never a byte past the bound, never a byte not looked at first
        if (!Readable(reinterpret_cast<uintptr_t>(src + i), okFrom, okTo)) return false;
        if (!src[i]) break;
        dst[i] = src[i];
    }
    dst[i] = 0;
    *cut = i == max;
    return true;
}

// Layer 1: plain copies behind SEH (nothing here needs unwinding). 1 = copied, 0 = no line, -1 = not readable.
int CopyLineGuarded(const void* raw, gct::ChatSourceLine* out) {
    __try {
        if (!raw) return 0;
        if (!UserPtr(raw)) return -1;
        uintptr_t okFrom = 0, okTo = 0;
        const uintptr_t a = reinterpret_cast<uintptr_t>(raw);
        if (!Readable(a, okFrom, okTo) || !Readable(a + sizeof(bridge::ChatEventsMessage) - 1, okFrom, okTo)) return -1;
        const auto* m = static_cast<const bridge::ChatEventsMessage*>(raw);
        out->time = (static_cast<uint64_t>(m->timeHigh) << 32) | m->timeLow;
        out->type = static_cast<int32_t>(m->type);
        out->flags = m->flags;
        bool ok = true;
        if (gct::ChatSourceTypeHasName(out->type)) {
            out->read = true;
            ok = CopyString(m->generic.characterName, out->name, gct::ChatSourceLine::kNameMax, &out->nameNull,
                            &out->nameBadPtr, &out->nameCut) &&
                 CopyString(m->generic.accountName, out->account, gct::ChatSourceLine::kAccountMax, &out->accountNull,
                            &out->accountBadPtr, &out->accountCut) &&
                 CopyString(m->generic.content, out->content, gct::ChatSourceLine::kContentMax, &out->contentNull,
                            &out->contentBadPtr, &out->contentCut);
        } else if (gct::ChatSourceTypeTextOnly(out->type)) {
            out->read = true;
            ok = CopyString(m->squadMessage, out->content, gct::ChatSourceLine::kContentMax, &out->contentNull,
                            &out->contentBadPtr, &out->contentCut);
        }
        return ok ? 1 : -1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}

enum Pause { kPauseNone = 0, kPauseUnreadable = 3, kPauseDrift = 4 };

struct SourceState {
    std::mutex mu;  // never held while g_mu is taken (g_mu first, then this)
    bool reading = true;        // [ChatSource] Reading – the switch in the options
    std::wstring version;       // the loaded Events: Chat ("" = not found)
    std::wstring lastVersion;   // [ChatSource] LastVersion: what the last session had
    std::wstring before;        // this session's Events: Chat is new: the version before it (for the details)
    int pause = kPauseNone;     // reading paused for this session (unreadable memory, lines stopped fitting)
    bool announced = false;     // the pause was said once in the window (also in the mini mode)
    gct::ChatSourceWatch watch;
    std::deque<gct::ChatSourceRecord> recent;  // the last 20 lines: types, lengths, rules – never text
    // Every line lands in exactly one pot: received == shown + dropped + ignored (a known type we do not show, an
    // empty event) – nothing disappears unnoticed (the self-test and the smoke test check the equation).
    int received = 0, shown = 0, dropped = 0, ignored = 0, unreadable = 0;
};
SourceState g_src;

void Persist(const wchar_t* key, const std::wstring& value, bool muHeld) {
    if (g_wiped) return;
    std::unique_lock<std::mutex> lock(g_mu, std::defer_lock);
    if (!muHeld) lock.lock();
    g_cfg.SaveValue(L"ChatSource", key, value);
}

void Say(const std::wstring& status) {  // g_mu NOT held
    if (status.empty()) return;
    std::lock_guard<std::mutex> lock(g_mu);
    SetStatus(status, 15000);
}

uint64_t NowFiletime() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    return (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

std::wstring Ver(const SourceState& s) { return s.version.empty() ? L"?" : s.version; }

std::wstring PauseText(int reason, const std::wstring& ver) {
    return reason == kPauseUnreadable
               ? TrF(L"Events: Chat {1} handed over unreadable data – reading paused for this session.", {ver})
               : TrF(L"Lines from Events: Chat {1} stopped fitting – reading paused for this session.", {ver});
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
    GetPrivateProfileStringW(L"ChatSource", L"LastVersion", L"", buf, 64, ini.c_str());
    g_src.lastVersion = buf;
    // 2.1.0 checked every new version by itself; what it left goes (only written when it is there).
    DeleteFileW((g_cfg.dataDir + L"\\chat-source-check.txt").c_str());
    for (const wchar_t* old : {L"VerifiedVersion", L"BadVersion", L"BadReason"})
        if (GetPrivateProfileStringW(L"ChatSource", old, L"\x01", buf, 64, ini.c_str()) != 1 || buf[0] != 1)
            WritePrivateProfileStringW(L"ChatSource", old, nullptr, ini.c_str());
    g_src.version.clear();
    g_src.before.clear();
    g_src.pause = kPauseNone;
    g_src.announced = false;
    g_src.watch.Reset();
    g_src.recent.clear();
    g_src.received = g_src.shown = g_src.dropped = g_src.ignored = g_src.unreadable = 0;
}

void ChatSourceSetVersion(const std::wstring& version) {  // from the module scan (every 3 s; g_mu NOT held)
    std::wstring note;
    bool save = false;
    {
        std::lock_guard<std::mutex> lock(g_src.mu);
        if (version == g_src.version) return;
        g_src.version = version;
        g_src.pause = kPauseNone;  // another Events: Chat: a fresh start
        g_src.announced = false;
        g_src.watch.Reset();
        if (version.empty() || version == g_src.lastVersion) return;
        if (!g_src.lastVersion.empty()) {  // updated since the last session: said once, the details show it
            g_src.before = g_src.lastVersion;
            note = TrF(L"Events: Chat {1} is new (before: {2}) – details: Options > Technical.", {version, g_src.before});
        }
        g_src.lastVersion = version;
        save = true;
    }
    if (save) Persist(L"LastVersion", version, false);
    Say(note);
}

void OnChatEvents(void* args) {
    if (g_disabled || g_wiped) return;
    g_chatEventsLive = true;
    struct Timed {  // what a line costs us on the thread Events: Chat calls from (Options > Technical)
        LARGE_INTEGER t0;
        Timed() { QueryPerformanceCounter(&t0); }
        ~Timed() {
            LARGE_INTEGER t1, f;
            QueryPerformanceCounter(&t1);
            QueryPerformanceFrequency(&f);
            CostLine(1e6 * static_cast<double>(t1.QuadPart - t0.QuadPart) / static_cast<double>(f.QuadPart));
        }
    } timed;
    std::wstring ver, status;
    {
        std::lock_guard<std::mutex> lock(g_src.mu);
        ++g_src.received;
        if (!g_src.reading || g_src.pause != kPauseNone) {
            ++g_src.dropped;
            return;  // off or paused: not a byte of the line is touched
        }
        ver = Ver(g_src);
    }
    gct::ChatSourceLine line;
    const int got = CopyLineGuarded(args, &line);
    if (got == 0) {  // no line at all
        std::lock_guard<std::mutex> lock(g_src.mu);
        ++g_src.ignored;
        return;
    }
    gct::ChatLineVerdict v;
    if (got < 0) v.rules = gct::kRuleUnreadable;
    else v = gct::CheckChatLine(line, NowFiletime());
    const bool show = got > 0 && line.read && !v.Hard();
    {
        std::lock_guard<std::mutex> lock(g_src.mu);
        g_src.recent.push_back(gct::RecordOf(line, v));
        if (g_src.recent.size() > 20) g_src.recent.pop_front();
        if (show) ++g_src.shown;
        else if (got < 0 || v.Hard()) ++g_src.dropped;
        else ++g_src.ignored;  // a known type we do not show: no failure
        int pause = kPauseNone;
        if (got < 0) {
            ++g_src.unreadable;
            pause = kPauseUnreadable;
        } else if ((line.read || v.Hard()) && g_src.watch.Feed(v.Hard())) {
            pause = kPauseDrift;
        }
        if (pause != kPauseNone && g_src.pause == kPauseNone) {
            g_src.pause = pause;
            if (!g_src.announced) {
                g_src.announced = true;
                status = PauseText(pause, ver);
            }
        }
    }
    Say(status);
    if (show) Dispatch(line);
}

void ChatSourceSetReading(bool on, bool muHeld) {
    {
        std::lock_guard<std::mutex> lock(g_src.mu);
        g_src.reading = on;
    }
    Persist(L"Reading", on ? L"1" : L"0", muHeld);
}

void ChatSourceResume() {  // after a pause: read again (unreadable memory still stops it at once)
    std::lock_guard<std::mutex> lock(g_src.mu);
    g_src.pause = kPauseNone;
    g_src.announced = false;
    g_src.watch.Reset();
}

std::string ChatSourceProtocolText() {
    std::lock_guard<std::mutex> lock(g_src.mu);
    gct::ChatSourceReport r;
    r.addonVersion = std::to_string(GCT_VER_MAJOR) + "." + std::to_string(GCT_VER_MINOR) + "." + std::to_string(GCT_VER_PATCH);
    r.sourceVersion = U8(g_src.version);
    r.previousVersion = U8(g_src.before);
    r.state = !g_src.reading                    ? "switched off by hand"
              : g_src.pause == kPauseUnreadable ? "paused: unreadable data"
              : g_src.pause == kPauseDrift      ? "paused: lines stopped fitting"
                                                : "reading";
    r.state += " - shown " + std::to_string(g_src.shown) + ", dropped " + std::to_string(g_src.dropped) +
               ", unreadable " + std::to_string(g_src.unreadable);
    r.lines.assign(g_src.recent.begin(), g_src.recent.end());
    return gct::ChatSourceProtocol(r);
}

namespace {

struct View {
    std::wstring text;
    int level = 2;  // 2 fine, 0 off or paused
    int pause = kPauseNone;
    bool reading = true;
};

View StateView() {
    View v;
    std::lock_guard<std::mutex> lock(g_src.mu);
    v.reading = g_src.reading;
    v.pause = g_src.pause;
    if (!g_src.reading) {
        v.level = 0;
        v.text = Tr(L"off – GW2Chatlator does not touch what Events: Chat sends");
    } else if (g_src.pause != kPauseNone) {
        v.level = 0;
        v.text = PauseText(g_src.pause, Ver(g_src));
    } else {
        v.text = Tr(L"on");
    }
    return v;
}

void Buttons(const View& v, bool muHeld) {
    bool reading = v.reading;
    if (ImGui::Checkbox(UI(Tr(L"Read the chat")).c_str(), &reading)) ChatSourceSetReading(reading, muHeld);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", UI(Tr(L"Off: GW2Chatlator does not touch what Events: Chat sends – writing and sending keep working.")).c_str());
    if (v.reading && v.pause != kPauseNone) {
        ImGui::SameLine();
        if (ImGui::SmallButton(UI(Tr(L"Resume")).c_str())) ChatSourceResume();
    }
}

}  // namespace

int ChatSourceStatus(std::wstring* text) {  // any thread: 2 reading, 0 off or paused – and in words
    const View v = StateView();
    if (text) *text = v.text;
    return v.level;
}

void RenderChatSourceNotice() {  // the chat area (g_mu held): only while reading is off or paused
    if (!g_chatEventsLive && !g_chatEventsPresent) return;  // no Events: Chat: the modules list says so
    const View v = StateView();
    if (v.reading && v.pause == kPauseNone) return;
    ImGui::PushID("chatsourcenote");
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.75f, 0.35f, 1.f));
    ImGui::TextWrapped("%s", UI(v.text).c_str());
    ImGui::PopStyleColor();
    if (v.reading && ImGui::SmallButton(UI(Tr(L"Resume")).c_str())) ChatSourceResume();
    if (v.reading) ImGui::SameLine();
    if (ImGui::SmallButton(UI(Tr(L"Options")).c_str())) g_showOptions = true;
    ImGui::PopID();
}

void RenderChatSourceDetails() {  // Options > Technical (g_mu held): for the developer
    const View v = StateView();
    std::wstring version, before;
    int shown, dropped, unreadable;
    std::vector<gct::ChatSourceRecord> lines;
    {
        std::lock_guard<std::mutex> lock(g_src.mu);
        version = g_src.version;
        before = g_src.before;
        shown = g_src.shown;
        dropped = g_src.dropped;
        unreadable = g_src.unreadable;
    }
    ImGui::PushID("chatsourcedetails");
    std::wstring head = L"Events: Chat " + (version.empty() ? std::wstring(L"[--]") : version);
    if (!before.empty()) head += L"  " + TrF(L"new since the last session (before: {1})", {before});
    ImGui::TextUnformatted(UI(head).c_str());
    StatusRow(UI(Tr(L"Reading")), v.level, UI(v.text));
    ImGui::Text("%s", UI(TrF(L"Lines: {1} shown, {2} dropped, {3} unreadable",
                             {std::to_wstring(shown), std::to_wstring(dropped), std::to_wstring(unreadable)})).c_str());
    {  // what we cost on GW2's render thread – the parts add up to the whole
        const FrameCosts c = CostsNow();
        static Steady s_total, s_part[6], s_line;  // smoothed, shown 4 times a second
        auto ms = [](double v) {
            wchar_t b[16];
            swprintf_s(b, L"%.3f", v);
            return std::wstring(b);
        };
        ImGui::Text("%s", UI(TrF(L"Our frame: {1} ms – window {2}, header {3}, chat {4}, input {5}, footer {6}, options {7}",
                                 {ms(s_total(c.totalMs)), ms(s_part[0](c.ms[kCostWindow] + c.ms[kCostPre])),
                                  ms(s_part[1](c.ms[kCostHeader])), ms(s_part[2](c.ms[kCostChat])),
                                  ms(s_part[3](c.ms[kCostInput])), ms(s_part[4](c.ms[kCostFooter])),
                                  ms(s_part[5](c.ms[kCostOptions]))})).c_str());
        ImGui::Text("%s", UI(TrF(L"Per chat line: {1} µs", {ms(s_line(c.lineUs))})).c_str());
    }
    Buttons(v, true);
    ImGui::SameLine();
    if (ImGui::SmallButton(UI(Tr(L"Copy protocol")).c_str())) ImGui::SetClipboardText(ChatSourceProtocolText().c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", UI(Tr(L"Versions and how often the last lines broke which rule – nothing of a single message. For a GitHub issue.")).c_str());
    if (ImGui::TreeNode(UI(Tr(L"The last lines, counted")).c_str())) {
        const std::string p = ChatSourceProtocolText();
        const size_t at = p.find("Last ");
        ImGui::TextDisabled("%s", at == std::string::npos ? p.c_str() : p.c_str() + at);
        ImGui::TreePop();
    }
    ImGui::PopID();
}

// nexus_kb_selftest: the state.
void ChatSourceTestState(ChatSourceTest* t) {
    std::lock_guard<std::mutex> lock(g_src.mu);
    t->reading = g_src.reading ? 1 : 0;
    t->pause = g_src.pause;
    t->shown = g_src.shown;
    t->dropped = g_src.dropped;
    t->unreadable = g_src.unreadable;
    t->received = g_src.received;
    t->ignored = g_src.ignored;
    t->disabled = g_disabled ? 1 : 0;
    strncpy_s(t->version, U8(g_src.version).c_str(), _TRUNCATE);
    strncpy_s(t->before, U8(g_src.before).c_str(), _TRUNCATE);
}

}  // namespace kb
