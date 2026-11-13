// kb_options_typing.cpp — the Chat and Typing pages, and the word lists.

#include "kb_shared.hpp"
#include "kb_options_parts.hpp"
#include "core/slang.hpp"
#include "core/word_model.hpp"
#include "imgui/imgui_internal.h"

namespace kb {

// The word lists (Technical tab): how many entries each holds – the built-in ones compiled in, yours and the learned
// ones to look at and correct here. Typo patterns only counted: correcting them by hand would fight what typing learns.
void RenderWordLists() {
    if (!ImGui::CollapsingHeader(UI(Tr(L"Word lists")).c_str())) return;  // folded until you open it
    size_t never = 0;
    for (const BuiltinTerm& bt : BuiltinTerms())
        if (bt.kind != TermKind::Word) ++never;
    ImGui::TextDisabled("%s", UI(TrF(L"Built-in GW2 terms: {1} (never translated: {2})",
                                     {std::to_wstring(BuiltinTerms().size()), std::to_wstring(never)})).c_str());
    ImGui::TextDisabled("%s", UI(TrF(L"Official names: {1} in en/de/fr/es", {std::to_wstring(BuiltinNames(L"en").size())})).c_str());
    ImGui::TextDisabled("%s", UI(TrF(L"Usual words: {1}", {std::to_wstring(BuiltinWordCount())})).c_str());
    if (!g_spell) return;
    auto chips = [](const std::vector<std::wstring>& items, auto&& onRemove) {  // one entry each, "x" removes it
        for (size_t i = 0; i < items.size(); ++i) {
            const std::string label = U8(items[i]) + "  x##" + std::to_string(i);
            const float w = ImGui::CalcTextSize(label.c_str(), nullptr, true).x + ImGui::GetStyle().FramePadding.x * 2;
            if (i > 0) {
                ImGui::SameLine();
                if (ImGui::GetContentRegionAvail().x < w) ImGui::NewLine();
            }
            if (ImGui::SmallButton(label.c_str())) onRemove(items[i]);
        }
    };
    // a list's file in your editor; after saving it there, "Reload after editing" takes it in
    auto fileButtons = [](const char* id, const std::wstring& path, auto&& reload) {
        ImGui::SameLine();
        if (ImGui::SmallButton((UI(Tr(L"Open")) + "##open" + id).c_str()) && !path.empty())
            ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        ImGui::SameLine();
        if (ImGui::SmallButton((UI(Tr(L"Reload after editing")) + "##reload" + id).c_str())) g_worker.Post(reload);
    };
    const std::vector<std::wstring> mine = g_spell->MyTerms();
    ImGui::TextUnformatted(UI(TrF(L"Your terms: {1}", {std::to_wstring(mine.size())})).c_str());
    fileButtons("terms", g_spell->TermsFile(), [] { if (g_spell) g_spell->ReloadTerms(); });
    ImGui::PushID("myterms");
    chips(mine, [](const std::wstring& m) { g_worker.Post([m] { if (g_spell) g_spell->RemoveTerm(m); ForgetRepetitions(); }); });
    static char addBuf[64] = {};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    const bool enter = ImGui::InputText("##addterm", addBuf, sizeof(addBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if ((ImGui::SmallButton(UI(Tr(L"Add")).c_str()) || enter) && addBuf[0]) {
        const std::wstring t = Trim(W(addBuf));
        addBuf[0] = 0;
        g_worker.Post([t] { if (g_spell) g_spell->AddTerm(t); ForgetRepetitions(); });
    }
    ImGui::PopID();
    // the learned words: the most used first (40), a search finds any; "x" forgets one
    const auto snap = g_spell->Model().GetSnapshot();
    ImGui::TextUnformatted(UI(TrF(L"Learned words: {1} – the most used; x forgets one",
                                  {std::to_wstring(snap->words.size())})).c_str());
    fileButtons("learned", g_spell->LearnedFile(), [] { if (g_spell) g_spell->ReloadLearned(); });
    static char findBuf[32] = {};
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
    ImGui::InputTextWithHint("##findlearned", UI(Tr(L"Find a word")).c_str(), findBuf, sizeof(findBuf));
    const std::wstring find = CaseFold(W(findBuf));
    std::vector<const WordModelSnapshot::Word*> found;
    for (const auto& [key, w] : snap->words)
        if (find.empty() || key.find(find) != std::wstring::npos) found.push_back(&w);
    const size_t n = std::min<size_t>(found.size(), 40);
    std::partial_sort(found.begin(), found.begin() + static_cast<std::ptrdiff_t>(n), found.end(),
                      [](const WordModelSnapshot::Word* a, const WordModelSnapshot::Word* b) { return a->count > b->count; });
    std::vector<std::wstring> shown;
    for (size_t i = 0; i < n; ++i) shown.push_back(found[i]->form);
    ImGui::PushID("learned");
    chips(shown, [](const std::wstring& w) { g_worker.Post([w] { if (g_spell) g_spell->Forget(w); }); });
    ImGui::PopID();
    ImGui::TextDisabled("%s", UI(TrF(L"Typo patterns learned: {1}", {std::to_wstring(g_spell->TypoPatterns())})).c_str());
}

void RenderChatPage(Config& edit) {
    BeginBox(UI(Tr(L"Display")));
    Switch("codenames", UI(Tr(L"Show chat codes by name")), &edit.chatCodeNames);
    Hint(UI(Tr(L"Asks GW2's public API. Only the code's number goes out, never a chat text.")));
    Switch("orig", UI(Tr(L"Original under the translation")), &edit.showOriginal);
    Switch("unsure", UI(Tr(L"Unsure: show the original")), &edit.noJunk);
    Hint(UI(Tr(L"A click on a line always translates it. A click on a name opens a whisper tab that answers in "
               L"that player's language.")));
    EndBox();
    BeginBox(UI(Tr(L"Focus")));
    {
        std::lock_guard<std::mutex> lock(g_mu);
        bool full = g_layout.autoOpen && edit.focusOnGameChat && edit.returnFocus;
        const std::string tip = UI(Tr(L"Enables all 3 focus options: automatically opens, takes over Enter in game, and keeps focus after sending."));
        if (Switch("full", UI(Tr(L"Complete chat takeover (full focus)")), &full, tip.c_str())) {
            g_layout.autoOpen = full;
            edit.focusOnGameChat = full;
            edit.returnFocus = full;
            SaveLayout();
        }
        if (Switch("open", UI(Tr(L"Open again when GW2's chat opens (Enter)")), &g_layout.autoOpen)) SaveLayout();
    }
    Switch("enter", UI(Tr(L"Enter in the game opens GW2Chatlator's input")), &edit.focusOnGameChat);
    Hint(UI(Tr(L"Instead of GW2's own chat line: you type here, with the typing help and the translation.")));
    Switch("return", UI(Tr(L"After sending, keep typing in GW2Chatlator")), &edit.returnFocus);
    Hint(UI(Tr(L"The input keeps the keyboard after Enter – the next message goes right after.")));
    EndBox();
    BeginBox(UI(Tr(L"Sending")));
    RowLabel(UI(Tr(L"Enter does")));
    int enterDoes = edit.copyOnly ? 1 : 0;
    if (RowCombo("##enter", {UI(Tr(L"Send into the GW2 chat")), UI(Tr(L"Only copy (not a single key reaches the game)"))}, &enterDoes))
        edit.copyOnly = enterDoes == 1;
    Switch("back", UI(Tr(L"Show the back-translation of my message")), &edit.backTranslate);
    EndBox();
}

void RenderTypingPage(Config& edit) {
    static bool s_forgetAsked = false;
    BeginBox(UI(Tr(L"Typing help")));
    RowLabel(UI(Tr(L"I write in")));
    ImGui::SetNextItemWidth(ControlW());
    LanguageCombo("##writein", edit.writeIn, Tr(L"System") + L" (" + NativeName(TypingLang()) + L")");
    RowHint(UI(Tr(L"Only suggestions and the dictionary – the language of your text is recognized.")));
    Switch("sugg", UI(Tr(L"Word suggestions while typing")), &edit.suggestions);
    Switch("spell", UI(Tr(L"Spell checking (Windows, offline)")), &edit.spellEnabled);
    {
        std::wstring dictLocale;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            dictLocale = g_spellDictLocale;
        }
        if (dictLocale.empty()) {
            static bool s_asked = false;
            if (!s_asked) {
                s_asked = true;
                g_worker.Post([] { if (!g_spell) InitSpelling(); });
            }
        } else if ((edit.spellEnabled || edit.autoCorrect != AutoCorrectMode::Off) && !g_spellDictReady.load()) {
            const std::wstring lang = NativeName(dictLocale);
            RiskHint(TrF(L"Windows has no dictionary for {1} – no correction in this language. Add it in Windows: Settings > Time & language > Language & region > {1} > Language options > Basic typing.", {lang}));
            ImGui::Indent(g_labelW);
            if (ImGui::SmallButton(UI(Tr(L"Open Windows' language settings")).c_str()))
                ShellExecuteW(nullptr, L"open", L"ms-settings:regionlanguage", nullptr, nullptr, SW_SHOWNORMAL);
            ImGui::Unindent(g_labelW);
        }
    }
    RowLabel(UI(Tr(L"Autocorrection")));
    int ac = static_cast<int>(edit.autoCorrect);
    if (ac < 0 || ac > 3) ac = 2;
    static bool s_showConsentDialog = false;

    if (edit.fallenAngel) {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);
    }
    if (RowCombo("##ac", {UI(Tr(L"Off")), UI(Tr(L"Safe")), UI(Tr(L"Like a phone keyboard")), UI(Tr(L"Strong"))}, &ac)) {
        edit.autoCorrect = static_cast<AutoCorrectMode>(ac);
        if (edit.autoCorrect == AutoCorrectMode::Strong) {
            const std::wstring normUrl = NormalizeLanguageToolUrl(edit.languageToolUrl);
            const bool isLocalHost = normUrl.find(L"localhost") != std::wstring::npos || normUrl.find(L"127.0.0.1") != std::wstring::npos;
            if (!isLocalHost && edit.languageToolConsent.empty()) {
                s_showConsentDialog = true;
            }
        }
    }
    if (edit.fallenAngel) {
        ImGui::PopStyleVar();
        ImGui::PopItemFlag();
        RowHint(UI(Tr(L"Fallen Angel mode is on (menu ≡)")));
    } else {
        if (edit.autoCorrect == AutoCorrectMode::Off) {
            RowHint(UI(Tr(L"No words are corrected automatically.")));
        } else if (edit.autoCorrect == AutoCorrectMode::Safe) {
            RowHint(UI(Tr(L"Only Windows' sure spelling fixes are applied.")));
        } else if (edit.autoCorrect == AutoCorrectMode::Phone) {
            RowHint(UI(Tr(L"Misspelled words are corrected when you press Space.")));
        } else if (edit.autoCorrect == AutoCorrectMode::Strong) {
            RowHint(UI(Tr(L"Every word the dictionary does not know becomes its first suggestion when you go on writing – Backspace takes it back. Words still standing wrong are underlined; a click corrects them. GW2 terms, names and what you taught stay. It needs Windows' dictionary for the language you write in.")));
        }
    }

    if (s_showConsentDialog) {
        ImGui::OpenPopup((UI(Tr(L"Privacy Notice")) + "##lt_consent").c_str());
    }

    if (ImGui::BeginPopupModal((UI(Tr(L"Privacy Notice")) + "##lt_consent").c_str(), &s_showConsentDialog, ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::wstring normUrl = NormalizeLanguageToolUrl(edit.languageToolUrl);
        const std::wstring notice = TrF(L"Strong uses LanguageTool online for grammar: your own text (never other players' lines) goes to {1}.", {normUrl});
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 25.0f);
        ImGui::TextWrapped("%s", UI(notice).c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button(UI(Tr(L"OK")).c_str(), ImVec2(ImGui::GetFontSize() * 6.0f, 0))) {
            edit.languageToolConsent = normUrl;
            edit.SaveValue(L"Spelling", L"LanguageToolConsent", normUrl);
            s_showConsentDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(UI(Tr(L"Offline only")).c_str(), ImVec2(ImGui::GetFontSize() * 7.0f, 0))) {
            edit.languageToolConsent = L"offline";
            edit.SaveValue(L"Spelling", L"LanguageToolConsent", L"offline");
            s_showConsentDialog = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    EndBox();
    BeginBox(UI(Tr(L"Learning")));
    RowLabel(UI(Tr(L"Learn")));
    int learn = !edit.learnWords ? 2 : edit.learnLater ? 1 : 0;
    if (RowCombo("##learn", {UI(Tr(L"Learn from what I send – while playing")), UI(Tr(L"Learn from what I send – later")),
                             UI(Tr(L"Do not learn"))}, &learn)) {
        edit.learnWords = learn != 2;
        edit.learnLater = learn == 1;
    }
    if (!s_forgetAsked) {
        if (ImGui::Button(UI(Tr(L"Delete everything learned…")).c_str())) s_forgetAsked = true;
    } else {
        ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.35f, 1.f), "%s", UI(Tr(L"Really delete everything learned?")).c_str());
        ImGui::SameLine();
        if (ImGui::Button(UI(Tr(L"Yes")).c_str())) {
            g_worker.Post([] { if (g_spell) g_spell->ForgetAll(); });
            s_forgetAsked = false;
        }
        ImGui::SameLine();
        if (ImGui::Button(UI(Tr(L"No")).c_str())) s_forgetAsked = false;
    }
    EndBox();
}

}  // namespace kb
