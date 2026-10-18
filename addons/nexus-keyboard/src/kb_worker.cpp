// kb_worker.cpp — worker jobs: spelling, translation, the typing help, sending.

#include "kb_shared.hpp"

namespace kb {


// ---------------------------------------------------------------------------------------------------------------
// Worker jobs.

void InitSpelling() {
    Config c;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        c = g_cfg;
    }
    g_spell = new SpellService();
    wchar_t locale[LOCALE_NAME_MAX_LENGTH] = {};
    GetUserDefaultLocaleName(locale, LOCALE_NAME_MAX_LENGTH);
    std::wstring typing;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        typing = TypingLocale();
    }
    if (c.spellEnabled) {
        std::vector<std::wstring> tags = SpellTagCandidates(PrimaryLang(typing), typing);
        if (c.writeIn.empty())
            for (const std::wstring& t : SpellTagCandidates(PrimaryLang(locale), locale)) tags.push_back(t);
        g_spell->Init(tags, c.UserWordsPath());
    }
    EnsureDir(c.LearnedDir());
    g_spell->UseLearnedLanguage(PrimaryLang(typing), c.LearnedDir());
    g_spell->SetLearnChoices(c.learnWords);
    g_mumbleWorker = new MumbleLink();
}

void FreeWorkerState() {
    if (g_spell) {
        g_spell->SaveLearned();
        delete g_spell;
        g_spell = nullptr;
    }
    delete g_mumbleWorker;
    g_mumbleWorker = nullptr;
}

// A chat line into your language, when its language is sure and not yours (nor one you understand) – only while
// "Translate the chat" is on; `asked` (a click on the line) translates it anyway, whatever its language seems to be.
void TranslateLine(uint64_t id, std::wstring text, bool asked, bool once) {
    std::shared_ptr<Translator> tr;
    std::wstring target;
    std::vector<std::wstring> understood;
    bool skipTyping = true;
    ProtectedText p = ProtectForTranslation(text, nullptr, g_spell ? &g_spell->KeepWords() : &BuiltinKeepWords());
    if (!HasTranslatableText(p.segments)) return;
    std::wstring detected = SureLanguage(text, DetectLanguage(text));  // on this PC (Windows), no translator asked
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (!detected.empty())  // what this player writes in (answering them in it)
            for (const ChatLine& l : g_lines)
                if (l.id == id && !l.speaker.empty()) g_playerLang[CaseFold(Trim(l.speaker))] = detected;
        if (!g_nx.translateChat && !asked && !once) return;  // once / a click: whatever "Translate the chat" says
        tr = g_translator;
        target = TypingLang();
        understood = g_cfg.understoodLangs;
        skipTyping = g_cfg.skipTypingLang;
    }
    if (!tr) return;
    if ((asked || once) && detected.empty()) detected = DetectLanguage(text);
    if (once) {  // independent of every chat setting: into the language you write in – unless it already is in it
        if (!detected.empty() && PrimaryLang(detected) == PrimaryLang(target)) return;
    } else if (!asked) {
        if (detected.empty()) return;
        if (skipTyping && PrimaryLang(detected) == PrimaryLang(target)) return;
        for (const std::wstring& u : understood)
            if (PrimaryLang(u) == PrimaryLang(detected)) return;
    }
    {
        std::lock_guard<std::mutex> lock(g_mu);
        CountMyMemory(CodePointCount(text));
    }
    const TranslateResult r = tr->Translate(p.segments, SourceCode(detected), target);
    if (!r.ok || r.text.empty()) return;
    const std::wstring t = SanitizeChatText(r.text);
    std::lock_guard<std::mutex> lock(g_mu);
    for (ChatLine& l : g_lines) {
        if (l.id == id && NormalizeForCompare(t) != NormalizeForCompare(l.text)) {
            l.translated = t;
            l.u8translated = U8(l.translated);
            l.transSegs = PieUI::ChatLinks::SegmentLine(l.u8translated);
        }
    }
}
std::vector<uint64_t> g_visibleLines;

// What can be seen of the chat, into the language you write in (the chat is read in it) – when "Translate the chat" is
// switched on, when the language you write in changes, or once from the menu. Every call starts afresh: a translation
// into another language is cleared and done again in the language you write in now. `once` (the menu) follows none of
// the chat settings: everything not already in the language you write in.
int TranslateRecentLines(bool once) {  // g_mu held; how many lines went to the translator
    int n = 0;
    for (ChatLine& l : g_lines) {
        if (l.own || l.speaker.empty() || std::find(g_visibleLines.begin(), g_visibleLines.end(), l.id) == g_visibleLines.end())
            continue;
        l.translated.clear();
        l.u8translated.clear();
        l.transSegs.clear();
        g_worker.Post([id = l.id, t = l.text, once] { TranslateLine(id, t, false, once); });
        ++n;
    }
    return n;
}

