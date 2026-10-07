// Portable unit tests for src/core — no Windows needed:
//   g++ -std=c++17 -I src tests/core_tests.cpp src/core/*.cpp -o core_tests && ./core_tests
#include <chrono>
#include <cstdio>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

#include "core/chat_line.hpp"
#include "core/chat_stream.hpp"
#include "core/chat_tabs.hpp"
#include "core/cloud_mt_protocol.hpp"
#include "core/corrections.hpp"
#include "core/second_look.hpp"
#include "core/my_words.hpp"
#include "core/rapid_rec.hpp"
#include "core/deepl_protocol.hpp"
#include "core/gw2_text.hpp"
#include "core/chat_geometry.hpp"
#include "core/names.hpp"
#include "core/image.hpp"
#include "core/languages.hpp"
#include "core/llm_protocol.hpp"
#include "core/mumble.hpp"
#include "core/mymemory_protocol.hpp"
#include "core/glossary.hpp"
#include "core/gw2_install.hpp"
#include "core/housekeeping.hpp"
#include "core/i18n.hpp"
#include "core/languagetool_protocol.hpp"
#include "core/tesseract_tsv.hpp"
#include "core/word_model.hpp"
#include "core/hotkey.hpp"
#include "core/json.hpp"
#include "core/langs.hpp"
#include "core/protect.hpp"
#include "core/slang.hpp"
#include "core/text.hpp"
#include "core/translator.hpp"

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
}

static void TestDoubleScan() {
    ChatStream s;
    const ChatMessage a{Channel::Map, false, L"Tamsin", L"wer kommt mit zum Tequatl", L"", {}};
    const ChatMessage flash{Channel::Map, false, L"", L"Kiste geoeffnet: 3 Truhen", L"", {}};
    CHECK(s.Feed({a, flash}, true).empty() && s.HasPending());  // first look: nothing yet
    auto second = s.Feed({a}, true);                            // the tooltip is gone, the line stays
    CHECK(second.size() == 1 && second[0].speaker == L"Tamsin" && !s.HasPending());
    CHECK(s.Feed({a}, true).empty());                           // known now
    // Read a little differently the second time: still the same line.
    const ChatMessage b{Channel::Map, false, L"Kiro Vale", L"bin in fuenf Minuten da", L"", {}};
    const ChatMessage b2{Channel::Map, false, L"Kiro Vale", L"bin in fuenf Minuten da.", L"", {}};
    CHECK(s.Feed({a, b}, true).empty());
    CHECK(s.Feed({a, b2}, true).size() == 1);
}

static void TestStream() {
    ChatStream s;
    std::vector<ChatMessage> a = {{Channel::Map, false, L"Alice", L"hello everyone", L"", {}},
                                  {Channel::Map, false, L"Bob", L"hi alice", L"", {}}};
    CHECK(s.Feed(a).size() == 2);
    // Next frame: same lines, one OCR glitch, plus a new one.
    std::vector<ChatMessage> b = {{Channel::Map, false, L"Alice", L"hello everyone", L"", {}},
                                  {Channel::Map, false, L"Bob", L"hi alicе", L"", {}},  // Cyrillic e
                                  {Channel::Map, false, L"Carl", L"lfg tequatl", L"", {}}};
    auto fresh = s.Feed(b);
    CHECK(fresh.size() == 1 && fresh[0].speaker == L"Carl");
    CHECK(DiceSimilarity(L"hello everyone", L"hello everyone") == 1.0);
    CHECK(DiceSimilarity(L"hello everyone", L"completely different") < 0.3);
    CHECK(NormalizeForCompare(L"Hi, Alice!") == L"hialice");

    TranslationCache cache(2);
    cache.Put(L"Hello!", L"DE", L"Hallo!");
    std::wstring out;
    CHECK(cache.Get(L"hello", L"DE", out) && out == L"Hallo!");
    CHECK(!cache.Get(L"hello", L"FR", out));
    cache.Put(L"a", L"DE", L"1");
    cache.Put(L"b", L"DE", L"2");  // evicts least recently used ("hello" was used, "a" is older? no: hello used last)
    CHECK(cache.Get(L"b", L"DE", out) && out == L"2");
}

