// Portable unit tests for src/core — no Windows needed:
//   g++ -std=c++17 -I src tests/core_tests.cpp src/core/*.cpp -o core_tests && ./core_tests
#include <chrono>
#include <cmath>
#include <map>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "core/chat_line.hpp"
#include "core/cloud_mt_protocol.hpp"
#include "core/my_words.hpp"
#include "core/deepl_protocol.hpp"
#include "core/gw2_text.hpp"
#include "core/names.hpp"
#include "core/languages.hpp"
#include "core/llm_protocol.hpp"
#include "core/mumble.hpp"
#include "core/mymemory_protocol.hpp"
#include "core/glossary.hpp"
#include "core/i18n.hpp"
#include "core/languagetool_protocol.hpp"
#include "core/typo_memory.hpp"
#include "core/word_model.hpp"
#include "core/hotkey.hpp"
#include "core/json.hpp"
#include "core/langs.hpp"
#include "core/protect.hpp"
#include "core/slang.hpp"
#include "core/text.hpp"
#include "core/translator.hpp"
#include "core/voice.hpp"
#include "core/voice_learn.hpp"
#include "core/send_guard.hpp"
#include "core/chat_code.hpp"

using namespace gct;

static int g_fail = 0, g_pass = 0;

#define CHECK(...)                                                                    \
    do {                                                                               \
        if (__VA_ARGS__) {                                                                  \
            ++g_pass;                                                                  \
        } else {                                                                       \
            ++g_fail;                                                                  \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #__VA_ARGS__);                \
        }                                                                              \
    } while (0)

// ---------------------------------------------------------------------------
static void TestUtf() {
    const std::wstring w = L"Gr\u00fc\u00dfe \u4f60\u597d \U0001F600 \u00df";
    CHECK(FromUtf8(ToUtf8(w)) == w);
    CHECK(ToUtf8(L"\u00e4") == "\xC3\xA4");
    CHECK(CodePointCount(L"a\U0001F600b") == 3);
    CHECK(CodePointCount(L"\u4f60\u597d") == 2);
    const std::wstring bad = FromUtf8(std::string("a\xFF" "b\xE2\x82", 5));
    CHECK(bad.size() >= 3 && bad[0] == L'a' && bad[1] == 0xFFFD);
}

static void TestText() {
    CHECK(SanitizeChatText(L"  hallo\r\nwelt\t ! ") == L"hallo welt !");
    CHECK(SanitizeChatText(L"a   b") == L"a b");
    // Security hardening: Bidi overrides, Trojan Source, zero-width spaces stripped
    CHECK(SanitizeChatText(L"safe\u202Ereversed\u202Dtext") == L"safereversedtext");
    CHECK(SanitizeChatText(L"zero\u200Bwidth\uFEFFspace\u00ADhyphen") == L"zerowidthspacehyphen");
    CHECK(Trim(L"\u3000 x \u00a0") == L"x");
    const auto langs = ParseLangList(L"en-gb, fr ,, ZH-hans ");
    CHECK(langs.size() == 3 && langs[0] == L"EN-GB" && langs[1] == L"FR" && langs[2] == L"ZH-HANS");

    CHECK(CaseFold(L"L\u00d6WENSTEIN \u0139 \u0179") == L"l\u00f6wenstein \u013a \u017a");
    CHECK(CaseFoldChar(L'ß') == L'ß');

    const std::wstring s = L"Lion's Arch, Ele!";
    const auto words = WordSpans(s);
    CHECK(words.size() == 3);
    CHECK(s.substr(words[0].start, words[0].length) == L"Lion's");
    CHECK(s.substr(words[2].start, words[2].length) == L"Ele");
}

static void TestChat() {
    auto s = SplitChatCommand(L"hallo zusammen");
    CHECK(s.prefix.empty() && s.body == L"hallo zusammen");
    s = SplitChatCommand(L"/p hallo");
    CHECK(s.prefix == L"/p " && s.body == L"hallo");
    s = SplitChatCommand(L"/d Kommandeur braucht Hilfe");
    CHECK(s.prefix == L"/d " && s.body == L"Kommandeur braucht Hilfe");
    s = SplitChatCommand(L"/wave");
    CHECK(s.prefix == L"/wave" && s.body.empty());
    s = SplitChatCommand(L"/w Emi.1234 hallo, wie geht's?");
    CHECK(s.prefix == L"/w Emi.1234 " && s.body == L"hallo, wie geht's?");
    s = SplitChatCommand(L"/w Some Name, hallo du");
    CHECK(s.prefix == L"/w Some Name, " && s.body == L"hallo du");
    s = SplitChatCommand(L"/w Somebody hi");
    CHECK(s.prefix == L"/w Somebody " && s.body == L"hi");
    s = SplitChatCommand(L"/W Emi.1234");
    CHECK(s.prefix == L"/W Emi.1234" && s.body.empty());

    CHECK(IsAccountName(L"Emi.1234"));
    CHECK(!IsAccountName(L"Emi.12a4"));

    const std::wstring t = L"Treffpunkt [&BDAEAAA=] und [&ab] und [&AgH1WQAA]";
    const auto codes = FindChatCodes(t);
    CHECK(codes.size() == 2);
    CHECK(t.substr(codes[0].start, codes[0].length) == L"[&BDAEAAA=]");
    CHECK(t.substr(codes[1].start, codes[1].length) == L"[&AgH1WQAA]");
}

static void TestHotkey() {
    // What the settings' hotkey field stores reads back the same.
    for (const wchar_t* k : {L"Ctrl+Alt+Shift+T", L"Ctrl+F9", L"Alt+Numpad5", L"Ctrl+Shift+PageUp"}) {
        const auto hk = ParseHotkey(k);
        CHECK(hk && FormatHotkey(*hk) == k);
    }
    CHECK(FormatHotkey(Hotkey{kModCtrl, 0xBA}).empty());  // a key the parser does not know
    auto h = ParseHotkey(L"Ctrl+Alt+T");
    CHECK(h && h->mods == (kModCtrl | kModAlt) && h->vk == 0x54);
    h = ParseHotkey(L"strg + umschalt + 1");
    CHECK(h && h->mods == (kModCtrl | kModShift) && h->vk == 0x31);
    h = ParseHotkey(L"F9");
    CHECK(h && h->mods == 0 && h->vk == 0x78);
    h = ParseHotkey(L"Alt+F24");
    CHECK(h && h->vk == 0x87);
    CHECK(!ParseHotkey(L"T"));
    CHECK(!ParseHotkey(L"Ctrl+"));
    CHECK(!ParseHotkey(L"Hyper+T"));
    CHECK(!ParseHotkey(L"F25"));
    CHECK(!ParseHotkey(L""));
}

static void TestJson() {
    CHECK(JsonEscape("a\"b\\c\nd\x01") == "a\\\"b\\\\c\\nd\\u0001");
    JsonValue v;
    CHECK(ParseJson(R"({"translations":[{"detected_source_language":"DE","text":"Hello ä 😀 \"q\""}]})", v));
    const JsonValue* tr = v.Get("translations");
    CHECK(tr && tr->arr.size() == 1);
    CHECK(tr && tr->arr[0].GetString("text") == "Hello \xC3\xA4 \xF0\x9F\x98\x80 \"q\"");
    CHECK(!ParseJson("{\"a\":1", v));
    CHECK(!ParseJson("{} trailing", v));
    CHECK(ParseJson(" [1, -2.5e3, true, false, null, \"x\"] ", v) && v.arr.size() == 6);
    CHECK(v.arr[1].n == -2500.0);
    CHECK(ParseJson(R"({"elite":true})", v) && v.GetBool("elite") && !v.GetBool("missing"));
}

static void TestLangs() {
    CHECK(PrimaryLang(L"en-gb") == L"EN");
    CHECK(Gw2ApiLang(L"EN-GB") == "en");
    CHECK(Gw2ApiLang(L"ZH-HANS") == "zh");
    CHECK(Gw2ApiLang(L"IT").empty());
    auto tags = SpellTagCandidates(L"DE", L"de-AT");
    CHECK(!tags.empty() && tags[0] == L"de-AT");
    CHECK(tags.size() >= 2 && tags[1] == L"de-DE");
    tags = SpellTagCandidates(L"EN-GB", L"de-DE");
    CHECK(!tags.empty() && tags[0] == L"en-GB");
    tags = SpellTagCandidates(L"ZH-HANS", L"de-DE");
    CHECK(!tags.empty() && tags[0] == L"zh");  // script code is not a region
}

// ---------------------------------------------------------------------------
static NameTable De() {
    return {{"map:15", L"K\u00f6nigintal"},       {"map:50", L"L\u00f6wenstein"},  {"map:51", L"L\u00f6wenstein"},
            {"wvw:38-1", L"Schloss Steinnebel"}, {"wvw:38-2", L"Steinnebel"}, {"prof:Elementalist", L"Elementarmagier"},
            {"map:99", L"Arena"},            {"map:98", L"Bruch"},       {"map:97", L"Bruch"},
            {"map:1", L"Ost"},               {"map:2", L"(Platzhalter)"}};
}
static NameTable En() {
    return {{"map:15", L"Queensdale"},        {"map:50", L"Lion's Arch"}, {"map:51", L"Lion's Arch"},
            {"wvw:38-1", L"Stonemist Castle"}, {"wvw:38-2", L"Stonemist"}, {"prof:Elementalist", L"Elementalist"},
            {"map:99", L"Arena"},             {"map:98", L"Breach"},      {"map:97", L"Fracture"},
            {"map:1", L"East"},               {"map:2", L"(Placeholder)"}};
}

static void TestNameTable() {
    NameTable t{{"map:1", L"A\tB"}, {"map:2", L"L\u00f6wenstein"}};
    const std::string ser = SerializeNameTable(t);
    CHECK(ser.find("map:1\tA B\n") != std::string::npos);
    const NameTable back = ParseNameTable(ser + "garbage-without-tab\n\r\n");
    CHECK(back.size() == 2 && back.at("map:2") == L"L\u00f6wenstein");
}

static void TestGlossary() {
    const Glossary g = Glossary::Build(De(), En());
    // Löwenstein (dup, same target) yes; Bruch (ambiguous) no; Ost (too short) no; placeholder no.
    CHECK(g.Size() == 6);

    const std::wstring text = L"Treffen in L\u00d6WENSTEIN, dann Schloss Steinnebel und L\u00f6wensteiner Bruch";
    const auto hits = g.FindAll(text);
    CHECK(hits.size() == 2);
    CHECK(hits.size() == 2 && hits[0].target == L"Lion's Arch");
    CHECK(hits.size() == 2 && hits[1].target == L"Stonemist Castle");  // longest first, not "Stonemist"
    CHECK(hits.size() == 2 && text.substr(hits[1].span.start, hits[1].span.length) == L"Schloss Steinnebel");

    // Blocked spans are respected.
    const auto none = g.FindAll(L"L\u00f6wenstein", {Span{0, 3}});
    CHECK(none.empty());

    WordSet words;
    g.CollectSourceWords(words);
    CHECK(words.count(L"steinnebel") && words.count(L"k\u00f6nigintal") && !words.count(L"bruch"));

    WordSet all;
    CollectNameWords(De(), all);
    CHECK(all.count(L"bruch") && all.count(L"l\u00f6wenstein") && all.count(L"platzhalter") && all.count(L"ost"));
}

static void TestProtect() {
    const Glossary g = Glossary::Build(De(), En());
    const WordSet keep = BuiltinKeepWords();
    const auto p = ProtectForTranslation(L"LFG Meta in L\u00f6wenstein, WP [&BDAEAAA=] bitte", &g, &keep);
    // LFG | " " | Meta | " in " | Lion's Arch | ", " | WP | " " | [&..] | " bitte"
    CHECK(p.glossaryHits.size() == 1);
    CHECK(JoinSegments(p.segments) == L"LFG Meta in Lion's Arch, WP [&BDAEAAA=] bitte");
    int kept = 0;
    for (const auto& s : p.segments) kept += s.keep ? 1 : 0;
    CHECK(kept == 5);

    const auto plain = ProtectForTranslation(L"Wer kommt mit?", &g, &keep);
    CHECK(!HasProtected(plain.segments) && JoinSegments(plain.segments) == L"Wer kommt mit?");

    CHECK(BuiltinSpellIgnore().count(L"kommi") && BuiltinSpellIgnore().count(L"lfg"));
    CHECK(!BuiltinKeepWords().count(L"kommi"));
}

static void TestDeepL() {
    std::vector<Segment> plain{{L"Hallo \"Welt\"", false}};
    auto p = BuildDeepLPayload(plain, L"de", L"en-gb");
    CHECK(!p.xmlMode);
    CHECK(p.json == "{\"text\":[\"Hallo \\\"Welt\\\"\"],\"target_lang\":\"EN-GB\",\"source_lang\":\"DE\"}");

    std::vector<Segment> mixed{{L"Treffpunkt ", false}, {L"Lion's Arch", true}, {L" & <jetzt>", false}};
    p = BuildDeepLPayload(mixed, L"", L"FR");
    CHECK(p.xmlMode);
    CHECK(p.json.find("Treffpunkt <keep>Lion's Arch</keep> &amp; &lt;jetzt&gt;") != std::string::npos);
    CHECK(p.json.find("\"tag_handling\":\"xml\",\"ignore_tags\":[\"keep\"]") != std::string::npos);
    CHECK(p.json.find("source_lang") == std::string::npos);

    CHECK(RestoreFromXml(L"Meet <keep>[&BDAEAAA=]</keep> &amp; &lt;now&gt;") == L"Meet [&BDAEAAA=] & <now>");

    auto r = ParseDeepLResponse(
        R"({"translations":[{"detected_source_language":"DE","text":"Meeting point <keep>Lion's Arch</keep> &amp; now"}]})",
        true);
    CHECK(r.ok && r.text == L"Meeting point Lion's Arch & now" && r.detectedSource == L"DE");
    r = ParseDeepLResponse(R"({"translations":[{"text":"a &amp; b"}]})", false);
    CHECK(r.ok && r.text == L"a &amp; b");  // plain mode keeps literal entities
    r = ParseDeepLResponse(R"({"message":"Wrong endpoint"})", false);
    CHECK(!r.ok && r.error == L"Wrong endpoint");
    r = ParseDeepLResponse("<html>502</html>", false);
    CHECK(!r.ok && !r.error.empty());

    CHECK(IsDeepLFreeKey(L"279a2e9d-83b3-c416-7e2d-f721593e42a0:fx"));
    CHECK(!IsDeepLFreeKey(L"279a2e9d-83b3-c416-7e2d-f721593e42a0"));
}

// ---------------------------------------------------------------------------
// Phase 2: incoming chat
// ---------------------------------------------------------------------------
static void TestLanguages() {
    CHECK(FindLanguage(L"de-AT") && std::wstring(FindLanguage(L"de-AT")->code) == L"DE");
    CHECK(FindLanguage(L"EN") && std::wstring(FindLanguage(L"EN")->code) == L"EN-GB");
    CHECK(FindLanguage(L"en-US") && std::wstring(FindLanguage(L"en-US")->code) == L"EN-US");
    CHECK(FindLanguage(L"zh-TW") && std::wstring(FindLanguage(L"zh-TW")->code) == L"ZH-HANT");
    CHECK(FindLanguage(L"zh") && std::wstring(FindLanguage(L"zh")->code) == L"ZH-HANS");
    CHECK(FindLanguage(L"ar-SA") && !FindLanguage(L"ar-SA")->latinScript);
    CHECK(!FindLanguage(L"xx"));
    CHECK(LanguageEnglishName(L"DE") == L"German");
    CHECK(SourceCode(L"EN-GB") == L"EN");
}

