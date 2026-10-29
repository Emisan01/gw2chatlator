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

// The same kind of messages in the other UI languages (invented, the way players write in chat: lower case, short).
const Line kFrench[] = {
    {L"salut tout le monde", 30}, {L"salut", 20}, {L"merci", 25}, {L"merci beaucoup", 15}, {L"gg", 20}, {L"bonne nuit", 8},
    {L"bonne nuit à demain", 6}, {L"je reviens tout de suite", 8}, {L"brb", 10}, {L"afk deux minutes", 8},
    {L"qui vient pour tequatl", 6}, {L"tu viens avec nous", 10}, {L"tu viens dans le donjon", 6}, {L"je t'invite", 8},
    {L"invite moi s'il te plaît", 6}, {L"utilisez le tp", 15}, {L"tout le monde se regroupe", 8}, {L"cc sur le boss", 10},
    {L"cassez la barre maintenant", 8}, {L"attendez un peu", 10}, {L"attendez je dois réparer", 4},
    {L"désolé mon erreur", 6}, {L"pas de problème", 10}, {L"tout va bien", 12}, {L"vous êtes où", 8},
    {L"je suis au point de passage", 6}, {L"vous pouvez me relever", 4}, {L"merci de m'avoir relevé", 4},
    {L"qui a encore besoin du succès", 3}, {L"prenez le raccourci à gauche", 3}, {L"le boss est presque mort", 3},
    {L"la prochaine vague arrive", 3}, {L"il reste de la place dans le groupe", 4},
    {L"je cherche un groupe pour les fractales", 4}, {L"raid ce soir", 3}, {L"à demain", 8}, {L"bonne soirée", 5},
    {L"ça va", 5}, {L"ça va bien merci", 4}, {L"tu fais quoi", 4}, {L"je farme des matériaux", 3},
    {L"tu veux rejoindre la guilde", 3}, {L"je t'envoie une invitation", 3}, {L"on continue demain", 3},
    {L"je dois y aller", 5}, {L"à plus tard", 6}, {L"oui bien sûr", 10}, {L"non merci", 5}, {L"peut-être plus tard", 4},
    {L"ça me va", 6}, {L"bien joué", 5}, {L"bien joué tout le monde", 5},
};

const Line kSpanish[] = {
    {L"hola a todos", 30}, {L"hola", 20}, {L"gracias", 25}, {L"muchas gracias", 15}, {L"gg", 20}, {L"buenas noches", 8},
    {L"buenas noches hasta mañana", 6}, {L"ahora vuelvo", 8}, {L"brb", 10}, {L"afk un momento", 8},
    {L"quién viene a tequatl", 6}, {L"vienes con nosotros", 10}, {L"vienes a la mazmorra", 6}, {L"te invito", 8},
    {L"invítame por favor", 6}, {L"usad el tp", 15}, {L"todos juntos por favor", 8}, {L"cc al jefe", 10},
    {L"romped la barra ahora", 8}, {L"esperad un momento", 10}, {L"esperad tengo que reparar", 4},
    {L"perdón fue mi culpa", 6}, {L"no pasa nada", 10}, {L"todo bien", 12}, {L"dónde estáis", 8},
    {L"estoy en el punto de ruta", 6}, {L"me podéis revivir", 4}, {L"gracias por revivirme", 4},
    {L"quién necesita todavía el logro", 3}, {L"tomad el atajo de la izquierda", 3}, {L"el jefe está casi muerto", 3},
    {L"viene la siguiente oleada", 3}, {L"queda sitio en el grupo", 4}, {L"busco grupo para fractales", 4},
    {L"raid esta noche", 3}, {L"hasta mañana", 8}, {L"buena noche a todos", 5}, {L"qué tal", 5}, {L"bien gracias", 4},
    {L"qué haces", 4}, {L"estoy farmeando materiales", 3}, {L"quieres unirte al clan", 3},
    {L"te mando una invitación", 3}, {L"seguimos mañana", 3}, {L"me tengo que ir", 5}, {L"hasta luego", 6},
    {L"sí claro", 10}, {L"no gracias", 5}, {L"quizás más tarde", 4}, {L"me parece bien", 6}, {L"bien hecho", 5},
    {L"bien hecho todos", 5},
};