static void TestImage() {
    Image img;
    img.width = 4;
    img.height = 2;
    img.bgra.assign(4 * 2 * 4, 20);  // dark background
    for (int x = 1; x < 3; ++x) {    // purple "text" pixels in row 0
        uint8_t* p = &img.bgra[(0 * 4 + x) * 4];
        p[0] = 255; p[1] = 140; p[2] = 200; p[3] = 255;
    }
    const Rgb c = SampleTextColor(img, {RectI{0, 0, 4, 2}});
    CHECK(c == Rgb{200, 140, 255});
    const Image o = PrepareForOcr(img, 2);
    CHECK(o.width == 8 && o.height == 4 && o.bgra.size() == 8 * 4 * 4);
    CHECK(o.bgra[(0 * 8 + 0) * 4] > 200);        // background -> white
    CHECK(o.bgra[(1 * 8 + 3) * 4] < 100);        // text -> dark
    CHECK(ImageFingerprint(img) == ImageFingerprint(img));
    Image img2 = img;
    img2.bgra[0] = 250;
    CHECK(ImageFingerprint(img) != ImageFingerprint(img2));

    const std::string bmp = EncodeBmp(img2);
    CHECK(bmp.size() == 54 + static_cast<size_t>(img2.width) * img2.height * 4);
    CHECK(bmp[0] == 'B' && bmp[1] == 'M');
    CHECK(static_cast<uint8_t>(bmp[10]) == 54);
    CHECK(static_cast<uint8_t>(bmp[28]) == 32);
    CHECK(EncodeBmp(Image{}).empty());
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
    r = ParseLlmResponse(R"({"choices":[{"message":{"content":"[\"nur eins\"]"}}]})", 2);
    CHECK(!r.ok && r.formatError);  // -> the translator retries line by line
    r = ParseLlmResponse(R"({"error":{"message":"model not found"}})", 1);
    CHECK(!r.ok && !r.formatError && r.error.find(L"model not found") != std::wstring::npos);
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