static void TestColors() {
    const auto pal = DefaultChannelColors();
    CHECK(ClassifyColor({200, 140, 255}, pal) == Channel::Whisper);
    CHECK(ClassifyColor({140, 98, 178}, pal) == Channel::Whisper);  // darker anti-aliased purple
    CHECK(ClassifyColor({245, 200, 80}, pal) == Channel::Guild);
    CHECK(ClassifyColor({110, 175, 255}, pal) == Channel::Party);
    CHECK(ClassifyColor({240, 240, 240}, pal) == Channel::Unknown);  // white system text
    CHECK(ClassifyColor({20, 20, 20}, pal) == Channel::Unknown);
    Rgb c;
    CHECK(RgbFromHex(L"#C88CFF", c) && c == Rgb{200, 140, 255});
    CHECK(RgbToHex({200, 140, 255}) == L"c88cff");
    CHECK(!RgbFromHex(L"12345", c));
}

static void TestParseLine() {
    Channel tag;
    auto m = ParseChatLine(L"[12:34] [Map] Emi Sonnenschein: wer kommt mit?", &tag);
    CHECK(tag == Channel::Map && m.speaker == L"Emi Sonnenschein" && m.text == L"wer kommt mit?");
    m = ParseChatLine(L"[M] [ABC] Rygar: lfg meta 12:30", &tag);
    CHECK(tag == Channel::Map && m.speaker == L"Rygar" && m.text == L"lfg meta 12:30");
    m = ParseChatLine(L"1:05 PM Alice: hi", &tag);
    CHECK(tag == Channel::Unknown && m.speaker == L"Alice" && m.text == L"hi");
    m = ParseChatLine(L"From Bob.1234: psst", &tag);
    CHECK(m.speaker == L"From Bob.1234" && m.text == L"psst");
    m = ParseChatLine(L"Der Weltboss erscheint in 5 Minuten", &tag);
    CHECK(m.speaker.empty() && m.text == L"Der Weltboss erscheint in 5 Minuten");
    m = ParseChatLine(L"http://example.com: nope", &tag);
    CHECK(m.speaker.empty());
}

static void TestBuildMessages() {
    const auto pal = DefaultChannelColors();
    std::vector<OcrLine> lines = {
        {L"Alice: hello everyone, anyone up for the", {240, 165, 155}, 0, 14},
        {L"world boss later?", {238, 160, 150}, 15, 14},                   // wrapped continuation
        {L"From Bob.1234: hey, kannst du helfen?", {200, 140, 255}, 32, 14},
        {L"To Bob.1234: klar!", {198, 138, 250}, 48, 14},
        {L"[G] Carl: guild missions now", {245, 200, 80}, 64, 14},
        {L"Server restart in 10 minutes", {240, 240, 240}, 80, 14},
    };
    auto msgs = BuildMessages(lines, pal);
    CHECK(msgs.size() == 5);
    if (msgs.size() == 5) {
        CHECK(msgs[0].channel == Channel::Map && msgs[0].speaker == L"Alice" &&
              msgs[0].text == L"hello everyone, anyone up for the world boss later?");
        CHECK(msgs[1].channel == Channel::Whisper && msgs[1].speaker == L"Bob.1234" && !msgs[1].outgoingWhisper);
        CHECK(msgs[2].channel == Channel::Whisper && msgs[2].speaker == L"Bob.1234" && msgs[2].outgoingWhisper);
        CHECK(msgs[3].channel == Channel::Guild && msgs[3].speaker == L"Carl");
        CHECK(msgs[4].channel == Channel::System && msgs[4].speaker.empty());
        CHECK((msgs[0].color == Rgb{240, 165, 155}));
    }

    std::vector<OcrLine> hyphen1 = {
        {L"Alice: unter-", {240, 165, 155}, 0, 14},
        {L"brechung", {238, 160, 150}, 15, 14},
    };
    auto m1 = BuildMessages(hyphen1, pal);
    CHECK(m1.size() == 1 && m1[0].text == L"unterbrechung");

    std::vector<OcrLine> hyphen2 = {
        {L"Alice: 80-", {240, 165, 155}, 0, 14},
        {L"er Zone", {238, 160, 150}, 15, 14},
    };
    auto m2 = BuildMessages(hyphen2, pal);
    CHECK(m2.size() == 1 && m2[0].text == L"80-er Zone");

    std::vector<OcrLine> hyphen3 = {
        {L"Alice: PvP-", {240, 165, 155}, 0, 14},
        {L"Event startet", {238, 160, 150}, 15, 14},
    };
    auto m3 = BuildMessages(hyphen3, pal);
    CHECK(m3.size() == 1 && m3[0].text == L"PvP-Event startet");
}

// German QWERTZ letter rows as KeyLayout (what keyboard_layout.cpp builds from Windows).
static KeyLayout Qwertz() {
    KeyLayout k;
    const wchar_t* rows[] = {L"qwertzuiopü", L"asdfghjklöä", L"yxcvbnm"};
    const float offset[] = {0.0f, 0.25f, 0.75f};
    for (int r = 0; r < 3; ++r)
        for (size_t i = 0; rows[r][i]; ++i) k.Set(rows[r][i], r, static_cast<float>(i) + offset[r]);
    return k;
}

static void TestGuessLanguage() {
    CHECK(GuessLanguageByLetters(L"nasılsın kardeş") == L"TR");      // nasılsın kardeş
    CHECK(GuessLanguageByLetters(L"¿qué tal, mañana?") == L"ES");     // ¿qué tal, mañana?
    CHECK(GuessLanguageByLetters(L"cześć, jak się masz") == L"PL");   // cześć, jak się masz
    CHECK(GuessLanguageByLetters(L"grüße euch") == L"DE");
    CHECK(GuessLanguageByLetters(L"привет") == L"RU");  // привет
    CHECK(GuessLanguageByLetters(L"привіт") == L"UK");  // привіт
    CHECK(GuessLanguageByLetters(L"مرحبا") == L"AR");
    CHECK(GuessLanguageByLetters(L"안녕") == L"KO");
    CHECK(GuessLanguageByLetters(L"hello there").empty());
}

static void TestTypingSlips() {
    const KeyLayout k = Qwertz();
    CHECK(k.Neighbors(L'a', L's') && k.Neighbors(L'g', L'z') && !k.Neighbors(L'a', L'l'));
    CHECK(k.Shifted(L'h', 1) == L'j' && k.Shifted(L'l', 1) == L'ö' && k.Shifted(L'q', -1) == 0);
    // The whole hand one key to the right: "hallo" came out as "jsööp".
    const auto v = HandShiftVariants(L"jsööp", k);
    CHECK(std::find(v.begin(), v.end(), L"hallo") != v.end());
    // Slips on the key next door are cheap, other mistakes are not.
    CHECK(SlipDistance(L"hsllo", L"hallo", k, 3) == 0.5);
    CHECK(SlipDistance(L"hzllo", L"hallo", k, 3) == 1.0);
    CHECK(SlipDistance(L"gsööo", L"hallo", k, 3) > 1.5);  // four slips: too far for a 5-letter word
    WordModel m;
    for (int i = 0; i < 3; ++i) m.Learn(L"hallo zusammen wer kommt mit zum Tequatl");
    // Two slips in one word still find it ("zusammen" typed "zusamnwn").
    auto near = m.NearSlip(L"zusamnwn", k, 3);
    CHECK(!near.empty() && near[0] == L"zusammen");
    near = m.NearSlip(L"Teqiatl", k, 3);
    CHECK(!near.empty() && near[0] == L"Tequatl");
    CHECK(m.NearSlip(L"zum", k, 3).empty());  // a known word needs no correction from itself
    CHECK(LooksLikeSlip(L"micih", L"mich", k, false));
    CHECK(IsKeepWord(BuiltinSpellIgnore(), L"ohje"));
}



static void TestGw2Text() {
    CHECK(UnsupportedScript(L"Hallöchen ça va? Łódź").empty());
    CHECK(UnsupportedScript(L"hi مرحبا") == L"Arabic");
    CHECK(UnsupportedScript(L"你好") == L"Chinese");
    CHECK(UnsupportedScript(L"Привет") == L"Cyrillic");
    CHECK(IsRtlText(L"  مرحبا hi"));
    CHECK(!IsRtlText(L"hi مرحبا"));

    const std::wstring longText =
        L"Das ist der erste Satz. Und hier kommt ein zweiter, etwas längerer Satz, der den Rahmen sprengt.";
    auto parts = SplitForChat(longText, L"/p ", 60);
    CHECK(parts.size() >= 2);
    bool fits = true;
    for (const auto& p : parts) fits = fits && CodePointCount(p) <= 60 && p.rfind(L"/p ", 0) == 0;
    CHECK(fits);
    CHECK(!parts.empty() && parts[0] == L"/p Das ist der erste Satz.");
    CHECK(SplitForChat(L"kurz", L"", 199).size() == 1);
}

static void TestLlm() {
    std::vector<std::vector<Segment>> items = {{{L"Treffen in ", false}, {L"Lion's Arch", true}},
                                               {{L"say \"hi\"", false}}};
    const std::string req = BuildLlmRequest(items, L"German", L"qwen2.5:7b");
    JsonValue v;
    CHECK(ParseJson(req, v));
    CHECK(v.GetString("model") == "qwen2.5:7b");
    const JsonValue* msgs = v.Get("messages");
    CHECK(msgs && msgs->arr.size() == 2);
    if (msgs && msgs->arr.size() == 2) {
        const std::string user = msgs->arr[1].GetString("content");
        JsonValue arr;
        CHECK(ParseJson(user, arr) && arr.arr.size() == 2);
        CHECK(arr.arr.size() == 2 && arr.arr[0].s == "Treffen in <k>Lion's Arch</k>" && arr.arr[1].s == "say \"hi\"");
        CHECK(msgs->arr[0].GetString("content").find("German") != std::string::npos);
    }

    auto r = ParseLlmResponse(
        R"({"choices":[{"message":{"role":"assistant","content":"<think>hmm</think>\n```json\n[\"Meet in <k>Lion's Arch</k>\", \"sag \\\"hi\\\"\"]\n```"}}]})",
        2);
    CHECK(r.ok && r.texts.size() == 2 && r.texts[0] == L"Meet in Lion's Arch" && r.texts[1] == L"sag \"hi\"");
    r = ParseLlmResponse(R"({"choices":[{"message":{"content":"Hallo zusammen"}}]})", 1);
    CHECK(r.ok && r.texts.size() == 1 && r.texts[0] == L"Hallo zusammen");
    // Native Ollama /api/chat response
    r = ParseLlmResponse(R"({"message":{"role":"assistant","content":"[\"Meet in Lion's Arch\", \"sag \\\"hi\\\"\"]"}})", 2);
    CHECK(r.ok && r.texts.size() == 2 && r.texts[0] == L"Meet in Lion's Arch" && r.texts[1] == L"sag \"hi\"");
    r = ParseLlmResponse(R"({"choices":[{"message":{"content":"[\"nur eins\"]"}}]})", 2);
    CHECK(!r.ok && r.formatError);  // -> the translator retries line by line
    r = ParseLlmResponse(R"({"error":{"message":"model not found"}})", 1);
    CHECK(!r.ok && !r.formatError && r.error.find(L"model not found") != std::wstring::npos);
    r = ParseLlmResponse(R"({"error":"model 'xyz' not found, try pulling it first"})", 1);
    CHECK(!r.ok && !r.formatError && r.error.find(L"model 'xyz' not found") != std::wstring::npos);

    // Ollama CPU-friendly options in request
    const std::string reqOllama = BuildLlmRequest(items, L"German", L"gemma3:1b", false, true, true);
    JsonValue vOllama;
    CHECK(ParseJson(reqOllama, vOllama));
    CHECK(vOllama.GetBool("stream", true) == false);
    CHECK(vOllama.GetString("keep_alive") == "30m");
    CHECK(vOllama.GetBool("think", true) == false);  // no thinking phase
    const JsonValue* opt = vOllama.Get("options");
    CHECK(opt != nullptr);
    if (opt) {
        const JsonValue* numGpu = opt->Get("num_gpu");
        CHECK(numGpu && numGpu->n == 0.0);
        const JsonValue* numThread = opt->Get("num_thread");
        CHECK(numThread && numThread->n == 4.0);
        const JsonValue* numCtx = opt->Get("num_ctx");
        CHECK(numCtx && numCtx->n == 4096.0);  // never the 128k context Ollama may be set to
    }
}

static void TestDeepLBatch() {
    std::vector<std::vector<Segment>> items = {{{L"Hallo", false}}, {{L"Treffen in ", false}, {L"Lion's Arch", true}}};
    auto p = BuildDeepLPayloadBatch(items, L"", L"EN-GB");
    CHECK(p.xmlMode);
    CHECK(p.json.find("\"text\":[\"Hallo\",\"Treffen in <keep>Lion's Arch</keep>\"]") != std::string::npos);
    auto rs = ParseDeepLResponseBatch(
        R"({"translations":[{"detected_source_language":"DE","text":"Hello"},{"detected_source_language":"DE","text":"Meet in <keep>Lion's Arch</keep>"}]})",
        true, 2);
    CHECK(rs.size() == 2 && rs[0].ok && rs[0].text == L"Hello" && rs[1].text == L"Meet in Lion's Arch");
    rs = ParseDeepLResponseBatch(R"({"message":"Quota exceeded"})", false, 2);
    CHECK(rs.size() == 2 && !rs[0].ok && rs[1].error == L"Quota exceeded");
}

static void TestMumble() {
    auto id = ParseMumbleIdentity(
        L"{\"name\":\"Emi Sonnenschein\",\"profession\":3,\"spec\":0,\"race\":2,\"map_id\":15,\"world_id\":0,"
        L"\"team_color_id\":0,\"commander\":true,\"fov\":1.222,\"uisz\":1}");
    CHECK(id.name == L"Emi Sonnenschein" && id.uiSize == 1 && id.mapId == 15 && id.commander);
    CHECK(ParseMumbleIdentity(L"").name.empty());
    MumbleState st;
    st.live = true;
    st.uiState = kUiTextboxHasFocus | kUiGameHasFocus;
    CHECK(st.TextboxHasFocus() && st.GameHasFocus());
    CHECK(Gw2BottomBarOffset(1080.0f, 0) == 25.0f);
    CHECK(Gw2BottomBarOffset(1080.0f, 1) == 28.0f);
    CHECK(Gw2BottomBarOffset(1080.0f, 2) == 31.0f);
    CHECK(Gw2BottomBarOffset(1080.0f, 3) == 35.0f);
    CHECK(Gw2BottomBarOffset(1080.0f, -1) == 25.0f);
    CHECK(Gw2BottomBarOffset(2160.0f, 1) == 56.0f);
}