const Line kPolish[] = {
    {L"cześć wszystkim", 30}, {L"cześć", 20}, {L"dzięki", 25}, {L"dziękuję bardzo", 15}, {L"gg", 20}, {L"dobranoc", 8},
    {L"dobranoc do jutra", 6}, {L"zaraz wracam", 8}, {L"brb", 10}, {L"afk chwilę", 8}, {L"kto idzie na tequatla", 6},
    {L"idziesz z nami", 10}, {L"idziesz do lochu", 6}, {L"zapraszam cię", 8}, {L"zaproś mnie proszę", 6},
    {L"użyjcie tp", 15}, {L"wszyscy razem proszę", 8}, {L"cc na bossa", 10}, {L"zbijcie pasek teraz", 8},
    {L"poczekajcie chwilę", 10}, {L"poczekajcie muszę naprawić", 4}, {L"sorry mój błąd", 6}, {L"nie ma problemu", 10},
    {L"wszystko dobrze", 12}, {L"gdzie jesteście", 8}, {L"jestem przy punkcie podróży", 6},
    {L"możecie mnie wskrzesić", 4}, {L"dzięki za wskrzeszenie", 4}, {L"kto jeszcze potrzebuje osiągnięcia", 3},
    {L"idźcie na skrót w lewo", 3}, {L"boss zaraz padnie", 3}, {L"nadchodzi następna fala", 3},
    {L"jest jeszcze miejsce w grupie", 4}, {L"szukam grupy na fraktale", 4}, {L"rajd dziś wieczorem", 3},
    {L"do jutra", 8}, {L"miłego wieczoru", 5}, {L"jak się masz", 5}, {L"dobrze dzięki", 4}, {L"co robisz", 4},
    {L"farmię materiały", 3}, {L"chcesz dołączyć do gildii", 3}, {L"wyślę ci zaproszenie", 3},
    {L"kontynuujemy jutro", 3}, {L"muszę już iść", 5}, {L"na razie", 6}, {L"tak jasne", 10}, {L"nie dzięki", 5},
    {L"może później", 4}, {L"brzmi dobrze", 6}, {L"dobra robota", 5}, {L"dobra robota wszyscy", 5},
};

const Line kPortuguese[] = {
    {L"oi pessoal", 30}, {L"oi", 20}, {L"obrigado", 25}, {L"muito obrigado", 15}, {L"gg", 20}, {L"boa noite", 8},
    {L"boa noite até amanhã", 6}, {L"já volto", 8}, {L"brb", 10}, {L"afk um minuto", 8}, {L"quem vem pro tequatl", 6},
    {L"você vem com a gente", 10}, {L"você vem pra masmorra", 6}, {L"vou te convidar", 8}, {L"me convida por favor", 6},
    {L"usem o tp", 15}, {L"todo mundo junto por favor", 8}, {L"cc no chefe", 10}, {L"quebrem a barra agora", 8},
    {L"esperem um pouco", 10}, {L"esperem preciso consertar", 4}, {L"desculpa foi mal", 6}, {L"sem problema", 10},
    {L"tudo bem", 12}, {L"cadê vocês", 8}, {L"estou no ponto de teleporte", 6}, {L"podem me reviver", 4},
    {L"obrigado por me reviver", 4}, {L"quem ainda precisa da conquista", 3}, {L"peguem o atalho à esquerda", 3},
    {L"o chefe está quase morto", 3}, {L"a próxima onda está vindo", 3}, {L"ainda tem vaga no grupo", 4},
    {L"procuro grupo para fractais", 4}, {L"raide hoje à noite", 3}, {L"até amanhã", 8}, {L"boa noite pra todos", 5},
    {L"tudo bem com você", 5}, {L"tudo ótimo obrigado", 4}, {L"o que você está fazendo", 4},
    {L"estou farmando materiais", 3}, {L"quer entrar na guilda", 3}, {L"vou te mandar um convite", 3},
    {L"continuamos amanhã", 3}, {L"preciso ir agora", 5}, {L"até mais", 6}, {L"sim claro", 10}, {L"não obrigado", 5},
    {L"talvez mais tarde", 4}, {L"parece bom", 6}, {L"mandou bem", 5}, {L"mandou bem pessoal", 5},
};