static void TestTabs() {
    const auto tabs = DefaultTabs();
    CHECK(tabs.size() == 1);
    CHECK(TabShows(tabs[0], Channel::Map) && TabShows(tabs[0], Channel::System) && TabShows(tabs[0], Channel::Unknown));
    CHECK(SoleSendChannel(tabs[0].channels) == Channel::Unknown);
    // System / unknown lines do not count as a channel you can write to.
    CHECK(SoleSendChannel(ChannelBit(Channel::Guild) | ChannelBit(Channel::System)) == Channel::Guild);
    CHECK(SoleSendChannel(ChannelBit(Channel::Party) | ChannelBit(Channel::Squad)) == Channel::Unknown);
    CHECK(SoleSendChannel(ChannelBit(Channel::System)) == Channel::Unknown);

    ChatTab t;
    CHECK(ParseTab(L" Gruppe | party, SQUAD ,bogus", t));
    CHECK(t.name == L"Gruppe" && t.channels == (ChannelBit(Channel::Party) | ChannelBit(Channel::Squad)));
    CHECK(SerializeTab(t) == L"Gruppe|party,squad");
    ChatTab back;
    CHECK(ParseTab(SerializeTab(tabs[0]), back) && back.channels == AllChannels() && back.name == L"Chat");
    CHECK(!ParseTab(L"Leer|bogus", t));
    CHECK(SerializeChannels(DefaultAutoTranslate()) == L"say,map,party,squad,team,guild,whisper,other");
    CHECK(ParseChannels(L" Whisper ,map,bogus") == (ChannelBit(Channel::Whisper) | ChannelBit(Channel::Map)));
    CHECK(ParseChannels(SerializeChannels(DefaultAutoTranslate())) == DefaultAutoTranslate());
    ChatTab rook;
    rook.name = L"Rook";
    rook.channels = ChannelBit(Channel::Whisper);
    rook.person = L"Rook Vale";
    CHECK(SerializeTab(rook) == L"Rook|whisper|@Rook Vale");
    CHECK(ParseTab(SerializeTab(rook), back) && back.person == L"Rook Vale" && back.channels == rook.channels);
    CHECK(!ParseTab(L"|party", t));
    CHECK(!ParseTab(L"kein trenner", t));
    ChatTab odd;
    odd.name = L"a|b";
    odd.channels = ChannelBit(Channel::Map);
    CHECK(SerializeTab(odd) == L"a b|map");
    CHECK(TabPresets().size() == 6 && TabPresets()[1].name == L"Party");
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
static Image FakeChat(int w, int h, int firstTop, int pitch, int textH, int lines) {
    Image img;
    img.width = w;
    img.height = h;
    img.bgra.assign(static_cast<size_t>(w) * h * 4, 0);
    for (size_t i = 0; i < img.bgra.size(); i += 4) {
        img.bgra[i] = 40;
        img.bgra[i + 1] = 34;
        img.bgra[i + 2] = 30;
        img.bgra[i + 3] = 255;
    }
    auto ink = [&](int x, int y) {
        if (x < 0 || y < 0 || x >= w || y >= h) return;
        uint8_t* p = &img.bgra[(static_cast<size_t>(y) * w + x) * 4];
        p[0] = 225;
        p[1] = 230;
        p[2] = 235;
    };
    for (int l = 0; l < lines; ++l)
        for (int y = firstTop + l * pitch; y < firstTop + l * pitch + textH; ++y)
            for (int x = 20; x < w - 20; ++x)
                if ((x / 3) % 2 == 0) ink(x, y);
    return img;
}

static void TestChatGeometry() {
    // 4K-like: lines 20 px apart, ink 14 px high.
    Image img = FakeChat(400, 260, 30, 20, 14, 10);
    LineGrid g = FindLineGrid(img, {0, 0, 400, 260});
    CHECK(g.rows.size() == 10 && g.pitch == 20 && g.textHeight == 14);
    CHECK(OcrScaleFor(g) == 2);
    LineGrid small;
    small.pitch = 10;
    small.textHeight = 7;
    CHECK(OcrScaleFor(small) == 3);
    small.pitch = 31;
    CHECK(OcrScaleFor(small) == 1);

    // Two lines whose descenders touch read as one run: split again.
    Image joined = FakeChat(400, 200, 20, 20, 14, 6);
    for (int x = 20; x < 380; ++x)
        for (int y = 34; y < 40; ++y) {  // fill the gap between line 1 and 2
            uint8_t* p = &joined.bgra[(static_cast<size_t>(y) * 400 + x) * 4];
            if ((x / 3) % 2 == 0) p[0] = p[1] = p[2] = 230;
        }
    CHECK(FindLineGrid(joined, {0, 0, 400, 200}).rows.size() == 6);

    // Dark text on a white page (a website in the free screen area): the same grid.
    Image page = FakeChat(400, 260, 30, 20, 14, 10);
    for (size_t i = 0; i + 3 < page.bgra.size(); i += 4) {
        page.bgra[i] = static_cast<uint8_t>(255 - page.bgra[i]);
        page.bgra[i + 1] = static_cast<uint8_t>(255 - page.bgra[i + 1]);
        page.bgra[i + 2] = static_cast<uint8_t>(255 - page.bgra[i + 2]);
    }
    const LineGrid pg = FindLineGrid(page, {0, 0, 400, 260});
    CHECK(pg.rows.size() == 10 && pg.pitch == 20);

    // Snapping: the frame cuts the first line mostly away (left out) and the
    // last line only a little (taken in).
    SnapResult s = SnapChatArea(img, {0, 40, 400, 165});  // lines at 30,50,...,210
    CHECK(s.grid.rows.size() == 8);
    CHECK(s.area.y > 30 && s.area.y <= 50 && s.area.y + s.area.h >= 204);
    CHECK(RateArea(s) == AreaQuality::Good);

    // Tab bar above and input line below, set apart by wider gaps: dropped.
    Image panel = FakeChat(400, 300, 60, 20, 14, 8);  // lines 60..200
    for (int x = 20; x < 380; ++x)
        for (int y = 20; y < 34; ++y)
            if ((x / 3) % 2 == 0) panel.bgra[(static_cast<size_t>(y) * 400 + x) * 4 + 1] = 230;  // tab bar at 20
    for (int x = 20; x < 380; ++x)
        for (int y = 250; y < 264; ++y)
            if ((x / 3) % 2 == 0) panel.bgra[(static_cast<size_t>(y) * 400 + x) * 4 + 1] = 230;  // input line at 250
    s = SnapChatArea(panel, {0, 10, 400, 270});
    CHECK(s.grid.rows.size() == 8);
    CHECK(s.area.y >= 34 && s.area.y + s.area.h <= 250);

    // Scroll bar: a narrow bright column at the left edge, apart from the text.
    Image bar = FakeChat(400, 260, 30, 20, 14, 10);
    for (int y = 25; y < 235; ++y)
        for (int x = 4; x < 9; ++x) bar.bgra[(static_cast<size_t>(y) * 400 + x) * 4 + 2] = 230;
    s = SnapChatArea(bar, {0, 25, 400, 210});
    std::printf("bar snap: x=%d y=%d w=%d h=%d rows=%zu pitch=%d th=%d\n", s.area.x, s.area.y, s.area.w, s.area.h,
                s.grid.rows.size(), s.grid.pitch, s.grid.textHeight);
    CHECK(s.area.x > 9 && s.area.x < 20);

    CHECK(RateArea(SnapChatArea(FakeChat(200, 100, 0, 20, 14, 0), {0, 0, 200, 100})) == AreaQuality::None);
    // Words of three chat lines reported as one line come apart again, in order.
    LineGrid three;
    three.rows = {{10, 14}, {30, 14}, {50, 14}};
    three.pitch = 20;
    three.textHeight = 14;
    auto groups = GroupWordsByRows({{L"c2", {90, 31, 20, 12}}, {L"a1", {5, 11, 20, 12}}, {L"b1", {40, 52, 20, 12}},
                                    {L"a2", {40, 10, 20, 13}}, {L"b0", {5, 50, 30, 14}}, {L"far", {5, 200, 20, 12}}},
                                   three);
    CHECK(groups.size() == 3);
    if (groups.size() == 3) {
        CHECK(groups[0].size() == 2 && groups[0][0].text == L"a1" && groups[0][1].text == L"a2");
        CHECK(groups[1].size() == 1 && groups[1][0].text == L"c2");
        CHECK(groups[2].size() == 2 && groups[2][0].text == L"b0" && groups[2][1].text == L"b1");
    }

    const Image up = UpscaleForOcr(Crop(img, {0, 0, 50, 40}), 3);
    CHECK(up.width == 150 && up.height == 120 && !up.Empty());
}

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

static void TestTesseract() {
    const std::string tsv =
        "level\tpage_num\tblock_num\tpar_num\tline_num\tword_num\tleft\ttop\twidth\theight\tconf\ttext\n"
        "1\t1\t0\t0\t0\t0\t0\t0\t600\t200\t-1\t\n"
        "4\t1\t1\t1\t1\t0\t10\t10\t300\t20\t-1\t\n"
        "5\t1\t1\t1\t1\t1\t10\t10\t60\t20\t95.5\t[19:17]\n"
        "5\t1\t1\t1\t1\t2\t80\t10\t60\t20\t91.0\tEmi:\n"
        "5\t1\t1\t1\t1\t3\t150\t10\t60\t20\t12.0\tHallo\n"
        "5\t1\t1\t1\t2\t1\t10\t40\t60\t20\t5.0\t~~\n"
        "5\t1\t2\t1\t1\t1\t10\t70\t60\t20\t88.0\tZweite\n"
        "5\t1\t2\t1\t1\t2\t80\t70\t60\t20\t87.0\tZeile\n";
    const auto lines = ParseTesseractTsv(tsv, 30);
    CHECK(lines.size() == 2);
    if (lines.size() == 2) {
        CHECK(lines[0].text == L"[19:17] Emi: Hallo");
        CHECK(lines[0].words.size() == 3 && lines[0].words[1].rect.x == 80);
        CHECK(lines[1].text == L"Zweite Zeile");
    }
    CHECK(TesseractModel(L"DE") == "deu" && TesseractModel(L"EN-GB") == "eng" && TesseractModel(L"ZH-HANS") == "chi_sim");
    CHECK(TesseractModel(L"ZH-HANT") == "chi_tra" && TesseractModel(L"PT-BR") == "por" && TesseractModel(L"XX").empty());
    const std::vector<std::string> installed = {"eng", "deu", "osd", "chi_sim", "fra"};
    CHECK(ChooseTesseractLangs("", installed, false) == "eng+deu+fra");
    CHECK(ChooseTesseractLangs("", installed, true) == "eng+deu+fra+chi_sim");
    CHECK(ChooseTesseractLangs("deu+spa", installed, false) == "deu");
    CHECK(ChooseTesseractLangs("", {"osd", "jpn"}, false) == "jpn");
    Image img;
    img.width = 2;
    img.height = 1;
    img.bgra = {255, 255, 255, 255, 0, 0, 0, 255};
    const std::string pgm = EncodePgm(img);
    CHECK(pgm.rfind("P5\n2 1\n255\n", 0) == 0 && pgm.size() == 11 + 2);
    CHECK(static_cast<unsigned char>(pgm[11]) >= 254 && pgm[12] == 0);
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

static std::vector<OcrLine> LoadCapture(const char* path) {
    std::vector<OcrLine> out;
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::string fallback = std::string("../") + path;
        f.open(fallback, std::ios::binary);
        if (!f.is_open()) {
            f.open(std::string("../../") + path, std::ios::binary);
        }
    }
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        // "f6f93a | 95 | 18 | text"
        std::vector<std::string> parts;
        size_t s = 0;
        for (int i = 0; i < 3; ++i) {
            size_t bar = line.find(" | ", s);
            if (bar == std::string::npos) break;
            parts.push_back(line.substr(s, bar - s));
            s = bar + 3;
        }
        if (parts.size() != 3) continue;
        OcrLine l;
        RgbFromHex(FromUtf8(parts[0]), l.color);
        l.top = std::atoi(parts[1].c_str());
        l.height = std::atoi(parts[2].c_str());
        l.text = FromUtf8(line.substr(s));
        out.push_back(l);
    }
    return out;
}