static void TestMyMemory() {
    CHECK(UrlEncode("a b&c\xC3\xA4") == "a%20b%26c%C3%A4");
    CHECK(MyMemoryLang(L"EN-GB") == L"en-GB");
    CHECK(MyMemoryLang(L"ZH-HANS") == L"zh-CN");
    CHECK(MyMemoryLang(L"DE") == L"de");
    CHECK(BuildMyMemoryPath(L"Hallo Welt", L"DE", L"EN-GB", L"") == L"/get?q=Hallo%20Welt&langpair=de%7Cen-GB&mt=1");
    auto r = ParseMyMemoryResponse(R"({"responseData":{"translatedText":"Hello world","match":0.98},"quotaFinished":false,"responseDetails":"","responseStatus":200})");
    CHECK(r.ok && r.text == L"Hello world");
    r = ParseMyMemoryResponse(R"({"responseData":{"translatedText":"MYMEMORY WARNING: YOU USED ALL AVAILABLE FREE TRANSLATIONS FOR TODAY"},"responseStatus":429})");
    CHECK(!r.ok && r.quotaExceeded);
    r = ParseMyMemoryResponse(R"({"responseData":{"translatedText":""},"responseDetails":"INVALID LANGUAGE PAIR","responseStatus":"403"})");
    CHECK(!r.ok && !r.quotaExceeded && r.error.find(L"INVALID") != std::wstring::npos);
}

// Once the free contingent is used up, a batch must not keep firing requests.
static void TestBatchQuota() {
    struct Fake : Translator {
        int calls = 0;
        TranslateResult Translate(const std::vector<Segment>& segs, const std::wstring&, const std::wstring&) override {
            ++calls;
            TranslateResult r;
            if (calls == 2) {
                r.error = L"quota";
                r.quotaExceeded = true;
            } else {
                r.ok = true;
                r.text = segs.empty() ? L"" : segs[0].text;
            }
            return r;
        }
        std::wstring Name() const override { return L"fake"; }
    } t;
    const std::vector<std::vector<Segment>> items(5, std::vector<Segment>{{L"x", false}});
    const auto out = t.TranslateBatch(items, L"", L"DE");
    CHECK(out.size() == 5);
    CHECK(t.calls == 2);
    CHECK(out[0].ok && !out[1].ok && out[1].quotaExceeded);
    CHECK(!out[4].ok && out[4].quotaExceeded);
}


static void TestAsciiEscape() {
    const std::wstring name = L"Fl\u00fcstern \u0645\u0631\u062d\u0628\u0627 a\\b";
    const std::wstring esc = AsciiEscape(name);
    bool ascii = true;
    for (wchar_t c : esc) ascii = ascii && static_cast<uint32_t>(c) < 0x80;
    CHECK(ascii);
    CHECK(esc.find(L"\\u00fc") != std::wstring::npos);
    CHECK(AsciiUnescape(esc) == name);
    CHECK(AsciiUnescape(L"C:\\temp \\uZZZZ") == L"C:\\temp \\uZZZZ");  // stray backslashes stay
    const std::wstring emoji = FromUtf8("x\xF0\x9F\x98\x80y");      // outside the BMP
    CHECK(AsciiUnescape(AsciiEscape(emoji)) == emoji);
}

// ---------------------------------------------------------------------------
static void TestI18n() {
    SetUiLang(UiLang::En);
    CHECK(Tr(L"Map") == L"Map");
    CHECK(TrF(L"Sent {1} of {2}", {L"1", L"3"}) == L"Sent 1 of 3");
    CHECK(FormatArgs(L"{1}{1}{3}", {L"a"}) == L"aa{3}");
    CHECK(UiLangFromCode(L"de-AT") == UiLang::De && UiLangFromCode(L"AR") == UiLang::Ar);
    CHECK(UiLangFromCode(L"fr", UiLang::En) == UiLang::En);
    SetUiLang(UiLang::De);
    CHECK(Tr(L"Map") == L"Karte");
    CHECK(Tr(L"no such text, falls back") == L"no such text, falls back");
    SetUiLang(UiLang::Ar);
    CHECK(UiRtl());
    CHECK(Tr(L"Map") != L"Map");
    SetUiLang(UiLang::En);
    CHECK(!UiRtl());
    CHECK(TranslationCount(UiLang::De) > 50 && TranslationCount(UiLang::Ar) > 50);
}

static void TestWordModel() {
    CHECK(EditDistance(L"kommt", L"komtm", 2) == 1);   // swap
    CHECK(EditDistance(L"hallo", L"halo", 2) == 1);
    CHECK(EditDistance(L"abc", L"xyz", 1) == 2);        // limit + 1
    CHECK(MatchCase(L"Hallo", L"hello") == L"Hello");
    CHECK(MatchCase(L"HALLO", L"hello") == L"HELLO");
    CHECK(MatchCase(L"hallo", L"Lion's") == L"Lion's");

    WordModel m;
    for (int i = 0; i < 3; ++i) m.Learn(L"Wir gehen zum Tequatl, kommt jemand mit?");
    m.Learn(L"/g wir brauchen noch Leute");
    CHECK(m.Knows(L"tequatl") && m.Knows(L"kommt"));
    CHECK(m.Count(L"g") == 0);                         // command prefix not learned
    CHECK(m.Count(L"wir") >= 4);
    auto c = m.Complete(L"Teq", L"zum", 3);
    CHECK(!c.empty() && c[0] == L"Tequatl");
    auto n = m.Next(L"kommt", 3);
    CHECK(!n.empty() && n[0] == L"jemand");
    auto near = m.Near(L"komtm", 3);
    CHECK(!near.empty() && near[0] == L"kommt");
    // First word of a message keeps the mid-sentence form.
    m.Learn(L"Lion's Arch ist voll");
    m.Learn(L"in Lion's Arch");
    CHECK(m.Complete(L"lio", L"", 1).size() == 1 && m.Complete(L"lio", L"", 1)[0] == L"Lion's");

    // Phone-style correction.
    CHECK(ChooseCorrection(L"komtm", {L"kommt", L"komm"}, m) == L"kommt");
    CHECK(ChooseCorrection(L"Komtm", {}, m) == L"Kommt");
    CHECK(ChooseCorrection(L"LFG", {L"LG"}, m).empty());          // all caps
    CHECK(ChooseCorrection(L"dsa", {L"das"}, m).empty());         // too short
    CHECK(ChooseCorrection(L"kommt", {L"komm"}, m).empty());      // a word you use
    CHECK(ChooseCorrection(L"fraktal", {L"Fraktal"}, m).empty()); // same word
    CHECK(ChooseCorrection(L"hause", {L"Haus mit"}, m).empty());  // no multi-word
    CHECK(ChooseCorrection(L"wollte", {L"sollte"}, m).empty());   // other first letter
    CHECK(ChooseCorrection(L"Tequalt", {}, m) == L"Tequatl");

    const std::string saved = m.Serialize();
    WordModel back;
    back.Parse(saved);
    CHECK(back.Size() == m.Size() && back.PairCount() == m.PairCount());
    CHECK(back.Count(L"kommt") == m.Count(L"kommt"));
    CHECK(back.Complete(L"Teq", L"", 1).size() == 1);

    // Completion with one typo in what was typed so far.
    m.Learn(L"hello hello hello");
    auto fz = m.CompleteFuzzy(L"helo", L"", 3);
    CHECK(!fz.empty() && fz[0] == L"hello");
    CHECK(m.CompleteFuzzy(L"komt", L"", 3).size() >= 1 && m.CompleteFuzzy(L"komt", L"", 3)[0] == L"kommt");
    CHECK(m.CompleteFuzzy(L"Teq", L"", 3).empty());   // exact completions are Complete()'s job
    CHECK(m.CompleteFuzzy(L"ke", L"", 3).empty());    // too short to guess
    m.Learn(L"sagen sagen sahen sahen");  // "sajen": one typo from both, j sits next to h
    CHECK(m.CompleteFuzzy(L"sajen", L"", 3)[0] == L"sagen");  // without layout: equal, alphabetical
    auto nb = m.CompleteFuzzy(L"sajen", L"", 3, [](wchar_t a, wchar_t b) {
        return (a == L'h' && b == L'j') || (a == L'j' && b == L'h');
    });
    CHECK(!nb.empty() && nb[0] == L"sahen");

    // Forget: a word taught by mistake disappears with its pairs.
    CHECK(m.Forget(L"Jemand"));
    CHECK(m.Count(L"jemand") == 0 && m.Next(L"kommt", 3).empty());
    CHECK(!m.Forget(L"jemand"));
    CHECK(m.Dirty());
    WordModel cleared = m;
    cleared.Clear();
    CHECK(cleared.Size() == 0 && cleared.PairCount() == 0);

    // Arabic: spelling variants and harakat count as the same word.
    CHECK(WordKey(L"أحمد") == WordKey(L"احمد"));           // أحمد / احمد
    CHECK(WordKey(L"مدرسة") == WordKey(L"مدرسه")); // مدرسة / مدرسه
    CHECK(WordKey(L"على") == WordKey(L"علي"));                         // على / علي
    CHECK(WordKey(L"مَرْحَبا") == WordKey(L"مرحبا"));
    WordModel ar;
    for (int i = 0; i < 2; ++i) ar.Learn(L"مرحبا أصدقائي");
    CHECK(ar.Knows(L"اصدقائي"));  // typed without hamza
    auto arc = ar.Complete(L"اصد", L"", 1);          // "اصد" completes "أصدقائي"
    CHECK(arc.size() == 1 && arc[0] == L"أصدقائي");
    auto arf = ar.CompleteFuzzy(L"مرخب", L"", 1);  // مرخب: خ for ح
    CHECK(arf.size() == 1 && arf[0] == L"مرحبا");
    CHECK(!IsWordChar(L'؟') && !IsWordChar(L'،'));         // ؟ and ، end a word

    WordModel big;  // pruning keeps the size bounded
    for (size_t i = 0; i < WordModel::kMaxWords + 50; ++i) big.AddWord(L"wort" + std::wstring(1, L'a' + i % 26) + std::to_wstring(i), 1);
    CHECK(big.Size() <= WordModel::kMaxWords);
    // Speed: the word bar runs on every key press. Full model, worst case.
    auto letters = [](size_t i) {  // 0 -> "aaaa", 1 -> "aaab" ... (letters only, digits are not learned)
        std::wstring s(4, L'a');
        for (int k = 3; k >= 0; --k, i /= 26) s[k] = static_cast<wchar_t>(L'a' + i % 26);
        return s;
    };
    WordModel full;
    for (size_t i = 0; i < WordModel::kMaxWords - 10; ++i) full.AddWord(L"wort" + letters(i), 2);
    const auto t0 = std::chrono::steady_clock::now();
    size_t hits = 0;
    for (int i = 0; i < 20; ++i) {
        hits += full.Complete(L"worta", L"", 3).size();
        hits += full.CompleteFuzzy(L"wotra", L"", 3).size();
    }
    const double perKeyMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 20.0;
    std::printf("word bar, %zu words: %.3f ms per key press (%zu hits)\n", full.Size(), perKeyMs, hits);
    CHECK(hits == 120);
    CHECK(perKeyMs < 15.0);
}

// A synthetic chat panel: dark background, "letters" as bright stripes.


static void TestMangledStamps() {
    // Timestamps whose brackets text recognition misread, as seen on real 4K captures.
    struct Case {
        const wchar_t* line;
        Channel channel;
        const wchar_t* speaker;
        const wchar_t* text;
    };
    const Case cases[] = {
        {L"117:46J[M] Kiro Vale: wer kommt mit", Channel::Map, L"Kiro Vale", L"wer kommt mit"},
        {L"(17-46)(M) Pell Brand: gleich da >", Channel::Map, L"Pell Brand", L"gleich da >"},
        {L"(17: 46)(M) Tamsin: hallo", Channel::Map, L"Tamsin", L"hallo"},
        {L"(17-471(M) Kiro Vale: bin weg", Channel::Map, L"Kiro Vale", L"bin weg"},
        {L"[17:48J1IWJ] An Rook: ich warte", Channel::Whisper, L"Rook", L"ich warte"},
        {L"[7:571[M] Sivo Dahl: anyone for the meta", Channel::Map, L"Sivo Dahl", L"anyone for the meta"},
        {L"10M] Tamsin: gute Nacht", Channel::Map, L"Tamsin", L"gute Nacht"},
        {L"[(W] An Rook: ich", Channel::Whisper, L"Rook", L"ich"},
        {L"[10:12][P] Kiro Vale: komm", Channel::Party, L"Kiro Vale", L"komm"},
        {L"tO:13JtPJ Kiro Vale: danke", Channel::Party, L"Kiro Vale", L"danke"},          // Windows OCR
        {L"(17:47 10M] Tamsin: gute Nacht", Channel::Map, L"Tamsin", L"gute Nacht"},
        {L"[10:12JtPJ Kiro Vale; [Wegmarke Felder]", Channel::Party, L"Kiro Vale", L"[Wegmarke Felder]"},
    };
    for (const Case& c : cases) {
        std::vector<OcrLine> one(1);
        one[0].text = c.line;
        one[0].height = 15;
        const auto msgs = BuildMessages(one, DefaultChannelColors());
        CHECK(msgs.size() == 1);
        if (msgs.size() != 1) continue;
        if (!msgs[0].stamped || msgs[0].channel != c.channel || msgs[0].speaker != c.speaker)
            std::printf("case: %ls -> [%ls] [%ls]\n", c.line, msgs[0].speaker.c_str(), msgs[0].text.c_str());
        CHECK(msgs[0].stamped && msgs[0].channel == c.channel);
        CHECK(msgs[0].speaker == c.speaker);
        CHECK(msgs[0].text == c.text);
    }
    // Lone misread brackets before a name.
    CHECK(ParseChatLine(L") Kiro Vale: bis gleich").speaker == L"Kiro Vale");
    CHECK(ParseChatLine(L"J Von Rook: in was").speaker == L"Von Rook");
    // Not a stamp: plain text and names that start with tag letters.
    Channel ch = Channel::Unknown;
    CHECK(MangledStampAndTagLength(L"[7:50] Bedrohung entdeckt!", &ch) == 0);
    CHECK(MangledStampAndTagLength(L"[10:13] Mad ist nicht mehr", &ch) == 0);
    CHECK(MangledStampAndTagLength(L"Mad Kiro: hallo", &ch) == 0);
    CHECK(MangledStampAndTagLength(L"2 Pakete (P) bitte", &ch) == 0);

    // A wrapped line continues the text even when the speaker's name has another colour.
    std::vector<OcrLine> wrap(2);
    wrap[0].text = L"[7:57][M] Sivo Dahl: our guild is looking for new players";
    wrap[0].top = 0;
    wrap[0].height = 15;
    wrap[0].words = {{L"[7:57][M]", {200, 200, 200}, true}, {L"Sivo", {240, 110, 100}, true},
                     {L"Dahl:", {240, 110, 100}, true},     {L"looking", {250, 220, 205}, true},
                     {L"new", {250, 220, 205}, true},       {L"players", {250, 220, 205}, true}};
    wrap[1].text = L"for raids and fractals, whisper me";
    wrap[1].top = 20;
    wrap[1].height = 15;
    wrap[1].words = {{L"for", {248, 221, 207}, true}, {L"raids", {248, 221, 207}, true}, {L"and", {248, 221, 207}, true}};
    const auto wm = BuildMessages(wrap, DefaultChannelColors());
    CHECK(wm.size() == 1 && wm[0].text.find(L"raids and fractals") != std::wstring::npos);
}

