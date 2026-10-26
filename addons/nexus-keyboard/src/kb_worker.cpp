// kb_worker.cpp — worker jobs: spelling, translation, the typing help, sending.

#include "kb_shared.hpp"

namespace kb {


// ---------------------------------------------------------------------------------------------------------------
// Worker jobs.

void InitSpelling() {
    AssertNotInteractiveLane("InitSpelling");
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
    AssertNotInteractiveLane("FreeWorkerState");
    if (g_spell) {
        g_spell->SaveLearned();
        delete g_spell;
        g_spell = nullptr;
    }
    delete g_mumbleWorker;
    g_mumbleWorker = nullptr;
}

// A chat line into your language – whether is core/translate_policy's (`ShouldTranslate`): automatically (only while
// "Translate the chat" is on) and `once` from the menu by the same rule; `asked` (a click on the line) always.
void TranslateLine(uint64_t id, std::wstring text, bool asked, bool once) {
    AssertNotInteractiveLane("TranslateLine");
    std::shared_ptr<Translator> tr;
    std::vector<std::wstring> understood;
    LineToTranslate what;
    what.trigger = asked ? TranslateTrigger::LineClick : once ? TranslateTrigger::Once : TranslateTrigger::Auto;
    auto keep = g_spell ? g_spell->KeepWordsSnapshot() : nullptr;
    ProtectedText p = ProtectForTranslation(text, nullptr, keep ? keep.get() : &BuiltinKeepWords());
    auto clearPending = [&](bool attempted = true) {
        std::lock_guard<std::mutex> lock(g_mu);
        for (ChatLine& l : g_lines) {
            if (l.id == id) {
                l.transPending = false;
                if (attempted) {
                    l.transAttempted = true;
                    l.transTarget = what.target;
                }
            }
        }
    };
    if (!HasTranslatableText(p.segments)) {
        clearPending(true);
        return;
    }
    const std::wstring els = DetectLanguage(text);  // on this PC (Windows), no translator asked
    what.detected = SureLanguage(text, els);
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (!what.detected.empty())  // what this player writes in (answering them in it)
            for (const ChatLine& l : g_lines)
                if (l.id == id && !l.speaker.empty()) g_playerLang[CaseFold(Trim(l.speaker))] = what.detected;
        const bool windowVisible = (g_show && (g_inMap || g_test.forceMap) && !g_mapOpen && !g_collapsed);
        what.chatOn = g_nx.translateChat && (asked || once || (windowVisible && !g_orchestrator.IsPaused()));
        tr = g_translator;
        what.target = TypingLang();
        understood = g_cfg.understoodLangs;
        what.skipTarget = g_cfg.skipTypingLang;
    }
    what.understood = &understood;
    const bool culled = !what.chatOn;
    if (!tr || !ShouldTranslate(what)) {
        clearPending(!culled);
        return;
    }
    const std::wstring& target = what.target;
    const std::wstring detected = what.detected.empty() ? els : what.detected;  // a click: the best guess, sure or not
    const std::wstring by = tr->Name();

    // Dynamic throttle: adapt beam size to queue pressure (greedy under load, configured quality otherwise)
    const int effectiveBeam = g_orchestrator.ComputeBeamSize(
        static_cast<QualityMode>(g_cfg.localQuality), g_worker.QueueSize());
    SetLocalMtBeamSize(tr, effectiveBeam);

    const TranslateResult r = g_transCache.Through({text, SourceCode(detected), target, by}, [&] {
        {
            std::lock_guard<std::mutex> lock(g_mu);
            CountMyMemory(CodePointCount(text));  // only a real request counts, never a line from the cache
        }
        return tr->Translate(p.segments, SourceCode(detected), target);
    });
    if (!r.ok || r.text.empty()) {  // never silent: the status says why (a missing language pack, the quota …)
        clearPending(true);
        if (!r.error.empty()) {
            std::lock_guard<std::mutex> lock(g_mu);
            SetStatus(r.error, 8000);
        }
        return;
    }
    const std::wstring t = SanitizeChatText(r.text);
    g_orchestrator.RecordTranslation(text, target, t, by);
    std::lock_guard<std::mutex> lock(g_mu);
    for (ChatLine& l : g_lines) {
        if (l.id == id) {
            l.transPending = false;
            l.transAttempted = true;
            if (NormalizeForCompare(t) != NormalizeForCompare(l.text)) {
                l.translated = t;
                l.transTarget = target;
                l.transBy = by;
                l.u8translated = U8(l.translated);
                l.transSegs = PieUI::ChatLinks::SegmentLine(l.u8translated);
            }
        }
    }
}
std::vector<uint64_t> g_visibleLines;
TranslationCache g_transCache;

