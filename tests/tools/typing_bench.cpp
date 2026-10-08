// typing_bench.cpp — measures the typing help the way a player would use it, with the app's own logic (SpellService:
// word model, completions, the grey suggestion, the sure next word and phrase, autocorrection) and the real Windows
// spell checker.
//
// A simulated player types invented but typical GW2 chat messages letter by letter. Before a word it takes the grey
// next word or phrase (Tab) when it is exactly what comes; while typing it takes the grey completion (Space) or
// tabs to the right suggestion when that is cheaper than typing on; otherwise it types. When it typed a word
// correctly and Space or Enter would change it into another word, that is a SURPRISE (it then presses Backspace,
// which undoes it) – the thing the typing help must never do. After each message the tool learns, as in the app.
//
//   typing_bench.exe [messages per language]      (default 600)
//
// Reports per block of messages: key presses saved, surprises, how often the grey word was right.
#include <windows.h>

#include <cstdio>
#include <cwctype>
#include <map>
#include <string>
#include <vector>

#include "app/spell_service.hpp"
#include "core/text.hpp"
#include "win/files.hpp"

using namespace gct;

namespace {

struct Line {
    const wchar_t* text;
    int weight;  // how often a player writes it (dungeon calls and greetings: very often)
};

// Invented messages in the style of the GW2 chat (no real player's chat).
const Line kGerman[] = {
    {L"hallo zusammen", 30}, {L"hi", 20}, {L"danke", 25}, {L"danke dir", 15}, {L"gg", 20}, {L"gute nacht", 8},
    {L"gute nacht bis morgen", 6}, {L"bin gleich da", 12}, {L"bin gleich wieder da", 8}, {L"kurz afk", 10},
    {L"wer kommt mit zu tequatl", 6}, {L"kommst du mit", 10}, {L"kommst du mit in den dungeon", 6},
    {L"ich lade dich ein", 8}, {L"lade mich bitte ein", 6}, {L"use tp", 15}, {L"bitte alle stacken", 8},
    {L"cc auf den boss", 10}, {L"breakbar jetzt", 8}, {L"wartet kurz", 10}, {L"wartet kurz ich muss noch reparieren", 4},
    {L"sorry mein fehler", 6}, {L"kein problem", 10}, {L"alles gut", 12}, {L"wo seid ihr", 8},
    {L"ich bin am wegpunkt", 6}, {L"könnt ihr mich wiederbeleben", 4}, {L"danke fürs wiederbeleben", 4},
    {L"wie viele brauchen noch den erfolg", 3}, {L"ich mache den weg frei", 3}, {L"nehmt die abkürzung links", 3},
    {L"der boss ist gleich tot", 3}, {L"gleich kommt die nächste welle", 3}, {L"habt ihr noch platz in der gruppe", 4},
    {L"ich suche noch eine gruppe für fraktale", 4}, {L"heute abend wieder raid", 3}, {L"bis morgen", 8},
    {L"schönen abend noch", 5}, {L"wie geht es dir", 5}, {L"mir geht es gut danke", 4}, {L"was machst du gerade", 4},
    {L"ich farme gerade materialien", 3}, {L"willst du in die gilde", 3}, {L"ich schicke dir eine einladung", 3},
    {L"lass uns morgen weitermachen", 3}, {L"ich muss jetzt los", 5}, {L"bis später", 6}, {L"ja klar", 10},
    {L"nein danke", 5}, {L"vielleicht später", 4}, {L"klingt gut", 6}, {L"super gemacht", 5}, {L"gut gemacht alle", 5},
};

const Line kEnglish[] = {
    {L"hello everyone", 25}, {L"hi", 20}, {L"thanks", 25}, {L"thank you", 15}, {L"gg", 20}, {L"good night", 8},
    {L"good night see you tomorrow", 6}, {L"brb", 12}, {L"be right back", 6}, {L"afk for a moment", 8},
    {L"anyone for tequatl", 6}, {L"are you coming", 10}, {L"are you coming to the dungeon", 6}, {L"i will invite you", 8},
    {L"please invite me", 6}, {L"use tp", 15}, {L"everyone stack please", 8}, {L"cc the boss", 10},
    {L"break the bar now", 8}, {L"wait a moment", 10}, {L"wait a moment i need to repair", 4}, {L"sorry my bad", 6},
    {L"no problem", 10}, {L"all good", 12}, {L"where are you", 8}, {L"i am at the waypoint", 6},
    {L"can you revive me", 4}, {L"thanks for the revive", 4}, {L"how many still need the achievement", 3},
    {L"take the shortcut on the left", 3}, {L"the boss is almost dead", 3}, {L"next wave is coming", 3},
    {L"do you have room in the group", 4}, {L"looking for a group for fractals", 4}, {L"raid again tonight", 3},
    {L"see you tomorrow", 8}, {L"have a nice evening", 5}, {L"how are you", 5}, {L"i am fine thanks", 4},
    {L"what are you doing", 4}, {L"farming materials right now", 3}, {L"do you want to join the guild", 3},
    {L"i will send you an invite", 3}, {L"lets continue tomorrow", 3}, {L"i have to go now", 5}, {L"see you later", 6},
    {L"yes sure", 10}, {L"no thanks", 5}, {L"maybe later", 4}, {L"sounds good", 6}, {L"well done", 5},
    {L"well done everyone", 5},
};

struct Stats {
    long keys = 0, letters = 0;      // presses with the help / letters (incl. spaces) of the messages
    int surprises = 0;               // a correct word changed by Space / Enter
    int offers = 0, offersRight = 0; // grey word shown when a word was finished by typing / it was the right one
    int nextTaken = 0, phraseTaken = 0, completionTaken = 0, tabbed = 0;
    std::map<std::wstring, int> surpriseList;
};

std::vector<std::wstring> Split(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring cur;
    for (wchar_t c : s) {
        if (c == L' ') {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// One message, typed by the simulated player.
void TypeMessage(SpellService& sp, const std::wstring& message, AutoCorrectMode mode, Stats& st) {
    const std::vector<std::wstring> words = Split(message);
    std::wstring text;
    st.letters += static_cast<long>(message.size());
    size_t wi = 0;
    while (wi < words.size()) {
        const std::wstring& w = words[wi];
        // Before the word: the grey next word or phrase (Tab), when it is exactly what comes.
        if (!text.empty()) {
            const WordSuggestions s = sp.Suggestions(text, text.size(), mode);
            if (s.kind == WordSuggestions::Kind::Next && s.autoIndex == 0 && !s.words.empty()) {
                const std::vector<std::wstring> ph = Split(s.phrase);
                bool phraseFits = ph.size() >= 2 && wi + ph.size() <= words.size();
                for (size_t k = 0; phraseFits && k < ph.size(); ++k) phraseFits = ph[k] == words[wi + k];
                if (phraseFits) {
                    ++st.keys;  // Tab: the whole rest
                    ++st.phraseTaken;
                    sp.Chose(text, text.size(), ph[0], true);
                    for (const auto& p : ph) text += p + L" ";
                    wi += ph.size();
                    continue;
                }
                if (s.words[0] == w) {
                    ++st.keys;  // Tab
                    ++st.nextTaken;
                    sp.Chose(text, text.size(), w, true);
                    text += w + L" ";
                    ++wi;
                    continue;
                }
            }
        }
        // Letter by letter; the grey completion (Space) or a few Tabs when cheaper than typing on.
        const size_t start = text.size();
        bool done = false;
        for (size_t i = 0; i < w.size() && !done; ++i) {
            text += w[i];
            ++st.keys;
            if (i + 1 == w.size()) break;
            const WordChoices c = sp.Choices(text, text.size(), mode);
            if (c.words.empty()) continue;
            int at = -1;
            for (size_t k = 0; k < c.words.size(); ++k)
                if (c.words[k] == w) at = static_cast<int>(k);
            if (at < 0) continue;
            const int n = static_cast<int>(c.words.size());
            const int tabs = c.highlight < 0 ? at + 1 : (at - c.highlight + n) % n;
            const int rest = static_cast<int>(w.size() - i - 1);  // letters still to type
            if (tabs + 1 <= rest) {  // Tabs + Space against typing on (+ the space typed anyway)
                st.keys += tabs + 1;
                if (tabs == 0) ++st.completionTaken;
                else ++st.tabbed;
                sp.Chose(text, start, w, tabs > 0 || c.highlight > 0);
                text = text.substr(0, start) + w + L" ";
                done = true;
            }
        }
        ++wi;
        if (done) continue;
        // Typed in full: the space (or Enter at the end) must not change the word.
        const WordChoices c = sp.Choices(text, text.size(), mode);
        if (c.Changes()) {
            ++st.offers;
            const std::wstring got = c.words[static_cast<size_t>(c.highlight)];
            if (got == w) {
                ++st.offersRight;
            } else {
                ++st.surprises;  // Space would write `got`; Backspace undoes it
                ++st.surpriseList[w + L" -> " + got];
                ++st.keys;
            }
        } else if (const auto fix = sp.AutoCorrection(w, mode); fix && *fix != w) {
            ++st.surprises;
            ++st.surpriseList[w + L" -> " + *fix];
            ++st.keys;
        }
        if (wi < words.size()) {
            text += L' ';
            ++st.keys;
        }
    }
    sp.Learn(message);
}

void Run(const wchar_t* lang, const wchar_t* tag, const Line* lines, size_t count, int messages) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring dir = std::wstring(tmp) + L"gct_typing_bench_" + lang;
    CreateDirectoryW(dir.c_str(), nullptr);
    DeleteFileW((dir + L"\\learned_" + lang + L".txt").c_str());
    SpellService sp;
    const bool spell = sp.Init({tag, lang}, dir + L"\\words.txt");
    sp.UseLearnedLanguage(lang, dir);
    std::printf("\n=== %ls (Windows spell checker: %s) ===\n", lang, spell ? "yes" : "no");
    std::printf("%-12s %8s %9s %10s %8s %6s %6s %6s %5s\n", "messages", "saved %", "surprise", "grey right", "next",
                "phrase", "compl", "tab", "");
    int total = 0;
    for (const Line* l = lines; l < lines + count; ++l) total += l->weight;
    unsigned seed = 12345;
    Stats block, all;
    for (int m = 1; m <= messages; ++m) {
        seed = seed * 1103515245u + 12345u;
        int pick = static_cast<int>((seed >> 8) % static_cast<unsigned>(total));
        const Line* l = lines;
        while (pick >= l->weight) pick -= (l++)->weight;
        TypeMessage(sp, l->text, AutoCorrectMode::Phone, block);
        if (m % 100 == 0 || m == messages) {
            std::printf("%5d-%-6d %7.1f%% %9d %9.0f%% %8d %6d %6d %6d\n", m - (m - 1) % 100, m,
                        100.0 * (1.0 - static_cast<double>(block.keys) / block.letters), block.surprises,
                        block.offers ? 100.0 * block.offersRight / block.offers : 0.0, block.nextTaken,
                        block.phraseTaken, block.completionTaken, block.tabbed);
            all.keys += block.keys;
            all.letters += block.letters;
            all.surprises += block.surprises;
            for (const auto& [k, v] : block.surpriseList) all.surpriseList[k] += v;
            block = Stats();
        }
    }
    std::printf("total: %.1f %% key presses saved, %d surprises\n", 100.0 * (1.0 - static_cast<double>(all.keys) / all.letters),
                all.surprises);
    for (const auto& [k, v] : all.surpriseList) std::printf("  surprise %3dx  %s\n", v, ToUtf8(k).c_str());
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const int messages = argc > 1 ? std::max(100, _wtoi(argv[1])) : 600;
    Run(L"de", L"de-DE", kGerman, std::size(kGerman), messages);
    Run(L"en", L"en-US", kEnglish, std::size(kEnglish), messages);
    return 0;
}