static void TestStarterWords() {
    const auto& v = Gw2StarterWords();
    CHECK(v.size() > 60);
    CHECK(std::find(v.begin(), v.end(), L"Tequatl") != v.end());
    std::set<std::wstring> keys;
    for (const std::wstring& w : v) keys.insert(WordKey(w));
    CHECK(keys.size() == v.size());  // no duplicates
    // By the language you write in: no English suggestions for German and the other way round.
    auto has = [](const std::vector<std::wstring>& list, const wchar_t* w) {
        return std::find(list.begin(), list.end(), w) != list.end();
    };
    const auto& de = Gw2StarterWords(L"DE");
    const auto& en = Gw2StarterWords(L"EN-GB");
    const auto& fr = Gw2StarterWords(L"FR");
    CHECK(has(de, L"Danke") && !has(de, L"Thanks") && has(de, L"Tequatl"));
    CHECK(has(en, L"Thanks") && !has(en, L"Danke") && has(en, L"Tequatl"));
    CHECK(!has(fr, L"Danke") && !has(fr, L"Thanks") && has(fr, L"LFG"));
}

static void TestEmoticons() {
    auto kept = [](const std::wstring& text) {
        std::vector<std::wstring> k;
        for (const Segment& s : ProtectForTranslation(text, nullptr, nullptr).segments)
            if (s.keep) k.push_back(s.text);
        return k;
    };
    CHECK((kept(L"bin gleich da :D") == std::vector<std::wstring>{L":D"}));
    CHECK((kept(L"^^ danke <3 xD") == std::vector<std::wstring>{L"^^", L"<3", L"xD"}));
    CHECK((kept(L"hallo :-) und ;P und :3") == std::vector<std::wstring>{L":-)", L";P", L":3"}));
    CHECK(kept(L"12:30 am Tor, Uhrzeit: 8").empty());  // times and colons are no smileys
    CHECK(kept(L"Notiz: D und P").empty());
}

static void TestLinks() {
    const std::wstring t = L"schau mal https://wiki.guildwars2.com/wiki/Tequatl, und www.gw2efficiency.com! "
                           L"oder (discord.gg/abc12) bzw. http://x.y";
    const auto links = FindLinks(t);
    CHECK(links.size() == 4);
    if (links.size() == 4) {
        CHECK(t.substr(links[0].start, links[0].length) == L"https://wiki.guildwars2.com/wiki/Tequatl");
        CHECK(t.substr(links[1].start, links[1].length) == L"www.gw2efficiency.com");
        CHECK(t.substr(links[2].start, links[2].length) == L"discord.gg/abc12");
        CHECK(t.substr(links[3].start, links[3].length) == L"http://x.y");
    }
    CHECK(LinkTarget(L"www.gw2efficiency.com") == L"https://www.gw2efficiency.com");
    CHECK(LinkTarget(L"http://x.y") == L"http://x.y");
    CHECK(LinkTarget(L"https://example.com/\"injection\"") == L"https://example.com/injection");
    CHECK(FindLinks(L"wer kommt mit? www. ist kein link").empty());
    // Not translated.
    const ProtectedText p = ProtectForTranslation(L"guck hier www.gw2efficiency.com bitte", nullptr, nullptr);
    CHECK(p.segments.size() == 3 && p.segments[1].keep && p.segments[1].text == L"www.gw2efficiency.com");
}


static void TestNotChat() {
    // The character selection shows character data where the chat is: no
    // timestamp, no tag, no "Name:" -> nothing is read.
    const wchar_t* selection[] = {L"Kiro Vale", L"Stufe 80 Schnitterin (Mensch)", L"54% der Karte erforscht",
                                  L"@ Löwenstein", L"Weltname [DE]", L"80 t"};
    std::vector<OcrLine> lines;
    int top = 0;
    for (const wchar_t* t : selection) {
        OcrLine l;
        l.text = t;
        l.top = top;
        l.height = 15;
        l.color = {240, 200, 120};
        top += 20;
        lines.push_back(l);
    }
    CHECK(BuildMessages(lines, DefaultChannelColors()).empty());
    // Real chat in the same frame still comes through; fragments without letters do not.
    lines[2].text = L"[19:20][M] Tamsin: wer kommt mit zum Tequatl";
    lines[4].text = L"[19:21] Bedrohung entdeckt! Tequatl greift an";
    lines[5].text = L"[19:22] 80 t";
    const auto msgs = BuildMessages(lines, DefaultChannelColors());
    CHECK(msgs.size() == 2);
    if (msgs.size() == 2) {
        CHECK(msgs[0].speaker == L"Tamsin");
        CHECK(msgs[1].text.find(L"Bedrohung") == 0);
    }
    // Read down to the window edge: the input line with what you are typing
    // and the number row below it are no messages.
    std::vector<OcrLine> bottom(4);
    const wchar_t* texts[] = {L"[19:20][P] Tamsin: kommt ihr", L"[19:21][P] Rook: gleich", L"[Gruppe] bin gleich da",
                              L"621"};
    for (int i = 0; i < 4; ++i) {
        bottom[i].text = texts[i];
        bottom[i].top = i * 20;
        bottom[i].height = 15;
    }
    const auto b = BuildMessages(bottom, DefaultChannelColors());
    CHECK(b.size() == 2 && b.back().speaker == L"Rook");
}

static void TestLocateChat() {
    // The game's corner: a health number, then the chat (timestamps aligned at x=40), the input line.
    struct L {
        const wchar_t* text;
        int left, top, width;
    };
    const L in[] = {
        {L"12.345", 600, 10, 60},
        {L"[19:20][M] Tamsin: wer kommt mit zum", 40, 100, 380},
        {L"Tequatl heute abend", 40, 120, 200},
        {L"[19:21][P] Rook: ich", 41, 140, 160},
        {L"[19:22] Bedrohung entdeckt!", 39, 160, 300},
        {L"[Gruppe]", 40, 200, 80},
    };
    std::vector<OcrLine> lines;
    for (const L& l : in) {
        OcrLine o;
        o.text = l.text;
        o.left = l.left;
        o.top = l.top;
        o.width = l.width;
        o.height = 15;
        lines.push_back(o);
    }
    ChatBlock b;
    CHECK(LocateChatLines(lines, &b));
    CHECK(b.stamped == 3 && b.top == 100 && b.bottom == 175 && b.left == 39 && b.right == 420);
    // Only one timestamp: not sure enough.
    std::vector<OcrLine> one(lines.begin(), lines.begin() + 3);
    CHECK(!LocateChatLines(one, &b));
}

static void TestNames() {
    NameList names;
    names.Add(L"Kiro Vale");
    names.Add(L"Mad Kiro");
    names.Add(L"B E L A");
    names.Add(L"Tamsin");
    names.Add(L"Wanderer");
    names.Add(L"Al");  // too short to protect as a single word
    auto has = [&](const std::wstring& text, const std::wstring& name) {
        for (const Span& s : names.Find(text))
            if (text.substr(s.start, s.length) == name) return true;
        return false;
    };
    CHECK(has(L"helo du med kiro wie gehts", L"med kiro"));     // one typo in a short word, other word exact
    CHECK(has(L"frag Kiro Vale mal", L"Kiro Vale"));
    CHECK(has(L"ich warte auf b e l a", L"b e l a"));
    CHECK(has(L"wo ist tamsin?", L"tamsin"));
    CHECK(!has(L"med kiru ist da", L"med kiru"));               // no word exact
    names.Add(L"Rook");
    CHECK(!has(L"rouk ist da", L"rouk"));                       // one-word names: typo only from 6 letters
    CHECK(has(L"tamsen ist da", L"tamsen"));
    CHECK(has(L"der wandrer kommt", L"wandrer"));
    CHECK(names.Find(L"al ist da").empty());
    // The translator leaves the name alone.
    const ProtectedText p = ProtectForTranslation(L"helo du med kiro", nullptr, nullptr, &names);
    CHECK(p.segments.size() == 2 && !p.segments[0].keep && p.segments[1].keep && p.segments[1].text == L"med kiro");
    for (int i = 0; i < 400; ++i) names.Add(L"Spieler" + std::wstring(1, static_cast<wchar_t>(L'a' + i % 26)) +
                                            std::wstring(1, static_cast<wchar_t>(L'a' + i / 26)));
    CHECK(names.Size() == NameList::kMax);
}


static void TestLanguageTool() {
    CHECK(LanguageToolLang(L"DE") == L"de-DE" && LanguageToolLang(L"en-GB") == L"en-GB" && LanguageToolLang(L"") == L"auto");
    CHECK(LanguageToolLang(L"AR") == L"ar" && LanguageToolLang(L"ZH-HANS") == L"zh-CN" && LanguageToolLang(L"fr_FR") == L"fr-FR");
    const std::string form = BuildLanguageToolForm(L"Ich komme morgn", L"DE", L"");
    CHECK(form.find("text=Ich%20komme%20morgn") != std::string::npos || form.find("text=Ich+komme+morgn") != std::string::npos);
    CHECK(form.find("language=de-DE") != std::string::npos);
    const std::wstring text = L"Ich komme morgn \U0001F600 dann";
    const std::string body = R"({"software":{},"language":{"code":"de-DE"},"matches":[
        {"message":"Möglicher Tippfehler","shortMessage":"Tippfehler","replacements":[{"value":"morgen"},{"value":"Morgen"}],
         "offset":10,"length":5,"rule":{"id":"GERMAN_SPELLER_RULE","issueType":"misspelling"}},
        {"message":"x","replacements":[],"offset":19,"length":4,"rule":{"id":"R","issueType":"grammar"}}]})";
    const LtResult r = ParseLanguageToolResponse(body, text);
    CHECK(r.ok && r.language == L"de-DE" && r.matches.size() == 2);
    if (r.matches.size() == 2) {
        CHECK(text.substr(r.matches[0].span.start, r.matches[0].span.length) == L"morgn");
        CHECK(r.matches[0].Spelling() && r.matches[0].replacements.size() == 2 && r.matches[0].replacements[0] == L"morgen");
        CHECK(text.substr(r.matches[1].span.start, r.matches[1].span.length) == L"dann");  // after an emoji (2 UTF-16 units)
    }
    CHECK(!ParseLanguageToolResponse("Too many requests", text).ok);
    CHECK(NormalizeLanguageToolUrl(L"") == L"https://api.languagetool.org/v2/check");
    CHECK(NormalizeLanguageToolUrl(L"http://localhost:8081/") == L"http://localhost:8081/v2/check");
    CHECK(NormalizeLanguageToolUrl(L"http://x/v2") == L"http://x/v2/check");
    RateLimiter rl(2);
    CHECK(rl.Allow(0) && rl.Allow(1000) && !rl.Allow(2000) && rl.Allow(61000));
}

static void TestOcrTimestamps() {
    struct Case {
        const wchar_t* line;
        const wchar_t* rest;  // what remains after the timestamp, nullptr = no timestamp
    };
    const Case cases[] = {
        {L"[19:17] Emi: hi", L"Emi: hi"},
        {L"C9;19J Krypts hecken etwas", L"Krypts hecken etwas"},
        {L"C920J entdeckt! Modr", L"entdeckt! Modr"},
        {L"[92 9J Es kommen Berichte", L"Es kommen Berichte"},
        {L"19:35) entdeckt! Der", L"entdeckt! Der"},
        {L"19:37 JIM) Dr Hamwick: x", L"IM) Dr Hamwick: x"},
        {L"1927 J Bob: hey", L"Bob: hey"},
        {L"t9\u202220J Text", L"Text"},
        {L"C9;f7J Text", L"Text"},
        {L"19:42 JCSJ Pell: Thanks!", L"CSJ Pell: Thanks!"},
        {L"1234 Gold fehlen noch", nullptr},
        {L"Jemand da?", nullptr},
        {L"C4 ist kaputt", nullptr},
        {L"12 Leute fehlen", nullptr},
    };
    for (const Case& c : cases) {
        const std::wstring s = c.line;
        const size_t n = OcrTimestampLength(s);
        if (!c.rest) {
            CHECK(n == 0);
            if (n != 0) std::printf("   unexpected timestamp in: %ls\n", c.line);
        } else {
            CHECK(n > 0 && Trim(s.substr(n)) == c.rest);
            if (!(n > 0 && Trim(s.substr(n)) == c.rest)) std::printf("   timestamp case: %ls -> %zu\n", c.line, n);
        }
    }
    Channel ch = Channel::Unknown;
    CHECK(FuzzyTagLength(L"CSJ Pell", &ch) == 3 && ch == Channel::Say);
    CHECK(FuzzyTagLength(L"IM) Dr", &ch) == 3 && ch == Channel::Map);
    CHECK(FuzzyTagLength(L"[Sagen) Bob: x", &ch) == 7 && ch == Channel::Say);
    CHECK(FuzzyTagLength(L"CKontakteJ Emi ist online", &ch) == 10 && ch == Channel::System);
    CHECK(FuzzyTagLength(L"[Gilde] Carl: x", &ch) == 7 && ch == Channel::Guild);
    CHECK(FuzzyTagLength(L"Couch: x", &ch) == 0);
    auto m = ParseChatLine(L"19:42 JCSJ Pell The Unnamed: Thanks!");
    CHECK(m.stamped && m.speaker == L"Pell The Unnamed" && m.text == L"Thanks!");
    m = ParseChatLine(L"[Sagen]");
    CHECK(m.tagOnly);
    m = ParseChatLine(L"C9;19J Krypts hecken etwas Gro\u00dfes");
    CHECK(m.stamped && m.speaker.empty() && m.text == L"Krypts hecken etwas Gro\u00dfes");
}





static void TestChatTextFilter() {
    CHECK(LooksLikeChatText(L"wer kommt mit zum Tequatl?"));
    CHECK(LooksLikeChatText(L"gg"));
    CHECK(LooksLikeChatText(L"\u0645\u0631\u062d\u0628\u0627"));
    CHECK(!LooksLikeChatText(L"\u00a7\u00b0^~ |l \u00a6\u00ac"));
    CHECK(!LooksLikeChatText(L"~~ ** =="));
    CHECK(!LooksLikeChatText(L"123 456"));
}

static void TestModelList() {
    auto a = ParseModelList(R"({"object":"list","data":[{"id":"qwen2.5:7b"},{"id":"llama3.1:8b"}]})");
    CHECK(a.size() == 2 && a[0] == L"llama3.1:8b");
    auto b = ParseModelList(R"({"models":[{"name":"qwen2.5:7b","size":1},{"name":"gemma2:9b"}]})");
    CHECK(b.size() == 2 && b[0] == L"gemma2:9b");
    CHECK(ParseModelList("nope").empty());
    const std::string req = BuildLlmRequest({{{L"Hallo Lête", false}}}, L"English", L"m", true);
    CHECK(req.find("recognition errors") != std::string::npos);
    CHECK(BuildLlmRequest({{{L"x", false}}}, L"English", L"m").find("recognition errors") == std::string::npos);
    // Cloud reasoning models reject any temperature: left out for them.
    CHECK(BuildLlmRequest({{{L"x", false}}}, L"English", L"m").find("temperature") != std::string::npos);
    CHECK(BuildLlmRequest({{{L"x", false}}}, L"English", L"m", false, false).find("temperature") == std::string::npos);
}