// What can be seen of the chat, into the language you write in (the chat is read in it) – when "Translate the chat" is
// switched on, when the language you write in changes, or once from the menu. A translation into the language you
// write in now, by the translator you use now, stays (nothing paid twice); another one is cleared and done again – what
// was asked before comes from the session's cache (g_transCache). `once` (the menu) follows the automatic
// translation's rule without it being switched on (core/translate_policy).
int TranslateRecentLines(bool once) {  // g_mu held; how many lines of other players are in view
    const std::wstring& target = TypingLang();
    const std::wstring by = g_translator ? g_translator->Name() : std::wstring();
    int n = 0;
    for (ChatLine& l : g_lines) {
        if (l.own || l.speaker.empty() || std::find(g_visibleLines.begin(), g_visibleLines.end(), l.id) == g_visibleLines.end())
            continue;
        ++n;
        if (!l.translated.empty() && l.transTarget == target && l.transBy == by) continue;
        if (IsGamerInterjection(l.text)) continue;
        if (!once && l.transAttempted && l.transTarget == target) continue;

        std::wstring cachedTrans, cachedBy;
        if (g_orchestrator.SieveMessage(l.text, target, true, true, true, false, cachedTrans, cachedBy) == ChatPriority::Immediate) {
            l.translated = cachedTrans;
            l.transTarget = target;
            l.transBy = cachedBy;
            l.u8translated = U8(l.translated);
            l.transSegs = PieUI::ChatLinks::SegmentLine(l.u8translated);
            l.transPending = false;
            l.transAttempted = true;
            continue;
        }

        l.translated.clear();
        l.u8translated.clear();
        l.transSegs.clear();
        l.transPending = true;
        l.transAttempted = false;
        g_worker.Post([id = l.id, t = l.text, once] { TranslateLine(id, t, false, once); });
    }
    return n;
}

// The input into the chat's language.
void TranslateInput(uint64_t gen, std::wstring text) {
    AssertNotInteractiveLane("TranslateInput");
    std::shared_ptr<Translator> tr;
    std::wstring target, typing;
    bool translating;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        tr = g_translator;
        target = SendLang();
        typing = VoiceFirst() && !g_dictLang.empty() ? g_dictLang : TypingLang();  // what you said: its language
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
        auto keep = g_spell ? g_spell->KeepWordsSnapshot() : nullptr;
        ProtectedText p = ProtectForTranslation(expanded, nullptr, keep ? keep.get() : &BuiltinKeepWords());
        if (HasTranslatableText(p.segments)) {
            std::wstring source = DetectLanguage(body);
            if (source.empty()) source = typing;
            {
                std::lock_guard<std::mutex> lock(g_mu);
                CountMyMemory(CodePointCount(body));
            }
            const TranslateResult r = tr->Translate(p.segments, source, target);
            if (r.ok) {
                result = r.text;
                std::lock_guard<std::mutex> lock(g_mu);
                g_forcePreviewForHardError = false;
                g_lastErrorKind = TranslateErrorKind::None;
            } else {
                error = r.error;
                if (r.errorKind != TranslateErrorKind::None) {
                    std::lock_guard<std::mutex> lock(g_mu);
                    g_lastErrorKind = r.errorKind;
                    SYSTEMTIME st;
                    GetLocalTime(&st);
                    wchar_t timeBuf[64];
                    swprintf(timeBuf, 64, L"%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                    g_lastErrorTime = timeBuf;
                    g_forcePreviewForHardError = true;
                    if (!g_layout.preview) {
                        g_layout.preview = true;
                        SaveLayout();
                    }
                }
            }
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
        auto keep = g_spell ? g_spell->KeepWordsSnapshot() : nullptr;
        ProtectedText p = ProtectForTranslation(result, nullptr, keep ? keep.get() : &BuiltinKeepWords());
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
            if (!error.empty()) {  // the translation failed: nothing goes out in the wrong language – the text comes back
                if (g_input[0] == 0) {
                    const std::string again = U8(text);
                    strncpy_s(g_input, again.c_str(), _TRUNCATE);
                }
                SetStatus(TrF(L"Not sent: the translation failed – {1}", {error}), 10000);
                return;
            }
            const std::wstring prefix = split.prefix.empty() ? ComposePrefix() : split.prefix;
            parts = SplitForChat(g_preview, prefix, static_cast<size_t>(g_cfg.maxLength));
            sendNow = true;
        }
    }
    if (sendNow) SendNow(std::move(parts), text, body);
}