// The input into the chat's language.
void TranslateInput(uint64_t gen, std::wstring text) {
    std::shared_ptr<Translator> tr;
    std::wstring target, typing;
    bool translating;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        tr = g_translator;
        target = SendLang();
        typing = TypingLang();
        translating = Translating();
    }
    const ChatSplit split = SplitChatCommand(text);
    const std::wstring body = split.prefix.empty() ? text : split.body;
    std::wstring result = body;
    std::wstring error;
    if (translating && tr) {
        std::wstring expanded;  // your own words replaced by what they mean (only for the translator)
        {
            std::lock_guard<std::mutex> lock(g_mu);
            expanded = g_myWords.Expand(body);
        }
        ProtectedText p = ProtectForTranslation(expanded, nullptr, g_spell ? &g_spell->KeepWords() : &BuiltinKeepWords());
        if (HasTranslatableText(p.segments)) {
            std::wstring source = DetectLanguage(body);
            if (source.empty()) source = typing;
            {
                std::lock_guard<std::mutex> lock(g_mu);
                CountMyMemory(CodePointCount(body));
            }
            const TranslateResult r = tr->Translate(p.segments, source, target);
            if (r.ok) result = r.text;
            else error = r.error;
        }
    }
    // The back-translation: what your message means in your language again (small under it).
    std::wstring back;
    bool wantBack;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        wantBack = g_cfg.backTranslate && gen == g_inputGen && !g_sendPending;
    }
    if (translating && tr && wantBack && error.empty() && result != body) {
        ProtectedText p = ProtectForTranslation(result, nullptr, g_spell ? &g_spell->KeepWords() : &BuiltinKeepWords());
        if (HasTranslatableText(p.segments)) {
            {
                std::lock_guard<std::mutex> lock(g_mu);
                CountMyMemory(CodePointCount(result));
            }
            const TranslateResult r = tr->Translate(p.segments, SourceCode(target), typing);
            if (r.ok) back = SanitizeChatText(r.text);
        }
    }
    std::vector<std::wstring> parts;
    bool sendNow = false;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (gen != g_inputGen && !g_sendPending) return;  // the text changed meanwhile
        g_preview = SanitizeChatText(result);
        g_back = back;
        g_previewGen = gen;
        if (!error.empty()) SetStatus(error);
        if (g_sendPending && gen == g_inputGen) {
            g_sendPending = false;
            const std::wstring prefix = split.prefix.empty() ? ComposePrefix() : split.prefix;
            parts = SplitForChat(g_preview, prefix, static_cast<size_t>(g_cfg.maxLength));
            sendNow = true;
        }
    }
    if (sendNow) SendNow(std::move(parts), text, body);
}

// The word bar, the grey completion and what Space writes, for the newest text + caret.
void ComputeTyping() {
    std::wstring text;
    size_t caret;
    AutoCorrectMode mode;
    std::vector<std::wstring> context;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_helpRequested = false;
        text = g_wFlatInput;
        caret = text.size();  // the help works at the end of the line (where you type)
        mode = g_cfg.autoCorrect;
        context = g_context;
    }
    if (!g_spell || !g_cfg.suggestions) return;
    g_spell->SetContext(std::move(context));
    TypingHelp h;
    h.text = text;
    h.caret = caret;
    h.bar = g_spell->Suggestions(text, caret, mode);
    h.choices = g_spell->Choices(text, caret, mode);
    std::lock_guard<std::mutex> lock(g_mu);
    if (g_wFlatInput == text) g_help = std::move(h);
}

