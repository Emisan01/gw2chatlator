// Portable unit tests for src/core — no Windows needed:
//   g++ -std=c++17 -I src tests/core_tests.cpp src/core/*.cpp -o core_tests && ./core_tests
#include <cstdio>
#include <string>

#include "core/chat_line.hpp"
#include "core/chat_stream.hpp"
#include "core/chat_tabs.hpp"
#include "core/deepl_protocol.hpp"
#include "core/gw2_text.hpp"
#include "core/image.hpp"
#include "core/languages.hpp"
#include "core/llm_protocol.hpp"
#include "core/mumble.hpp"
#include "core/mymemory_protocol.hpp"
#include "core/glossary.hpp"
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
    CHECK(UnsupportedScript(L"hi مرحبا") == L"Arabisch");
    CHECK(UnsupportedScript(L"你好") == L"Chinesisch");
    CHECK(UnsupportedScript(L"Привет") == L"Kyrillisch");
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
    CHECK(tabs.size() == 2);
    CHECK(TabShows(tabs[0], Channel::Map) && TabShows(tabs[0], Channel::System) && TabShows(tabs[0], Channel::Unknown));
    CHECK(TabShows(tabs[1], Channel::Whisper) && !TabShows(tabs[1], Channel::Map));
    CHECK(SoleSendChannel(tabs[1].channels) == Channel::Whisper);
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
    CHECK(!ParseTab(L"|party", t));
    CHECK(!ParseTab(L"kein trenner", t));
    ChatTab odd;
    odd.name = L"a|b";
    odd.channels = ChannelBit(Channel::Map);
    CHECK(SerializeTab(odd) == L"a b|map");
    CHECK(TabPresets().size() == 6 && TabPresets()[1].name == L"Gruppe");
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

int main() {
    TestUtf();
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
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