static void TestRealCapture() {
    // A real capture of the German client read by Windows OCR (lots of errors).
    const auto lines = LoadCapture("tests/data/real_capture_win_ocr.txt");
    CHECK(lines.size() == 17);
    const auto msgs = BuildMessages(lines, DefaultChannelColors());
    bool tabBar = false, inputLine = false, zapp = false, system = false;
    for (const ChatMessage& m : msgs) {
        if (m.text.find(L"cbt") != std::wstring::npos || m.text.find(L"Febt") != std::wstring::npos) tabBar = true;
        if (m.text.find(L"CK L N") != std::wstring::npos) inputLine = true;
        if (m.speaker == L"Pell The Unnamed" && m.text == L"Thanks!") zapp = true;
        if (m.channel == Channel::System && m.text.find(L"Krypts") != std::wstring::npos &&
            m.text.find(L"Konvergenz") != std::wstring::npos)
            system = true;  // the wrapped yellow notice is one message
    }
    CHECK(!tabBar);
    CHECK(zapp);
    CHECK(system);
    (void)inputLine;
    for (const ChatMessage& m : msgs) CHECK(!m.text.empty());
}

static void TestGw2Install() {
    auto libs = ParseSteamLibraryPaths("\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"D:\\\\SteamLibrary\"\n\t}\n}\n");
    CHECK(libs.size() == 1 && libs[0] == L"D:\\SteamLibrary");
    CHECK(GameDirFromRegistryValue(L"\"C:\\Games\\GW2\\Gw2-64.exe\" -maploadinfo") == L"C:\\Games\\GW2");
    CHECK(InstallDirFor(L"C:\\GW2") == L"C:\\GW2\\addons\\GW2ChatTranslator");
    std::set<std::wstring> files{L"d3d11.dll", L"addons\\Nexus", L"arcdps_unofficial_extras.dll"};
    auto env = DetectAddons([&](const std::wstring& r) { return files.count(r) > 0; },
                            [](const std::wstring&) { return std::wstring(L"Nexus"); });
    CHECK(env.nexus && env.unofficialExtras && env.proxyDll && !env.arcdps);
    CHECK(HasSwitch(L"\"x.exe\" --wait-for-gw2", L"--wait-for-gw2") && !HasSwitch(L"--wait-for-gw2x", L"--wait-for-gw2"));
}