// Enter: the parts go into the GW2 chat – only now, on your Enter (invariants 1–3).
void SendNow(std::vector<std::wstring> parts, std::wstring typed, std::wstring learnText) {
    if (parts.empty()) return;
    if (g_test.noSend) {  // the self-test: what would go into the chat
        std::lock_guard<std::mutex> lock(g_mu);
        g_test.sentParts += static_cast<int>(parts.size());
        strncpy_s(g_test.lastSent, ToUtf8(parts.front()).c_str(), _TRUNCATE);
        g_sending = false;  // as after a real send
        g_history.push_front(typed);
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_sending = true;
        SetStatus(L"…", 0);
    }
    // ImGui must let go of the keyboard first, else our own input line would eat the keys.
    for (int i = 0; i < 50 && !g_keyboardFree; ++i) Sleep(10);
    HWND gw2 = FindGw2Window(GetCurrentProcessId());
    SendOptions so;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        so = g_cfg.send;
    }
    if (so.autoTiming) {
        const int fps = g_fps;
        if (fps > 0) {
            const int action = std::clamp(1000 / std::max(fps, 10) + 10, 15, 1010);
            so.keyHoldMs = action;
            so.stepDelayMs = action;
        }
    } else if (const int fps = g_fps; fps > 0 && so.keyHoldMs == 30) {  // GW2 reads the keyboard once per frame
        const int action = std::clamp(1000 / std::max(fps, 10) + 10, 15, 1010);
        so.keyHoldMs = action;
        if (so.stepDelayMs == 80) so.stepDelayMs = action;
    }
    std::wstring error;
    bool ok = gw2 != nullptr;
    if (!gw2) error = Tr(L"GW2 window not found");
    bool copyOnly;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        copyOnly = g_cfg.copyOnly;
    }
    if (ok && copyOnly) {  // not a single key reaches the game (invariant 1): only the clipboard
        std::wstring all;
        for (const std::wstring& part : parts) all += (all.empty() ? L"" : L" ") + part;
        ok = CopyTextToClipboard(gw2, all);
        if (!ok) error = Tr(L"The clipboard is blocked");
        parts.clear();
    }
    for (const std::wstring& part : parts) {
        if (!ok) break;
        const MumbleState ms = g_mumbleWorker ? g_mumbleWorker->Read() : MumbleState{};
        const SendOutcome out = SendToGw2Chat(gw2, part, so, ms.live ? g_mumbleWorker : nullptr);
        if (!out.ok) {
            ok = false;
            error = out.error;
        }
    }
    if (ok && g_spell) {
        Config c;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            c = g_cfg;
        }
        if (c.learnWords) g_spell->Learn(learnText);  // the word model learns only from what you send
    }
    {  // misheard words: what was dictated vs. what you finally sent ("heard = meant", per language)
        std::lock_guard<std::mutex> lock(g_mu);
        if (ok && !g_lastDictated.empty()) {
            if (!g_voiceFixes.Learn(g_lastDictated, learnText, g_dictLang).empty())
                WriteFileAtomic(g_cfg.dataDir + L"\\voice-fixes.txt", ToUtf8(g_voiceFixes.Serialize()));
            g_lastDictated.clear();
        }
    }
    std::lock_guard<std::mutex> lock(g_mu);
    g_sending = false;
    if (ok) {
        SetStatus(copyOnly ? Tr(L"Copied – paste it into the GW2 chat with Ctrl+V") : L"");
        g_history.push_front(typed);
        while (g_history.size() > kHistoryMax) g_history.pop_back();
        if (g_cfg.returnFocus) g_focusInput = true;
    } else {
        SetStatus(error, 10000);
        if (g_input[0] == 0) {  // nothing lost: the text comes back
            const std::string back = U8(typed);
            strncpy_s(g_input, back.c_str(), _TRUNCATE);
        }
    }
}

void StartSend() {  // Enter (g_mu held)
    const std::wstring text = SanitizeChatText(g_wFlatInput);
    if (text.empty() || g_sending) return;
    const SendGuard::Verdict v = g_guard.Check(text, false, false, GetTickCount64());
    if (!v.allow) {
        SetStatus(v.reason == SendGuard::Reason::Repeat ? Tr(L"Not sent: the same line too often in a short time.")
                  : v.reason == SendGuard::Reason::Rate  ? Tr(L"Not sent: more lines than a person writes in a minute.")
                                                         : Tr(L"Sending is switched off for this session (misuse detected). "
                                                              L"Restart the program to send again."),
                  10000);
        return;
    }
    const ChatSplit split = SplitChatCommand(text);
    const std::wstring body = split.prefix.empty() ? text : split.body;
    const std::wstring prefix = split.prefix.empty() ? ComposePrefix() : split.prefix;
    g_input[0] = 0;
    g_flatInput.clear();
    g_wFlatInput.clear();
    g_lastInput.clear();
    g_historyPos = -1;
    g_undo.active = false;
    ImGui::SetWindowFocus(nullptr);  // let go of the keyboard: the keys must reach the game
    if (!Translating()) {
        g_worker.Post([parts = SplitForChat(body, prefix, static_cast<size_t>(g_cfg.maxLength)), text, body] {
            SendNow(parts, text, body);
        });
    } else if (g_previewGen == g_inputGen && !g_preview.empty()) {
        g_worker.Post([parts = SplitForChat(g_preview, prefix, static_cast<size_t>(g_cfg.maxLength)), text, body] {
            SendNow(parts, text, body);
        });
    } else {  // the translation is not there yet: sent when it arrives
        g_sendPending = true;
        SetStatus(Tr(L"translating and sending …"), 0);
        g_worker.Post([gen = g_inputGen, text] { TranslateInput(gen, text); });
    }
}

}  // namespace kb