static void TestCloudMt() {
    CHECK(GoogleLang(L"EN-GB") == L"en" && GoogleLang(L"ZH-HANS") == L"zh-CN" && GoogleLang(L"ZH-HANT") == L"zh-TW");
    CHECK(GoogleLang(L"PT-PT") == L"pt-PT" && GoogleLang(L"PT-BR") == L"pt" && GoogleLang(L"NB") == L"no");
    CHECK(MicrosoftLang(L"ZH-HANS") == L"zh-Hans" && MicrosoftLang(L"DE") == L"de" && MicrosoftLang(L"") == L"");
    // Plain text without protected parts; HTML with them.
    const CloudMtPayload plain = BuildGooglePayload({{{L"hello", false}}}, L"", L"DE");
    CHECK(!plain.html && plain.json.find("\"format\":\"text\"") != std::string::npos);
    CHECK(plain.json.find("\"source\"") == std::string::npos && plain.json.find("\"target\":\"de\"") != std::string::npos);
    const CloudMtPayload prot = BuildGooglePayload({{{L"hi ", false}, {L"Kiro Vale", true}, {L" & bye", false}}}, L"EN-GB", L"DE");
    CHECK(prot.html && prot.json.find("translate=") != std::string::npos && prot.json.find("&amp; bye") != std::string::npos);
    const auto g = ParseGoogleResponse(
        R"({"data":{"translations":[{"translatedText":"hallo <span translate=\"no\" class=\"notranslate\">Kiro Vale</span> &amp; tsch&#252;ss","detectedSourceLanguage":"en"}]}})",
        true, 1);
    CHECK(g.size() == 1 && g[0].ok && g[0].text == L"hallo Kiro Vale & tschüss" && g[0].detectedSource == L"EN");
    const auto ge = ParseGoogleResponse(R"({"error":{"code":400,"message":"API key not valid."}})", false, 2);
    CHECK(ge.size() == 2 && !ge[1].ok && ge[1].error.find(L"API key not valid") != std::wstring::npos);
    CHECK(MicrosoftQuery(L"", L"ZH-HANS", true) == L"/translate?api-version=3.0&to=zh-Hans&textType=html");
    CHECK(BuildMicrosoftPayload({{{L"a", false}}, {{L"b", false}}}).json == "[{\"Text\":\"a\"},{\"Text\":\"b\"}]");
    const auto m = ParseMicrosoftResponse(
        R"([{"detectedLanguage":{"language":"fr","score":1.0},"translations":[{"text":"Hallo","to":"de"}]}])", false, 1);
    CHECK(m.size() == 1 && m[0].ok && m[0].text == L"Hallo" && m[0].detectedSource == L"FR");
    CHECK(!ParseMicrosoftResponse(R"({"error":{"code":401000,"message":"bad key"}})", false, 1)[0].ok);
    // LibreTranslate: any address, arrays in and out.
    CHECK(LibreTranslateUrl(L"http://localhost:5000/") == L"http://localhost:5000/translate");
    CHECK(LibreTranslateUrl(L"https://x.example/translate") == L"https://x.example/translate");
    CHECK(LibreLang(L"") == L"auto" && LibreLang(L"ZH-HANT") == L"zt" && LibreLang(L"EN-GB") == L"en");
    const CloudMtPayload lp = BuildLibrePayload({{{L"hola", false}}}, L"", L"DE", L"k1");
    CHECK(lp.json.find("\"source\":\"auto\"") != std::string::npos && lp.json.find("\"api_key\":\"k1\"") != std::string::npos);
    const auto lr = ParseLibreResponse(R"({"translatedText":["hallo"],"detectedLanguage":[{"confidence":90,"language":"es"}]})", false, 1);
    CHECK(lr.size() == 1 && lr[0].ok && lr[0].text == L"hallo" && lr[0].detectedSource == L"ES");
    CHECK(!ParseLibreResponse(R"({"error":"Invalid API key"})", false, 1)[0].ok);
}



static void TestMyWords() {
    MyWords w;
    w.Parse(L"# comment\nfinds = finde es\r\nbrb = bin gleich zurück\nKiro\n bad line with spaces = x\n");
    CHECK(w.Size() == 3);
    CHECK(w.MeaningOf(L"FINDS") == L"finde es" && w.Knows(L"kiro") && w.MeaningOf(L"kiro").empty());
    // Before translating: whole words only, the capital at the start kept, the rest untouched.
    CHECK(w.Expand(L"ich finds schön") == L"ich finde es schön");
    CHECK(w.Expand(L"Finds super, brb!") == L"Finde es super, bin gleich zurück!");
    CHECK(w.Expand(L"findest du") == L"findest du");
    CHECK(w.Expand(L"Kiro kommt") == L"Kiro kommt");  // only marked as correct: stays
    w.Set(L"finds", L"finde es halt");
    CHECK(w.Size() == 3 && w.MeaningOf(L"finds") == L"finde es halt");
    MyWords again;
    again.Parse(w.Serialize());
    CHECK(again.Size() == 3 && again.MeaningOf(L"brb") == L"bin gleich zurück");
    CHECK(again.Remove(L"BRB") && again.Size() == 2);
}


static void TestWordTriples() {
    WordModel m;
    // "du" is followed by many words; after "kommst du" it is "mit".
    for (int i = 0; i < 5; ++i) m.Learn(L"bist du da");
    for (int i = 0; i < 4; ++i) m.Learn(L"hast du zeit");
    for (int i = 0; i < 2; ++i) m.Learn(L"kommst du mit");
    CHECK(!m.Next(L"du", 3).empty() && m.Next(L"du", 3)[0] == L"da");      // one word: the most frequent
    CHECK(!m.Next(L"du", 3, L"kommst").empty() && m.Next(L"du", 3, L"kommst")[0] == L"mit");  // two words: the sentence
    CHECK(m.Complete(L"m", L"du", 1, L"kommst") == std::vector<std::wstring>{L"mit"});
    // Sure enough to offer before a letter is typed: only where one word clearly wins.
    CHECK(m.NextSure(L"du", L"bist") == L"da");     // 5 of 5 after "bist du"
    CHECK(m.NextSure(L"du").empty());              // after "du" alone: da 5, zeit 4, mit 2 – no clear winner
    CHECK(m.NextSure(L"du", L"kommst").empty());   // seen only twice
    // Saved and loaded again (triples are pairs with "a\x1fb" as their first word).
    WordModel back;
    back.Parse(m.Serialize());
    CHECK(!back.Next(L"du", 3, L"kommst").empty() && back.Next(L"du", 3, L"kommst")[0] == L"mit");
    // Choosing a suggestion teaches its place: after "hast du" the word bar offered "zeit", you took "lust" twice.
    m.Learn(L"hast du lust");
    m.Chose(L"hast", L"du", L"lust", 1.0);
    m.Chose(L"hast", L"du", L"lust", 1.0);
    CHECK(m.Next(L"du", 1, L"hast") == std::vector<std::wstring>{L"lust"});
    CHECK(m.Next(L"du", 1, L"kommst") == std::vector<std::wstring>{L"mit"});  // other places keep theirs
    m.Chose(L"", L"", L"https://x.org", 1.0);                                  // links are never learned
    CHECK(m.Count(L"https://x.org") == 0);
    // Forgetting a word removes its triples too.
    CHECK(back.Forget(L"kommst"));
    CHECK(back.Next(L"du", 3, L"kommst")[0] == L"da");

    // Pair and triple counts directly accessible.
    for (int i = 0; i < 2; ++i) m.Learn(L"seid ihr bereit");
    for (int i = 0; i < 2; ++i) m.Learn(L"wir wollen reisen");
    CHECK(m.PairCount(L"ihr", L"bereit") == 2.0);
    CHECK(m.TripleCount(L"seid", L"ihr", L"bereit") == 2.0);
    CHECK(m.TripleCount(L"habt", L"ihr", L"bereit") == 0.0);

    // Fuzzy completion with trigram context.
    CHECK(m.CompleteFuzzy(L"bereti", L"ihr", 1, nullptr, L"seid") == std::vector<std::wstring>{L"bereit"});

    // ChooseCorrection without context picks the first dictionary suggestion (reiten over reisen for typo reiden),
    // but with sentence context (wir wollen ...) the matching triple promotes reisen.
    CHECK(ChooseCorrection(L"reiden", {L"reiten", L"reisen"}, m) == L"reiten");
    CHECK(ChooseCorrection(L"reiden", {L"reiten", L"reisen"}, m, L"wollen", L"wir") == L"reisen");
}

static void TestWordModelWeights() {
    WordModel m;
    // "mal" is written more often, but after "kommst du" it is "mit": the sentence outweighs frequency.
    for (int i = 0; i < 12; ++i) m.Learn(L"das mal sehen");
    for (int i = 0; i < 2; ++i) m.Learn(L"kommst du mit");
    CHECK(m.Complete(L"m", L"", 1) == std::vector<std::wstring>{L"mal"});
    CHECK(m.Complete(L"m", L"du", 1, L"kommst") == std::vector<std::wstring>{L"mit"});
    // One accidental triple does not beat a word you use all the time.
    WordModel one;
    for (int i = 0; i < 30; ++i) one.Learn(L"mal");
    one.Learn(L"kommst du mit");
    CHECK(one.Complete(L"m", L"du", 1, L"kommst") == std::vector<std::wstring>{L"mal"});
    // Forgetting by use: counts shrink, a word used twice stays known, a pair seen once long ago goes.
    WordModel f;
    f.Learn(L"alpha beta");
    for (int i = 0; i < 10; ++i) f.Learn(L"gamma");
    const double before = f.Count(L"gamma");
    f.Decay();
    CHECK(f.Count(L"gamma") < before && f.Count(L"gamma") > 2.0);
    f.Learn(L"delta");
    f.Learn(L"delta");
    for (int i = 0; i < 40; ++i) f.Decay();
    CHECK(f.Knows(L"delta"));                 // still known: never corrected away
    CHECK(f.Next(L"alpha", 3).empty());       // the old pair has faded
}

static void TestGw2Abbreviations() {
    auto kept = [](const std::wstring& text) {
        std::wstring out;
        for (const Segment& seg : ProtectForTranslation(text, nullptr, &BuiltinKeepWords()).segments)
            if (seg.keep) out += (out.empty() ? L"" : L"|") + seg.text;
        return out;
    };
    // Commands from people to people stay as they are, in any spelling.
    CHECK(kept(L"use tp pls") == L"tp");
    CHECK(kept(L"CC now, then dps") == L"CC|dps");
    CHECK(kept(L"lfg fotm t4") == L"lfg|fotm|t4");
    // Ordinary words are translated: never kept.
    CHECK(kept(L"I might come later").empty());
    CHECK(kept(L"it's hot in here").empty());
    CHECK(kept(L"mes amis sont la").empty());
    CHECK(kept(L"la casa de mi amigo").empty());
    // The same letters written like an abbreviation are kept.
    CHECK(kept(L"HoT meta in LA") == L"HoT|meta|LA");
    CHECK(kept(L"DE needs CM") == L"DE|CM");
    CHECK(LooksLikeAbbreviation(L"CoF") && LooksLikeAbbreviation(L"WvW") && !LooksLikeAbbreviation(L"La"));
    // Never marked as a spelling mistake.
    CHECK(IsKeepWord(BuiltinSpellIgnore(), L"mes") && IsKeepWord(BuiltinSpellIgnore(), L"AC"));
    CHECK(!IsKeepWord(BuiltinSpellIgnore(), L"ac"));
    // A name the user taught: kept exactly as taught, the ordinary word is translated.
    WordSet withName = BuiltinKeepWords();
    withName.insert(L"=Fallen");
    CHECK(IsKeepWord(withName, L"Fallen") && !IsKeepWord(withName, L"fallen"));
}

static void TestLooksLikeSlip() {
    const KeyLayout k = Qwertz();
    // Slips that really happen.
    CHECK(LooksLikeSlip(L"shon", L"schon", k, false));            // left out inside
    CHECK(LooksLikeSlip(L"eifnach", L"einfach", k, false));       // swapped
    CHECK(LooksLikeSlip(L"aaber", L"aber", k, false));            // doubled
    CHECK(LooksLikeSlip(L"funnktioniert", L"funktioniert", k, true));
    CHECK(LooksLikeSlip(L"tippsuport", L"tippsupport", k, false)); // a double typed once
    CHECK(LooksLikeSlip(L"siund", L"sind", k, false));            // a key next door caught too
    CHECK(LooksLikeSlip(L"hsllo", L"hallo", k, false));           // key next door
    CHECK(LooksLikeSlip(L"ahllo", L"hallo", k, true));            // first two swapped
    CHECK(LooksLikeSlip(L"micih", L"mich", k, false));            // letter one place too late
    CHECK(LooksLikeSlip(L"mcich", L"mich", k, false));            // letter one place too early
    CHECK(BuiltinSpellIgnore().count(L"ohje"));                   // interjection: never a typo
    // Grammar, other languages, word parts: no slips.
    CHECK(!LooksLikeSlip(L"gehts", L"geht", k, false));
    CHECK(!LooksLikeSlip(L"habs", L"habe", k, false));
    CHECK(!LooksLikeSlip(L"habs", L"hab's", k, false));  // a way of writing, the user's choice
    CHECK(!LooksLikeSlip(L"with", L"witz", k, false));
    CHECK(!LooksLikeSlip(L"window", L"windows", k, false));
    CHECK(!LooksLikeSlip(L"auflösungs", L"auflösung", k, false));
    CHECK(!LooksLikeSlip(L"debug", L"debut", k, false));
    CHECK(!LooksLikeSlip(L"dont", L"dort", k, false));            // n and r are not next door
    CHECK(!LooksLikeSlip(L"once", L"ponce", k, false));           // the first letter stays
    CHECK(!LooksLikeSlip(L"shon", L"schon", k, true));            // strict: only swaps and doubles
}

static void TestGermanContractions() {
    CHECK(ExpandGermanContractions(L"habs schon, gehts dir gut? Gibt's das") == L"hab es schon, geht es dir gut? Gibt es das");
    CHECK(ExpandGermanContractions(L"nice hats and old wars, finds") == L"nice hats and old wars, finds");
    CHECK(IsGermanContraction(L"wirds") && IsGermanContraction(L"klappt’s") && !IsGermanContraction(L"teams"));
}