static void TestHousekeeping() {
    std::vector<FileEntry> f = {{L"capture_00.txt", 1000, 100}, {L"capture_00_raw.bmp", 5000, 100},
                                {L"capture_01.txt", 1000, 200}, {L"keep.ini", 50, 1}, {L"x.tmp", 10, 1}};
    auto del = SelectForCleanup(f, {2, 0, 0}, 300, IsCaptureFile);
    CHECK(del.size() == 1 && del[0] == L"capture_00.txt");
    del = SelectForCleanup(f, {0, 0, 150}, 300, IsStaleTempFile);
    CHECK(del.size() == 1 && del[0] == L"x.tmp");
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

static void TestCorrections() {
    CorrectionMemory c;
    // A corrected line comes back exactly, case and spacing of the source ignored.
    c.Add(L"wer kommt mit zum Weltboss?", L"EN-GB", L"who comes with to the world boss?", L"Who wants to join the world boss?");
    std::wstring out;
    CHECK(c.Lookup(L"Wer  kommt mit zum weltboss?", L"EN-US", &out) && out == L"Who wants to join the world boss?");
    CHECK(!c.Lookup(L"wer kommt mit zum Weltboss?", L"DE", &out));
    // Only a few words changed: that phrase is learned and applied to new translations.
    CHECK(c.Phrases() == 1);  // "who comes with to" -> "Who wants to join" (the end stayed the same)
    c.Add(L"Weltboss in 5 Minuten", L"EN", L"World Boss in 5 minutes", L"world boss in 5 minutes");
    CHECK(c.Phrases() == 1);  // only the case changed: no phrase
    c.Add(L"brauche Hilfe bei der Fraktale", L"EN", L"need help with the fractal", L"need help with the fractals");
    CHECK(c.Phrases() == 2);
    CHECK(c.Apply(L"The fractal starts now.", L"EN") == L"The fractals starts now.");
    CHECK(c.Apply(L"fractalist", L"EN") == L"fractalist");    // whole words only
    CHECK(c.Apply(L"The fractal", L"DE") == L"The fractal");  // other language untouched
    std::wstring from, to;
    CHECK(CorrectionMemory::ChangedPhrase(L"Let's go to Lion's Arc now!", L"Let's go to Lion's Arch now!", &from, &to));
    CHECK(from == L"Arc" && to == L"Arch");
    CHECK(!CorrectionMemory::ChangedPhrase(L"a b c d e f", L"u v w x y z", &from, &to));  // everything changed
    // Saved and loaded again, including tabs and backslashes.
    c.Add(L"tab\there \\ x", L"EN", L"a", L"b\tc");
    CorrectionMemory d;
    d.Parse(c.Serialize());
    CHECK(d.Lines() == c.Lines() && d.Phrases() == c.Phrases());
    CHECK(d.Lookup(L"tab\there \\ x", L"EN", &out) && out == L"b\tc");
    d.Clear();
    CHECK(d.Lines() == 0 && d.Phrases() == 0);
}

static void TestSecondLook() {
    size_t at = 9;
    CHECK(WordCore(L"(main),", &at) == L"main" && at == 1);
    CHECK(WordCore(L"*ain") == L"ain");
    // Worth checking: normal words; not numbers, abbreviations, links, other scripts, very short words.
    CHECK(WorthSecondLook(L"uvjrde") && WorthSecondLook(L"Weltboss") && WorthSecondLook(L"Lion's"));
    CHECK(!WorthSecondLook(L"LFG") && !WorthSecondLook(L"WvW") && !WorthSecondLook(L"lvl80"));
    CHECK(!WorthSecondLook(L"ok") && !WorthSecondLook(L"gw2.com") && !WorthSecondLook(L"привет"));
    // The second reading replaces only when close: one or two characters differ.
    CHECK(PlausibleRereading(L"rnain", L"main"));
    CHECK(PlausibleRereading(L"ain", L"main"));
    CHECK(PlausibleRereading(L"uvjrde", L"würde"));
    CHECK(!PlausibleRereading(L"main", L"main"));
    CHECK(!PlausibleRereading(L"Main", L"main"));            // only the case: nothing to fix
    CHECK(!PlausibleRereading(L"finds", L"Freunde"));        // another word altogether
    CHECK(!PlausibleRereading(L"tbe", L"thy"));              // short word: one character only
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

static void TestRapidRec() {
    // CTC: best class per step, repeats merged, blanks (0) dropped; the last class is a space.
    const std::vector<std::wstring> dict = {L"a", L"b", L"c"};
    // steps: a a _ b space c c  (classes: 0 blank, 1 a, 2 b, 3 c, 4 space)
    const int best[] = {1, 1, 0, 2, 4, 3, 3};
    std::vector<float> probs(7 * 5, 0.01f);
    for (int t = 0; t < 7; ++t) probs[static_cast<size_t>(t) * 5 + best[t]] = 0.9f;
    const RecResult r = CtcDecode(probs.data(), 7, 5, dict);
    CHECK(r.text == L"ab c" && r.charStep.size() == 4 && r.charStep[0] == 0 && r.charStep[3] == 5);
    CHECK(r.confidence > 0.89f && r.confidence < 0.91f);
    RecResult rr = r;
    rr.inputWidth = 70;  // 10 model pixels per step
    const std::vector<RecWord> w = RecWords(rr, rr.inputWidth);
    CHECK(w.size() == 2 && w[0].text == L"ab" && w[1].text == L"c" && w[1].x >= 45 && w[1].x <= 55);
    // Picture preparation: height 48, at least 320 wide, normalised to -1..1, invert flips.
    Image img;
    img.width = 20;
    img.height = 10;
    img.bgra.assign(20 * 10 * 4, 255);
    const RecInput in = PrepareRecInput(img, false);
    CHECK(in.width == 320 && in.data.size() == static_cast<size_t>(3) * 48 * 320);
    CHECK(in.data[0] > 0.99f && in.data[200] == 0.0f);  // white pixel; padding at the right stays 0
    CHECK(PrepareRecInput(img, true).data[0] < -0.99f);
    CHECK(ParseRecDictionary("a\r\nb\nc").size() == 3);
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

int main() {
    TestUtf();
    TestFreeText();
    TestCloudMt();
    TestCorrections();
    TestSecondLook();
    TestMyWords();
    TestSureLanguage();
    TestRapidRec();
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
    TestStream();
    TestImage();
    TestGw2Text();
    TestLlm();
    TestDeepLBatch();
    TestMumble();
    TestMyMemory();
    TestBatchQuota();
    TestTabs();
    TestAsciiEscape();
    TestI18n();
    TestWordModel();
    TestChatGeometry();
    TestMangledStamps();
    TestNames();
    TestLocateChat();
    TestNotChat();
    TestEmoticons();
    TestDoubleScan();
    TestTypingSlips();
    TestGuessLanguage();
    TestLinks();
    TestStarterWords();
    TestTesseract();
    TestLanguageTool();
    TestOcrTimestamps();
    TestRealCapture();
    TestGw2Install();
    TestHousekeeping();
    TestModelList();
    TestChatTextFilter();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