const Line kRussian[] = {
    {L"всем привет", 30}, {L"привет", 20}, {L"спасибо", 25}, {L"большое спасибо", 15}, {L"gg", 20},
    {L"спокойной ночи", 8}, {L"спокойной ночи до завтра", 6}, {L"сейчас вернусь", 8}, {L"brb", 10}, {L"afk минуту", 8},
    {L"кто идёт на текватла", 6}, {L"ты идёшь с нами", 10}, {L"ты идёшь в подземелье", 6}, {L"я тебя приглашу", 8},
    {L"пригласи меня пожалуйста", 6}, {L"используйте тп", 15}, {L"все в кучу пожалуйста", 8},
    {L"контроль на босса", 10}, {L"сбивайте полоску сейчас", 8}, {L"подождите немного", 10},
    {L"подождите мне надо починиться", 4}, {L"извините моя ошибка", 6}, {L"без проблем", 10}, {L"всё хорошо", 12},
    {L"где вы", 8}, {L"я у точки телепорта", 6}, {L"можете меня поднять", 4}, {L"спасибо что подняли", 4},
    {L"кому ещё нужно достижение", 3}, {L"срежьте путь слева", 3}, {L"босс почти мёртв", 3},
    {L"идёт следующая волна", 3}, {L"есть место в группе", 4}, {L"ищу группу на фракталы", 4},
    {L"рейд сегодня вечером", 3}, {L"до завтра", 8}, {L"хорошего вечера", 5}, {L"как дела", 5}, {L"хорошо спасибо", 4},
    {L"что делаешь", 4}, {L"фармлю материалы", 3}, {L"хочешь в гильдию", 3}, {L"отправлю тебе приглашение", 3},
    {L"продолжим завтра", 3}, {L"мне пора идти", 5}, {L"до скорого", 6}, {L"да конечно", 10}, {L"нет спасибо", 5},
    {L"может позже", 4}, {L"звучит хорошо", 6}, {L"молодцы", 5}, {L"молодцы все", 5},
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
            if (got == w || ToLowerAscii(got) == ToLowerAscii(w)) {
                ++st.offersRight;
            } else {
                ++st.surprises;  // Space would write `got`; Backspace undoes it
                ++st.surpriseList[w + L" -> " + got];
                ++st.keys;
            }
        } else if (const auto fix = sp.AutoCorrection(w, mode); fix && *fix != w && ToLowerAscii(*fix) != ToLowerAscii(w)) {
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

// Returns the surprises of this language (the gate wants none in any language).
int Run(const wchar_t* lang, const wchar_t* tag, const Line* lines, size_t count, int messages) {
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
    return all.surprises;
}

// typing_bench --probe de-DE "Sch" "wie g": what a fresh tool offers for these inputs (nothing learned).
// typing_bench --profile de-DE texts.txt "shon" …: the same after learning your own texts as a typing profile
// (prints what was learned, incl. the typos found – your texts stay on your PC, keep them in local/).
void Probe(int argc, wchar_t** argv) {
    const bool profile = std::wstring(argv[1]) == L"--profile";
    const std::wstring tag = argv[2];
    const std::wstring lang = tag.substr(0, 2);
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    const std::wstring dir = std::wstring(tmp) + L"gct_typing_probe";
    CreateDirectoryW(dir.c_str(), nullptr);
    DeleteFileW((dir + L"\\learned_" + lang + L".txt").c_str());
    DeleteFileW((dir + L"\\typos_" + lang + L".txt").c_str());
    SpellService sp;
    sp.Init({tag, lang}, dir + L"\\words.txt");
    sp.UseLearnedLanguage(lang, dir);
    int first = 3;
    if (profile && argc > 3) {
        std::string data;
        ReadFileBytes(argv[3], data);
        const auto t0 = GetTickCount64();
        const SpellService::ProfileResult r = sp.LearnFromText(FromUtf8(data));
        std::printf("profile: %zu sentences, %zu new words, %zu typos (%llu ms)\n", r.sentences, r.newWords, r.typos,
                    GetTickCount64() - t0);
        std::printf("%s\n", sp.Typos().Serialize().c_str());
        first = 4;
    }
    for (int i = first; i < argc; ++i) {
        const std::wstring text = argv[i];
        const auto t0 = GetTickCount64();
        const WordSuggestions s = sp.Suggestions(text, text.size(), AutoCorrectMode::Phone);
        const WordChoices c = sp.Choices(text, text.size(), AutoCorrectMode::Phone);
        std::wstring bar, ch;
        for (const std::wstring& w : s.words) bar += L"[" + w + L"] ";
        for (size_t k = 0; k < c.words.size(); ++k)
            ch += (static_cast<int>(k) == c.highlight ? L"*" : L"") + c.words[k] + L" ";
        std::printf("%-16s bar: %-40s grey/Tab: %s (%llu ms)\n", ToUtf8(text).c_str(), ToUtf8(bar).c_str(),
                    ToUtf8(ch).c_str(), GetTickCount64() - t0);
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (argc > 2 && (std::wstring(argv[1]) == L"--probe" || std::wstring(argv[1]) == L"--profile")) {
        Probe(argc, argv);
        return 0;
    }
    const int messages = argc > 1 ? std::max(100, _wtoi(argv[1])) : 600;
    struct Lang {
        const wchar_t *lang, *tag;
        const Line* lines;
        size_t count;
    };
    const Lang langs[] = {{L"de", L"de-DE", kGerman, std::size(kGerman)},        {L"en", L"en-US", kEnglish, std::size(kEnglish)},
                          {L"fr", L"fr-FR", kFrench, std::size(kFrench)},        {L"es", L"es-ES", kSpanish, std::size(kSpanish)},
                          {L"pl", L"pl-PL", kPolish, std::size(kPolish)},        {L"pt", L"pt-BR", kPortuguese, std::size(kPortuguese)},
                          {L"ru", L"ru-RU", kRussian, std::size(kRussian)}};
    int surprises = 0;
    for (const Lang& l : langs) surprises += Run(l.lang, l.tag, l.lines, l.count, messages);
    // One line for the gate: a surprise in any language fails it (the exit code says so too).
    std::printf("\ntyping_bench: %zu languages, %s\n", std::size(langs),
                surprises == 0 ? "no surprise in any language" : (std::to_string(surprises) + " SURPRISES").c_str());
    return surprises == 0 ? 0 : 1;
}