static void TestTypoMemory() {
    TypoMemory m;
    m.Add(L"shon", L"schon");
    m.Add(L"shon", L"schon");
    CHECK(m.FixFor(L"Shon") == L"schon");
    m.Add(L"tset", L"test");
    m.Add(L"tset", L"set");
    CHECK(m.FixFor(L"tset").empty());  // two fixes compete
    m.Add(L"tset", L"test");
    m.Add(L"tset", L"test");
    CHECK(m.FixFor(L"tset") == L"test");  // 3 of 4
    TypoMemory n;
    n.Parse(m.Serialize());
    CHECK(n.FixFor(L"shon") == L"schon" && n.FixFor(L"tset") == L"test" && !n.Dirty());
    n.ForgetFix(L"schon");
    CHECK(n.FixFor(L"shon").empty());
    CHECK(n.Forget(L"tset") && n.Size() == 0);
}

static void TestWordCase() {
    // The way you write a word most of the time wins: one "Fallen" (a name) does not capitalise the verb.
    WordModel m;
    for (int i = 0; i < 3; ++i) m.Learn(L"ich bin fallen gelassen");
    m.Learn(L"hi Fallen");
    CHECK(m.Complete(L"fal", L"", 1) == std::vector<std::wstring>{L"fallen"});
    // A noun you always write with a capital stays capital; the capital at the start of a message says nothing.
    for (int i = 0; i < 3; ++i) m.Learn(L"der Tisch ist da");
    m.Learn(L"tisch");
    CHECK(m.Complete(L"tis", L"", 1) == std::vector<std::wstring>{L"Tisch"});
    m.Learn(L"Gelassen bleiben");
    CHECK(m.Complete(L"gel", L"", 1) == std::vector<std::wstring>{L"gelassen"});
    // Saved and loaded: the counts of both spellings come along.
    WordModel back;
    back.Parse(m.Serialize());
    CHECK(back.Complete(L"fal", L"", 1) == std::vector<std::wstring>{L"fallen"});
    CHECK(back.Complete(L"tis", L"", 1) == std::vector<std::wstring>{L"Tisch"});
}

static void TestStampShape() {
    // Timestamps the recognition mangled (seen in a real whisper chat), each the start of its own message.
    Channel c = Channel::Unknown;
    CHECK(StampShapeLength(L"CO•.54JCWJ An A Brave Knight: hope you are better", &c) == 10 && c == Channel::Whisper);
    CHECK(StampShapeLength(L"CO:58JCWJ Von A Brave Knight: only those", &c) == 9 && c == Channel::Whisper);
    CHECK(StampShapeLength(L"CD;56JCWJ An A Brave Knight: you will be here", &c) == 9);
    CHECK(StampShapeLength(L"CIIOOJCWJ Von A Brave Knight: discord open", &c) == 9 && c == Channel::Whisper);
    CHECK(StampShapeLength(L"CO•.50JCS) Gwyn: hi", &c) == 10 && c == Channel::Say);
    CHECK(StampShapeLength(L"[11:05] Bedrohung entdeckt!", &c) == 7 && c == Channel::Unknown);
    // Words are no timestamps.
    CHECK(StampShapeLength(L"Cool idea", &c) == 0);
    CHECK(StampShapeLength(L"Dodo is here", &c) == 0);
    CHECK(StampShapeLength(L"ok test when?", &c) == 0);
    // The parser takes the tag as the channel (the whisper prefix "Von" is removed later, in BuildMessages).
    Channel tag = Channel::Unknown;
    const ChatMessage m = ParseChatLine(L"CO:58JCWJ Von A Brave Knight: only those who face the fury", &tag);
    CHECK(m.stamped && tag == Channel::Whisper && m.speaker == L"Von A Brave Knight" &&
          m.text == L"only those who face the fury");
}

static void TestInputLineTags() {
    // The GW2 input line "[Gilde][ABC]" with misread brackets (and the chat bubble in front) is never a message.
    CHECK(GluedTagsAtStart(L"O tüldeJCABCJ") > 0);
    CHECK(GluedTagsAtStart(L"CGIdeJCABCJ") > 0);
    CHECK(GluedTagsAtStart(L"[Gilde][ABC] hello") > 0);
    CHECK(GluedTagsAtStart(L"platziert einige Artefakte") == 0);
    CHECK(GluedTagsAtStart(L"Nia Stone: grrr") == 0);
    CHECK(GluedTagsAtStart(L"or or or") == 0);
    // The guild tag in front of a name, also with misread brackets.
    CHECK(StripGuildTag(L"tABCJ Nia Stone") == L"Nia Stone");
    CHECK(StripGuildTag(L"[ABC] Nia Stone") == L"Nia Stone");
    CHECK(StripGuildTag(L"IABCJ Nia Stone") == L"Nia Stone");
    CHECK(StripGuildTag(L"Nia Stone") == L"Nia Stone");
    CHECK(StripGuildTag(L"Tom Brave") == L"Tom Brave");
    // In a chat picture: the input line is not glued to the last message, the guild tag leaves the name.
    std::vector<OcrLine> lines(2);
    lines[0].text = L"[12:19] [G] [ABC] Nia Stone: it works well";
    lines[0].top = 0;
    lines[0].height = 20;
    lines[0].color = {240, 220, 120};
    lines[1].text = L"O tüldeJCABCJ";
    lines[1].top = 22;
    lines[1].height = 20;
    lines[1].color = {240, 220, 120};
    const auto msgs = BuildMessages(lines, DefaultChannelColors());
    CHECK(msgs.size() == 1);
    if (!msgs.empty()) CHECK(msgs[0].text == L"it works well" && msgs[0].speaker == L"Nia Stone");
}

static void TestPhraseMemory() {
    WordModel m;
    for (int i = 0; i < 4; ++i) m.Learn(L"gute nacht bis morgen mit micro");
    m.Learn(L"gute nacht allerseits");
    // After "gute nacht" the whole rest you always write.
    CHECK((m.ContinueSure(L"nacht", L"gute") == std::vector<std::wstring>{L"bis", L"morgen", L"mit", L"micro"}));
    // Never round in circles, even when the phrase leads back to its start.
    WordModel loop;
    for (int i = 0; i < 5; ++i) loop.Learn(L"go go go");
    CHECK(loop.ContinueSure(L"go", L"go").empty());
    // At most maxWords.
    CHECK(m.ContinueSure(L"nacht", L"gute", 2).size() == 2);
    // Nothing sure: nothing offered.
    CHECK(m.ContinueSure(L"irgendwas", L"").empty());
}

static void TestActiveBottom() {
    // Ten text lines 20 px apart; two paragraphs (gap after line 3).
    std::vector<OcrLine> lines;
    for (int i = 0; i < 10; ++i) {
        OcrLine l;
        l.text = L"line number " + std::to_wstring(i) + (i == 3 ? L"." : L"");
        l.top = i * 20 + (i >= 4 ? 30 : 0);
        l.height = 14;
        l.left = 10;
        l.width = 200;
        lines.push_back(l);
    }
    const std::vector<ChatMessage> all = BuildFreeTextMessages(lines);
    CHECK(all.size() == 2);
    // Only what reaches into the lowest 6 lines: the old paragraph above is left out, the lower one stays.
    const std::vector<ChatMessage> active = KeepActiveBottom(all, lines, 6);
    CHECK(active.size() == 1 && active[0].text.find(L"line number 9") != std::wstring::npos);
    // Few lines: everything stays.
    CHECK(KeepActiveBottom(all, lines, 20).size() == 2);
}


static void TestSureLanguage() {
    // Another script decides at once, even short.
    CHECK(SureLanguage(L"привет", L"") == L"RU");
    CHECK(SureLanguage(L"مرحبا", L"") == L"AR");
    // Latin: three words and the Windows detection, or telltale letters.
    CHECK(SureLanguage(L"quelqu'un pour le boss mondial", L"FR") == L"FR");
    CHECK(SureLanguage(L"ok np", L"EN").empty());            // too short: unsure, nothing sent
    CHECK(SureLanguage(L"Kiro Vale", L"").empty());           // a name
    CHECK(SureLanguage(L"mañana", L"").find(L"ES") == 0); // ñ says Spanish
    CHECK(SureLanguage(L"wer kommt mit", L"").empty());       // three words but no detection, no telltale letters
    // Short lines: words of one language only.
    CHECK(SureLanguage(L"merci", L"") == L"FR");
    CHECK(SureLanguage(L"gracias amigo!", L"") == L"ES");
    CHECK(SureLanguage(L"Grazie mille", L"") == L"IT");
    CHECK(SureLanguage(L"c'est bon", L"") == L"FR");
    CHECK(SureLanguage(L"thanks merci", L"").empty());       // two languages: unsure
    // The telltale words of different languages never overlap: "je" is Dutch too, "mille" French, "porque",
    // "amigo", "vamos" Portuguese.
    CHECK(SureLanguage(L"je bent", L"").empty());
    CHECK(SureLanguage(L"mille", L"").empty());
    CHECK(SureLanguage(L"vamos amigo", L"").empty());
    CHECK(SureLanguage(L"ty", L"").empty());
}

static void TestFreeText() {
    auto line = [](const wchar_t* t, int top, int left = 10) {
        OcrLine l;
        l.text = t;
        l.top = top;
        l.height = 20;
        l.left = left;
        l.width = 300;
        return l;
    };
    // Two lines close together are one paragraph, a gap starts the next; a
    // hyphenated word at the line end is joined again.
    const auto m = BuildFreeTextMessages({line(L"The quick brown fox jumps over the lazy", 0),
                                          line(L"dog and runs into the for-", 24), line(L"est.", 48),
                                          line(L"Second paragraph here.", 100)});
    CHECK(m.size() == 2);
    CHECK(m.size() == 2 && m[0].text == L"The quick brown fox jumps over the lazy dog and runs into the forest.");
    CHECK(m.size() == 2 && m[1].text == L"Second paragraph here." && m[1].freeText);
    // Hyphen kept when digit precedes or capital follows.
    const auto hy1 = BuildFreeTextMessages({line(L"ein 10-", 0), line(L"facher Gewinn", 24)});
    CHECK(hy1.size() == 1 && hy1[0].text == L"ein 10-facher Gewinn");
    const auto hy2 = BuildFreeTextMessages({line(L"in Nord-", 0), line(L"Rheinland", 24)});
    CHECK(hy2.size() == 1 && hy2[0].text == L"in Nord-Rheinland");
    // A long text is cut after a sentence.
    const auto cut = BuildFreeTextMessages({line(L"One sentence here.", 0), line(L"Another one follows.", 24)}, 20);
    CHECK(cut.size() == 2);
    // A clearly different indent (another column) is its own paragraph.
    CHECK(BuildFreeTextMessages({line(L"left column", 0, 10), line(L"right column", 24, 400)}).size() == 2);
    // Two columns side by side, lines sorted top to bottom alternate: each column stays one paragraph.
    const auto cols = BuildFreeTextMessages({line(L"New", 0, 10), line(L"The quick brown fox", 0, 400),
                                             line(L"Projects", 24, 10), line(L"jumps over the dog.", 24, 400)});
    CHECK(cols.size() == 2);
    CHECK(cols.size() == 2 && cols[0].text == L"New Projects" && cols[1].text == L"The quick brown fox jumps over the dog.");
    // The same paragraph read again: grown while typing, or read slightly differently.
    CHECK(SameFreeParagraph(L"es verhält sich relativ flott", L"es verhält sich relativ flott! ich finds super"));
    CHECK(SameFreeParagraph(L"when scanning it seems to read the lines mixed up",
                            L"when scanning it seems to read the lines mixed up not line by line"));
    CHECK(SameFreeParagraph(L"the window gets the same long list as before", L"the windovv gets the same long list as before"));
    CHECK(!SameFreeParagraph(L"Who wants to join the world boss?", L"Thanks for the help, see you later"));
    // Noise from UI icons and half-hidden letters is not translated; normal text is.
    CHECK(!LooksLikeFreeText(L"s•ch c:em'•"));
    CHECK(!LooksLikeFreeText(L"not-y •st öffent •ches Lloersetzungsgeciächtn•s:"));
    CHECK(!LooksLikeFreeText(L"VyVemory -3as•s: o:"));
    CHECK(LooksLikeFreeText(L"Ohne Konto 5.000 Zeichen am Tag. Mit deiner E-Mail-Adresse 50.000 am Tag – keine Anmeldung."));
    CHECK(LooksLikeFreeText(L"(Hallo, wer kommt mit?)"));
    CHECK(!LooksLikeFreeText(L"123 456"));
}


// ---- voice input (core/voice) ----------------------------------------------
static void TestVoiceCatalog() {
    VoiceCatalog cat;
    CHECK(ParseVoiceCatalog(DefaultVoiceCatalogJson(), &cat));
    CHECK(cat.date == "2026-10-12");
    CHECK(cat.models.size() == 4);
    CHECK(!cat.Find("tiny-q5_1"));  // loops with the short context (voice_bench)
    const VoiceModelInfo* rec = cat.Recommended();
    CHECK(rec && rec->id == "small-q5_1" && rec->name == "Whisper small (q5_1)");
    CHECK(rec && rec->bytes == 190085487ull && rec->licence == "MIT" && !rec->streaming && !rec->englishOnly);
    CHECK(rec && rec->sha256 == "ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb");
    const VoiceModelInfo* turbo = cat.Find("large-v3-turbo-q5_0");
    CHECK(turbo && turbo->bytes == 574041195ull && turbo->file == "ggml-large-v3-turbo-q5_0.bin");
    CHECK(turbo && turbo->url == "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q5_0.bin");
    CHECK(!cat.Find("nope"));
    for (const auto& m : cat.models) {
        CHECK(m.url.size() > m.file.size() && m.url.compare(m.url.size() - m.file.size(), m.file.size(), m.file) == 0);
        CHECK(m.provider == "whisper" && m.langs.size() == 14 && m.ramMb > 0 && m.sha256.size() == 64);
    }
    // Quality: Arabic needs at least small; a bigger model is never worse.
    CHECK(cat.Find("base-q5_1") && cat.Find("base-q5_1")->QualityFor("ar") == 0);
    CHECK(rec && rec->QualityFor("ar") == 1 && rec->QualityFor("de") == 2 && rec->QualityFor("en") == 3);
    CHECK(turbo && turbo->QualityFor("ar") == 2 && turbo->QualityFor("de") == 3);
    CHECK(rec && rec->QualityFor("sv") == 1);  // not listed: the model's weakest
    for (const char* lang : {"en", "de", "fr", "es", "it", "pt", "nl", "pl", "ru", "tr", "ar", "zh", "ja", "ko"})
        for (size_t i = 1; i < cat.models.size(); ++i)
            CHECK(cat.models[i].QualityFor(lang) >= cat.models[i - 1].QualityFor(lang));
    VoiceModelInfo bare;
    CHECK(bare.QualityFor("de") == 2);
    bare.englishOnly = true;
    CHECK(bare.QualityFor("de") == 0 && bare.QualityFor("en") == 2);

    // Validation: a bad entry is skipped, never offered.
    auto entry = [](const std::string& file, const std::string& url, const std::string& sha, const std::string& bytes) {
        std::string e = R"({"id":"m)" + std::to_string(file.size()) + R"(")";
        if (!file.empty()) e += R"(,"file":")" + file + R"(")";
        e += R"(,"url":")" + url + R"(","sha256":")" + sha + R"(")";
        if (!bytes.empty()) e += R"(,"bytes":)" + bytes;
        return e + "}";
    };
    auto doc = [](const std::string& entries) { return R"({"date":"t","models":[)" + entries + "]}"; };
    const std::string url = "https://example.com/m.bin";
    const std::string sha = "AE85E4A935D7A567BD102FE55AFC16BB595BDB618E11B2FC7591BC08120411BB";
    VoiceCatalog c;
    CHECK(ParseVoiceCatalog(doc(entry("m.bin", url, sha, "1234")), &c) && c.models.size() == 1);
    CHECK(c.models.size() == 1 && c.models[0].bytes == 1234 && c.models[0].provider == "whisper" &&
          c.models[0].sha256 == "ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb");
    CHECK(c.Recommended() == &c.models[0]);  // none marked: the first
    CHECK(c.models.size() == 1 && c.models[0].langs.empty() && c.models[0].QualityFor("de") == 2);
    CHECK(!ParseVoiceCatalog(doc(entry("m.bin", "http://example.com/m.bin", sha, "1234")), &c) && c.models.empty());
    CHECK(!ParseVoiceCatalog(doc(entry("m.bin", "ftp://example.com/m.bin", sha, "1234")), &c));
    CHECK(!ParseVoiceCatalog(doc(entry("m.bin", url, sha.substr(1), "1234")), &c));
    CHECK(!ParseVoiceCatalog(doc(entry("m.bin", url, "g" + sha.substr(1), "1234")), &c));
    CHECK(!ParseVoiceCatalog(doc(entry("../m.bin", url, sha, "1234")), &c));
    CHECK(!ParseVoiceCatalog(doc(entry("sub/m.bin", url, sha, "1234")), &c));
    CHECK(!ParseVoiceCatalog(doc(entry("sub\\\\m.bin", url, sha, "1234")), &c));
    CHECK(!ParseVoiceCatalog(doc(entry("C:m.bin", url, sha, "1234")), &c));
    CHECK(!ParseVoiceCatalog(doc(entry("", url, sha, "1234")), &c));
    CHECK(!ParseVoiceCatalog(doc(entry("m.bin", url, sha, "")), &c));
    CHECK(!ParseVoiceCatalog(doc(entry("m.bin", url, sha, "0")), &c));
    CHECK(!ParseVoiceCatalog(doc(entry("m.bin", url, sha, "-5")), &c));
    CHECK(!ParseVoiceCatalog(doc(entry("m.bin", url, sha, "1.5")), &c));
    CHECK(!ParseVoiceCatalog(doc(entry("m.bin", url, sha, "\"1234\"")), &c));
    CHECK(ParseVoiceCatalog(doc(entry("a/b.bin", url, sha, "1") + "," + entry("ok.bin", url, sha, "2")), &c) &&
          c.models.size() == 1 && c.models[0].file == "ok.bin");
    CHECK(!ParseVoiceCatalog("not json", &c));
    CHECK(!ParseVoiceCatalog("{}", &c));
    CHECK(!ParseVoiceCatalog(R"({"models":[]})", &c));
}