// The word bar, the grey completion and what Space writes, for the newest text + caret.
void ComputeTyping() {
    if (g_workerLane != Lane::Interactive) {
        Log(LOGL_WARNING, "ComputeTyping scheduled outside Interactive Lane");
    }
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
    AssertNotInteractiveLane("SendNow");
    if (parts.empty()) return;
    if (parts.size() > kMaxPartsPerEnter) {  // the translation came out longer (sent when it arrived): not sent either
        std::lock_guard<std::mutex> lock(g_mu);
        g_history.push_front(typed);
        SetStatus(TrF(L"Not sent: {1} chat lines – at most 3 per Enter. Shorten it (Up brings it back).",
                      {std::to_wstring(parts.size())}), 10000);
        return;
    }
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
                if (!g_wiped) WriteFileAtomic(g_cfg.dataDir + L"\\voice-fixes.txt", ToUtf8(g_voiceFixes.Serialize()));
            g_lastDictated.clear();
        }
    }
    std::lock_guard<std::mutex> lock(g_mu);
    g_sending = false;
    if (ok) {
        SetStatus(copyOnly ? Tr(L"Copied – paste it into the GW2 chat with Ctrl+V") : L"");
        g_history.push_front(typed);
        while (g_history.size() > kHistoryMax) g_history.pop_back();
        // Hands free (Punkt 4): nach dem Senden Fokus dorthin zurück, wo er vorher war
        if (g_autoSendTriggered) {
            g_autoSendTriggered = false;
            if (g_autoSendHadFocus) {
                g_focusInput = true;
            } else {
                g_focusInput = false;
                if (g_gameHwnd) SetFocus(g_gameHwnd);
            }
        } else {
            if (g_cfg.returnFocus) g_focusInput = true;
        }
    } else {
        SetStatus(error, 10000);
        if (g_input[0] == 0) {  // nothing lost: the text comes back
            const std::string back = U8(typed);
            strncpy_s(g_input, back.c_str(), _TRUNCATE);
        }
        if (g_autoSendTriggered) {
            g_autoSendTriggered = false;
            if (g_autoSendHadFocus) {
                g_focusInput = true;
            } else {
                g_focusInput = false;
                if (g_gameHwnd) SetFocus(g_gameHwnd);
            }
        }
    }
}

void StartSend() {  // Enter (g_mu held)
    const std::wstring text = SanitizeChatText(g_wFlatInput);
    if (text.empty() || g_sending) return;
    if (g_userRecording && g_recording) {  // what you said is still being recognized: it comes into the input first
        SetStatus(Tr(L"Not sent yet: what you said is still being recognized"), 4000);
        return;
    }
    const SendGuard::Verdict v = g_guard.Check(text, false, false, GetTickCount64());
    if (!v.allow) {
        CancelAutoEnter();
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
    {  // one Enter never pours a block of lines into the game: at most kMaxPartsPerEnter chat lines (the text stays)
        const bool ready = Translating() && g_previewGen == g_inputGen && !g_preview.empty();
        const size_t parts = SplitForChat(ready ? g_preview : body, prefix, static_cast<size_t>(g_cfg.maxLength)).size();
        if ((!Translating() || ready) && parts > kMaxPartsPerEnter) {
            CancelAutoEnter();
            SetStatus(TrF(L"Not sent: {1} chat lines – at most 3 per Enter. Shorten it.", {std::to_wstring(parts)}), 8000);
            return;
        }
    }
    g_bufferingStartTime = GetTickCount64();
    g_bufferingKeys = true;
    g_input[0] = 0;
    g_flatInput.clear();
    g_wFlatInput.clear();
    g_lastInput.clear();
    g_lastWords.clear();  // learning mode: the next message starts fresh (its words were compared with the sent one's)
    g_finishedLast.clear();
    g_finishedAt = 0;
    g_historyPos = -1;
    g_undo.active = false;
    ImGui::SetWindowFocus(nullptr);  // let go of the keyboard: the keys must reach the game
    if (!Translating()) {
        g_worker.PostFront([parts = SplitForChat(body, prefix, static_cast<size_t>(g_cfg.maxLength)), text, body] {
            SendNow(parts, text, body);
        });
    } else if (g_previewGen == g_inputGen && !g_preview.empty()) {
        g_worker.PostFront([parts = SplitForChat(g_preview, prefix, static_cast<size_t>(g_cfg.maxLength)), text, body] {
            SendNow(parts, text, body);
        });
    } else {  // the translation is not there yet: sent when it arrives
        g_sendPending = true;
        SetStatus(Tr(L"translating and sending …"), 0);
        g_worker.PostFront([gen = g_inputGen, text] { TranslateInput(gen, text); });
    }
}

}  // namespace kb