static void AddNoise(std::vector<float>& v, float amp, unsigned seed) {
    unsigned x = seed;
    for (auto& s : v) {
        x = x * 1664525u + 1013904223u;
        s += amp * (static_cast<float>(x >> 8) / 8388608.0f - 1.0f);
    }
}

static void AddTone(std::vector<float>& v, size_t from, size_t to, float amp, double hz = 440, int rate = 16000) {
    for (size_t i = from; i < to && i < v.size(); ++i)
        v[i] += amp * static_cast<float>(std::sin(6.283185307179586 * hz * static_cast<double>(i) / rate));
}

static void TestFindSpeech() {
    std::vector<float> a(32000, 0.0f);  // 2 s of digital silence
    SpeechSpan s = FindSpeech(a.data(), a.size());
    CHECK(!s.speech && s.voicedMs == 0 && s.peakDb <= -119);
    CHECK(!FindSpeech(nullptr, 100).speech && !FindSpeech(a.data(), 0).speech);

    std::vector<float> tone(3200, 0.0f);  // 0.2 s tone alone: recording too short
    AddTone(tone, 0, tone.size(), 0.3f);
    s = FindSpeech(tone.data(), tone.size());
    CHECK(!s.speech);

    std::vector<float> b(16000, 0.0f);  // 0.2 s tone in 1 s of quiet noise: too little speech
    AddNoise(b, 0.003f, 1);
    AddTone(b, 4000, 7200, 0.3f);
    s = FindSpeech(b.data(), b.size());
    CHECK(!s.speech && s.voicedMs >= 180 && s.voicedMs <= 240);

    std::vector<float> c(32000, 0.0f);  // 1.5 s tone in 2 s of noise
    AddNoise(c, 0.01f, 2);
    AddTone(c, 4000, 28000, 0.3f);
    s = FindSpeech(c.data(), c.size());
    CHECK(s.speech);
    CHECK(s.begin >= 4000 - 3200 - 320 && s.begin <= 4000 - 3200);
    CHECK(s.end >= 28000 + 3200 && s.end <= 28000 + 3200 + 320);
    CHECK(s.voicedMs >= 1480 && s.voicedMs <= 1540);
    CHECK(s.peakDb > -20 && s.noiseDb > -48 && s.noiseDb < -42);

    std::vector<float> d(32000, 0.0f);  // low noise only
    AddNoise(d, 0.003f, 3);
    CHECK(!FindSpeech(d.data(), d.size()).speech);
    std::vector<float> e(32000, 0.0f);  // loud noise only (fan, wind)
    AddNoise(e, 0.2f, 4);
    s = FindSpeech(e.data(), e.size());
    CHECK(!s.speech && s.voicedMs < 100);
    std::vector<float> f(32000, 0.0f);  // a short click in quiet noise
    AddNoise(f, 0.003f, 5);
    for (size_t i = 16000; i < 16160; ++i) f[i] = (i % 2) ? 0.9f : -0.9f;
    CHECK(!FindSpeech(f.data(), f.size()).speech);

    std::vector<float> g(16000 * 30, 0.0f);  // long recording, speech near the end, padding clamped
    AddNoise(g, 0.005f, 6);
    AddTone(g, g.size() - 8000, g.size(), 0.2f);
    s = FindSpeech(g.data(), g.size());
    CHECK(s.speech && s.end == g.size() && s.begin + 8000 + 3200 + 320 >= g.size() && s.begin + 8000 + 3200 <= g.size());

    std::vector<float> h(16000, 0.0f);  // syllables with short pauses (speech-like)
    AddNoise(h, 0.004f, 7);
    for (size_t k = 0; k < 4; ++k) AddTone(h, 3200 + k * 3200, 3200 + k * 3200 + 2400, 0.25f, 220 + 40.0 * k);
    s = FindSpeech(h.data(), h.size());
    CHECK(s.speech && s.voicedMs >= 560 && s.voicedMs <= 680);

    // Level meter: -60 dBFS = 0, 0 dBFS = 1; only the last 50 ms count.
    CHECK(LevelMeter(a.data(), a.size()) == 0.0f);
    CHECK(LevelMeter(nullptr, 10) == 0.0f);
    std::vector<float> full(1600);
    for (size_t i = 0; i < full.size(); ++i) full[i] = (i % 2) ? 1.0f : -1.0f;
    CHECK(LevelMeter(full.data(), full.size()) == 1.0f);
    std::vector<float> mid(1600, 0.0f);
    AddTone(mid, 0, mid.size(), 0.1f);  // RMS about -23 dBFS
    const float lv = LevelMeter(mid.data(), mid.size());
    CHECK(lv > 0.55f && lv < 0.68f);
    std::vector<float> fade(full);
    for (size_t i = fade.size() - 800; i < fade.size(); ++i) fade[i] = 0.0f;
    CHECK(LevelMeter(fade.data(), fade.size(), 50) == 0.0f && LevelMeter(fade.data(), fade.size(), 100) > 0.9f);
}

static std::wstring Clean(const std::wstring& w) { return CleanTranscript(ToUtf8(w)); }

static void TestCleanTranscript() {
    // Markers go.
    CHECK(Clean(L"[BLANK_AUDIO]") == L"");
    CHECK(Clean(L" [Musik] Hallo zusammen!") == L"Hallo zusammen!");
    CHECK(Clean(L"Hallo [ Silence ] du") == L"Hallo du");
    CHECK(Clean(L"Hallo [Musik].") == L"Hallo.");
    CHECK(Clean(L"(Applaus)") == L"");
    CHECK(Clean(L"(upbeat music) Let's go") == L"Let's go");
    CHECK(Clean(L"Ja (lacht), klar") == L"Ja, klar");
    CHECK(Clean(L"(Musik spielt) Wer kommt mit?") == L"Wer kommt mit?");
    CHECK(Clean(L"(MUSIC) ready") == L"ready" && Clean(L"(CROWD CHEERING) ready") == L"ready");
    CHECK(Clean(L"(...) gleich da") == L"gleich da");
    CHECK(Clean(L"*seufzt* Na gut.") == L"Na gut.");
    CHECK(Clean(L"♪ Hallo ♪") == L"Hallo");
    CHECK(Clean(L"♪ ♫") == L"");
    CHECK(Clean(L"- Hallo.") == L"Hallo.");
    CHECK(Clean(L"– Wer tankt?") == L"Wer tankt?");
    CHECK(Clean(L"  viel   Platz \n hier\t") == L"viel Platz hier");
    CHECK(Clean(L"（拍手）") == L"");  // （拍手）
    // Silence hallucinations, only when they are everything.
    CHECK(Clean(L"Untertitel der Amara.org-Gemeinschaft") == L"");
    CHECK(Clean(L" Untertitel im Auftrag des ZDF für funk, 2017") == L"");
    CHECK(Clean(L"Untertitelung des ZDF, 2020") == L"");
    CHECK(Clean(L"Vielen Dank fürs Zuschauen!") == L"");
    CHECK(Clean(L"Vielen Dank für Ihre Aufmerksamkeit.") == L"");
    CHECK(Clean(L"Thanks for watching!") == L"");
    CHECK(Clean(L"Thank you for watching.") == L"");
    CHECK(Clean(L"Subtitles by the Amara.org community") == L"");
    CHECK(Clean(L" you") == L"" && Clean(L"You.") == L"");
    CHECK(Clean(L".") == L"" && Clean(L"...") == L"" && Clean(L"") == L"");
    CHECK(Clean(L"Продолжение следует...") == L"");
    CHECK(Clean(L"ご視聴ありがとうございました。") == L"");
    CHECK(Clean(L"[Musik] Vielen Dank fürs Zuschauen") == L"");
    // Real text stays.
    CHECK(Clean(L"you too") == L"you too");
    CHECK(Clean(L"Thank you") == L"Thank you");
    CHECK(Clean(L"Vielen Dank fürs Zuschauen, bis morgen") == L"Vielen Dank fürs Zuschauen, bis morgen");
    CHECK(Clean(L"Wir treffen uns um 8 (oder 9) am Tor.") == L"Wir treffen uns um 8 (oder 9) am Tor.");
    CHECK(Clean(L"Ich bin (kurz) weg") == L"Ich bin (kurz) weg");
    CHECK(Clean(L"Das ist (Musik für mich)") == L"Das ist (Musik für mich)");
    CHECK(Clean(L"Gilde (OK)") == L"Gilde (OK)");
    CHECK(Clean(L"Ich komme gleich (bin noch im Dungeon)") == L"Ich komme gleich (bin noch im Dungeon)");
    CHECK(Clean(L"5 - 3 = 2") == L"5 - 3 = 2");
    CHECK(Clean(L"Grüße aus Köln, schön! Ärger? Spaß.") ==
          L"Grüße aus Köln, schön! Ärger? Spaß.");
    CHECK(Clean(L"مرحبا [موسيقى] كيف حالك؟") ==
          L"مرحبا كيف حالك؟");
}

static void TestSplitLive() {
    LiveText t = SplitLive(L"", L"");
    CHECK(t.stable.empty() && t.unsure.empty());
    t = SplitLive(L"anything", L"");
    CHECK(t.stable.empty() && t.unsure.empty());
    t = SplitLive(L"", L"hallo");
    CHECK(t.stable.empty() && t.unsure == L"hallo");
    t = SplitLive(L"hallo wie", L"Hallo, wie geht");
    CHECK(t.stable == L"Hallo, wie" && t.unsure == L"geht");
    t = SplitLive(L"hallo wie geht", L"hallo wie geht");
    CHECK(t.stable == L"hallo wie" && t.unsure == L"geht");  // the last word is always unsure
    t = SplitLive(L"hallo wir", L"hallo wie geht es");
    CHECK(t.stable == L"hallo" && t.unsure == L"wie geht es");
    t = SplitLive(L"hallo wie geht es dir", L"hallo wie");
    CHECK(t.stable == L"hallo" && t.unsure == L"wie");
    t = SplitLive(L"  a   b  c ", L"a  b c d");
    CHECK(t.stable == L"a b c" && t.unsure == L"d");
    t = SplitLive(L"Grüße AN alle", L"grüße an alle!");
    CHECK(t.stable == L"grüße an" && t.unsure == L"alle!");
}

static void TestWhisperSettings() {
    CHECK(WhisperLang(L"DE") == "de");
    CHECK(WhisperLang(L"EN-GB") == "en" && WhisperLang(L"EN-US") == "en");
    CHECK(WhisperLang(L"PT-BR") == "pt" && WhisperLang(L"pt-pt") == "pt");
    CHECK(WhisperLang(L"ZH-HANS") == "zh" && WhisperLang(L"ZH-HANT") == "zh" && WhisperLang(L"zh_TW") == "zh");
    CHECK(WhisperLang(L"NB") == "no" && WhisperLang(L"iw") == "he" && WhisperLang(L"HE") == "he");
    CHECK(WhisperLang(L"fil") == "tl" && WhisperLang(L"de-AT") == "de" && WhisperLang(L" ja ") == "ja");
    CHECK(WhisperLang(L"").empty() && WhisperLang(L"XX").empty() && WhisperLang(L"auto").empty());
    CHECK(WhisperLang(L"ä").empty() && WhisperLang(L"-").empty());
    for (const auto& l : Languages()) CHECK(!WhisperLang(l.code).empty());

    CHECK(WhisperAudioCtx(0) == 256);
    CHECK(WhisperAudioCtx(16000) == 256);         // 1 s
    CHECK(WhisperAudioCtx(16000 * 4) == 264);     // 4 s
    CHECK(WhisperAudioCtx(16000 * 10) == 564);    // 10 s
    CHECK(WhisperAudioCtx(16000 * 28) == 1464);
    CHECK(WhisperAudioCtx(16000 * 30) == 1500);   // the full window
    CHECK(WhisperAudioCtx(16000 * 120) == 1500);

    CHECK(VoiceThreads(0, 8) == 4 && VoiceThreads(0, 4) == 2 && VoiceThreads(0, 1) == 1);
    CHECK(VoiceThreads(0, 0) == 1 && VoiceThreads(0, 32) == 4 && VoiceThreads(-1, 6) == 3);
    CHECK(VoiceThreads(6, 2) == 6 && VoiceThreads(64, 4) == 16);
}

static void TestToMono16k() {
    CHECK(ToMono16k(nullptr, 10, 1, 48000).empty());
    std::vector<float> one = {0.5f};
    CHECK(ToMono16k(one.data(), 0, 1, 48000).empty() && ToMono16k(one.data(), 1, 0, 48000).empty() &&
          ToMono16k(one.data(), 1, 1, 0).empty());

    // 48 kHz stereo sine -> a third of the samples, amplitude kept.
    std::vector<float> mono48(48000, 0.0f);
    AddTone(mono48, 0, mono48.size(), 0.5f, 440, 48000);
    std::vector<float> stereo(mono48.size() * 2);
    for (size_t i = 0; i < mono48.size(); ++i) stereo[2 * i] = stereo[2 * i + 1] = mono48[i];
    std::vector<float> out = ToMono16k(stereo.data(), mono48.size(), 2, 48000);
    CHECK(out.size() == 16000);
    float peak = 0, maxErr = 0;
    for (size_t j = 0; j < out.size(); ++j) {
        peak = std::max(peak, std::fabs(out[j]));
        const float want = 0.5f * static_cast<float>(std::sin(6.283185307179586 * 440.0 * static_cast<double>(j) / 16000));
        maxErr = std::max(maxErr, std::fabs(out[j] - want));
    }
    CHECK(peak > 0.48f && peak < 0.51f);
    CHECK(maxErr < 0.02f);

    // Above 8 kHz is damped by the low-pass (it would fold back into speech).
    std::vector<float> high(48000, 0.0f);
    AddTone(high, 0, high.size(), 0.5f, 15000, 48000);
    out = ToMono16k(high.data(), high.size(), 1, 48000);
    double sq = 0;
    for (float v : out) sq += static_cast<double>(v) * v;
    CHECK(std::sqrt(sq / static_cast<double>(out.size())) < 0.5 * 0.5 / std::sqrt(2.0));

    // 16 kHz mono = copy; 16 kHz stereo = average; 44.1 and 8 kHz lengths; 8 kHz interpolates.
    std::vector<float> m16 = {0.1f, -0.2f, 0.3f};
    CHECK(ToMono16k(m16.data(), m16.size(), 1, 16000) == m16);
    std::vector<float> s16 = {0.2f, 0.4f, -0.2f, -0.4f};
    out = ToMono16k(s16.data(), 2, 2, 16000);
    CHECK(out.size() == 2 && std::fabs(out[0] - 0.3f) < 1e-6f && std::fabs(out[1] + 0.3f) < 1e-6f);
    std::vector<float> m441(44100, 0.25f);
    out = ToMono16k(m441.data(), m441.size(), 1, 44100);
    CHECK(out.size() == 16000 && std::fabs(out[100] - 0.25f) < 1e-5f && std::fabs(out.back() - 0.25f) < 1e-5f);
    std::vector<float> m8 = {0.0f, 1.0f, 0.0f, -1.0f};
    out = ToMono16k(m8.data(), m8.size(), 1, 8000);
    CHECK(out.size() == 8 && std::fabs(out[1] - 0.5f) < 1e-6f && std::fabs(out[2] - 1.0f) < 1e-6f &&
          std::fabs(out[5] + 0.5f) < 1e-6f);
}

static void TestPickVoiceCores() {
    // 4 physical cores with 2 siblings each; the game keeps core 1 (logical 2, 3) and logical 4 busy.
    const std::vector<std::vector<int>> cores{{0, 1}, {2, 3}, {4, 5}, {6, 7}};
    const std::vector<float> busy{0.10f, 0.05f, 0.95f, 0.30f, 0.80f, 0.10f, 0.02f, 0.01f};
    VoiceCorePick p = PickVoiceCores(busy, cores, 0);
    CHECK(p.threads == 2 && p.logical == std::vector<int>({1, 7}));  // free cores 0 and 3, the less busy sibling each
    p = PickVoiceCores(busy, cores, 3);
    CHECK(p.threads == 3 && p.logical == std::vector<int>({1, 5, 7}));  // then the next least busy core
    p = PickVoiceCores(busy, cores, 6);
    CHECK(p.threads == 6 && p.logical.size() == 6);  // more than cores: siblings join
    CHECK(PickVoiceCores({}, cores, 0).logical.empty());  // no data: Windows chooses
    const std::vector<float> idle(12, 0.05f);
    const std::vector<std::vector<int>> six{{0, 1}, {2, 3}, {4, 5}, {6, 7}, {8, 9}, {10, 11}};
    CHECK(PickVoiceCores(idle, six, 0).threads == 6);
}


static void TestSendGuard() {
    {  // ordinary chatting passes, the same line a few times too
        SendGuard g;
        for (int i = 0; i < 4; ++i) CHECK(g.Check(L"lfg Fraktale", false, false, 1000u * i).allow);
        CHECK(g.Check(L"anyone for the world boss?", false, false, 5000).allow);
        CHECK(!g.Locked() && g.Strikes() == 0);
    }
    {  // the 5th same line in 2 minutes (case, spacing, end punctuation ignored) is blocked; later it passes again
        SendGuard g;
        for (int i = 0; i < 4; ++i) CHECK(g.Check(L"WTS stuff", false, false, 10000u * i).allow);
        const SendGuard::Verdict v = g.Check(L"wts  STUFF!!", false, false, 45000);
        CHECK(!v.allow && v.reason == SendGuard::Reason::Repeat && !v.nowLocked && !g.Locked());
        CHECK(g.Check(L"wts stuff", false, false, 45000 + SendGuard::kRepeatWindowMs + 1).allow);
    }
    {  // more than 20 lines a minute: blocked; a second hit locks the session
        SendGuard g;
        for (int i = 0; i < SendGuard::kRateMax; ++i) CHECK(g.Check(L"line " + std::to_wstring(i), false, false, 100u * i).allow);
        CHECK(g.Check(L"one more", false, false, 2500).reason == SendGuard::Reason::Rate);
        const SendGuard::Verdict v = g.Check(L"and another", false, false, 2600);
        CHECK(!v.allow && v.nowLocked && g.Locked());
        CHECK(g.Check(L"hello", false, false, 999999).reason == SendGuard::Reason::Locked);
    }
    {  // an injected Enter or text written in from outside never sends
        SendGuard g;
        CHECK(g.Check(L"hi", true, false, 0).reason == SendGuard::Reason::Injected);
        CHECK(g.Check(L"hi", false, true, 10).reason == SendGuard::Reason::Foreign);
        CHECK(g.Locked() && g.Strikes() == 2);
    }
}

static void TestVoiceFixBook() {
    VoiceFixBook b;
    CHECK(!b.Learn(L"Wir gehen in den Red", L"Wir gehen in den Raid", L"DE").empty());
    CHECK(b.Apply(L"heute Red um acht", L"de") == L"heute Raid um acht");
    CHECK(b.Apply(L"the red one", L"EN-GB") == L"the red one");  // learned for German only
    // an old file without sections counts for every language; sections round-trip
    b.Parse(L"Fracktale = Fraktale\r\n[de]\r\nRed = Raid\r\n[en]\r\nwold boss = world boss\r\n");
    CHECK(b.Size() == 3);
    CHECK(b.Apply(L"Fracktale heute", L"en") == L"Fraktale heute");
    CHECK(b.Apply(L"the wold boss", L"en") == L"the world boss" && b.Apply(L"the wold boss", L"de") == L"the wold boss");
    VoiceFixBook c;
    c.Parse(b.Serialize());
    CHECK(c.Size() == 3 && c.Apply(L"Red", L"de") == L"Raid");
    const std::vector<std::wstring> w = c.Words(L"de");
    CHECK(w.size() == 2 && w[0] == L"Raid");
}

static void TestChatCode() {
    const ChatCode item = DecodeChatCode(L"[&AgGvWwEA]");
    CHECK(item.kind == ChatCodeKind::Item && item.id == 89007 && item.count == 1);
    CHECK(ChatCodeApiPath(item) == L"/v2/items?ids=89007");
    CHECK(DecodeChatCode(L"AgGvWwEA").id == 89007);
    CHECK(DecodeChatCode(L"[&BDgAAAA=]").kind == ChatCodeKind::Poi);
    CHECK(DecodeChatCode(L"[&#!x]").kind == ChatCodeKind::Unknown);
    CHECK(ChatCodeApiPath(DecodeChatCode(L"[&BDgAAAA=]")).empty());
    CHECK(UnescapeChatText(L"look \\[&AgGvWwEA\\] ok") == L"look [&AgGvWwEA] ok");
    CHECK(RarityColor("Exotic") == 0xFFA405 && RarityColor("x") == 0);
}

static void TestVoiceLearn() {
    CHECK(VoiceFixes::SoundsAlike(L"Red", L"Raid") && VoiceFixes::SoundsAlike(L"Fracktale", L"Fraktale"));
    CHECK(VoiceFixes::SoundsAlike(L"Löwinstein", L"Löwenstein"));
    CHECK(VoiceFixes::SoundsAlike(L"Löwen Stein", L"Löwenstein"));  // only the space differs
    CHECK(!VoiceFixes::SoundsAlike(L"heute", L"morgen") && !VoiceFixes::SoundsAlike(L"Raid", L"raid"));
    CHECK(!VoiceFixes::SoundsAlike(L"Raid", L"Raids") && !VoiceFixes::SoundsAlike(L"in", L"im"));
    CHECK(!VoiceFixes::SoundsAlike(L"gehen", L"gegangen") && !VoiceFixes::SoundsAlike(L"hallo", L"Raid"));

    // A correction: one word swapped for one that sounds alike.
    VoiceFixes f;
    std::vector<VoiceFixes::Pair> got = f.Learn(L"Red heute Abend um acht", L"Raid heute Abend um acht");
    CHECK(got.size() == 1 && got[0].first == L"Red" && got[0].second == L"Raid");
    CHECK(f.Apply(L"wer kommt zum red?") == L"wer kommt zum Raid?");
    CHECK(f.Apply(L"Red um acht, red!") == L"Raid um acht, Raid!");
    CHECK(f.Apply(L"Redner und reden") == L"Redner und reden");  // whole words only
    CHECK(f.Words().size() == 1 && f.Words()[0] == L"Raid");
    VoiceFixes one;
    CHECK(one.Learn(L"Red", L"Raid.").size() == 1 && one.Learn(L"hallo", L"Raid").empty() && one.Size() == 1);

    // A rewrite, added or removed words, other endings and short words teach nothing.
    VoiceFixes g;
    CHECK(g.Learn(L"ich gehe heute einkaufen", L"wir treffen uns morgen am Tor").empty());
    CHECK(g.Learn(L"heute Abend", L"heute Abend!").empty());
    CHECK(g.Learn(L"Raid heute", L"Raid heute um acht am Tor").empty());
    CHECK(g.Learn(L"ein Raid heute Abend", L"ein Raids heute Abend").empty());
    CHECK(g.Learn(L"wir sind in der Gilde", L"wir sind im der Gilde").empty());
    CHECK(g.Learn(L"wir gehen dann los", L"wir gegangen dann los").empty());
    CHECK(g.Learn(L"", L"Raid").empty() && g.Learn(L"Red", L"").empty() && g.Size() == 0);

    // Several words: split or joined as one phrase, word for word each on its own.
    got = f.Learn(L"treffen uns in Löwen Stein um acht", L"treffen uns in Löwenstein um acht");
    CHECK(got.size() == 1 && got[0].first == L"Löwen Stein" && got[0].second == L"Löwenstein");
    CHECK(f.Apply(L"bin in löwen stein") == L"bin in Löwenstein");
    CHECK(f.Apply(L"bin in Löwen, Stein") == L"bin in Löwen, Stein");  // not across punctuation
    CHECK(f.Learn(L"die Fracktale und Tekwattle heute", L"die Fraktale und Tequatl heute").size() == 2);
    CHECK(f.Apply(L"fracktale, dann tekwattle") == L"Fraktale, dann Tequatl");
    CHECK(f.Size() == 4);

    // Case and punctuation: written as learned, capitalized at a sentence start; punctuation does not count.
    VoiceFixes c;
    CHECK(c.Learn(L"Wir gehn rein, okay?", L"Wir gehen rein okay.").size() == 1);
    CHECK(c.Apply(L"gehn wir? gehn!") == L"Gehen wir? Gehen!");
    CHECK(c.Apply(L"wir GEHN") == L"wir gehen");
    // Newest wins; the reverse pair goes.
    CHECK(c.Learn(L"wir gehn rein", L"wir gähn rein").size() == 1);
    CHECK(c.Apply(L"wir gehn") == L"wir gähn" && c.Size() == 1);
    VoiceFixes r;
    r.Learn(L"zum Red heute", L"zum Raid heute");
    r.Learn(L"zum Raid heute", L"zum Red heute");
    CHECK(r.Size() == 1 && r.Apply(L"red und raid") == L"red und Red");

    // The file: comments and broken lines skipped, later lines win, round trip.
    VoiceFixes p;
    p.Parse(L"# heard = meant\r\nRed = Raid\r\n  löwen stein=Löwenstein \r\n=leer\r\nkein Gleich\r\nred = Rat\r\n");
    CHECK(p.Size() == 2 && p.Apply(L"red") == L"Rat");
    CHECK(p.Serialize() == L"löwen stein = Löwenstein\r\nred = Rat\r\n");
    VoiceFixes q;
    q.Parse(p.Serialize());
    CHECK(q.Size() == 2 && q.Serialize() == p.Serialize());
    std::wstring many;
    for (int i = 0; i < 510; ++i) many += L"hx" + std::to_wstring(i) + L" = mx" + std::to_wstring(i) + L"\n";
    q.Parse(many);
    CHECK(q.Size() == VoiceFixes::kMaxPairs && q.Entries().front().first == L"hx10" && q.Words().front() == L"mx509");
}

int main() {
    TestUtf();
    TestFreeText();
    TestCloudMt();
    TestMyWords();
    TestSureLanguage();
    TestActiveBottom();
    TestWordTriples();
    TestWordModelWeights();
    TestPhraseMemory();
    TestStampShape();
    TestInputLineTags();
    TestWordCase();
    TestLooksLikeSlip();
    TestTypoMemory();
    TestGermanContractions();
    TestGw2Abbreviations();
    TestText();
    TestChat();
    TestHotkey();
    TestJson();
    TestLangs();
    TestNameTable();
    TestGlossary();
    TestProtect();
    TestDeepL();
    TestLanguages();
    TestColors();
    TestParseLine();
    TestBuildMessages();
    TestGw2Text();
    TestLlm();
    TestDeepLBatch();
    TestMumble();
    TestMyMemory();
    TestBatchQuota();
    TestAsciiEscape();
    TestI18n();
    TestWordModel();
    TestMangledStamps();
    TestNames();
    TestLocateChat();
    TestNotChat();
    TestEmoticons();
    TestTypingSlips();
    TestGuessLanguage();
    TestLinks();
    TestStarterWords();
    TestLanguageTool();
    TestOcrTimestamps();
    TestModelList();
    TestChatTextFilter();
    TestVoiceCatalog();
    TestFindSpeech();
    TestCleanTranscript();
    TestSplitLive();
    TestWhisperSettings();
    TestToMono16k();
    TestVoiceLearn();
    TestChatCode();
    TestVoiceFixBook();
    TestSendGuard();
    TestPickVoiceCores();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
