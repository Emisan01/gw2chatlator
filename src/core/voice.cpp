// voice.cpp — portable logic of the voice input (see voice.hpp). No windows.h.
#include "core/voice.hpp"

#include <algorithm>
#include <cmath>

#include "core/i18n.hpp"
#include "core/json.hpp"
#include "core/text.hpp"

namespace gct {

namespace {

constexpr size_t kFrame = kVoiceRate / 50;  // 20 ms

float RmsDb(const float* p, size_t n) {
    double sum = 0;
    for (size_t i = 0; i < n; ++i) sum += static_cast<double>(p[i]) * p[i];
    const double rms = n ? std::sqrt(sum / static_cast<double>(n)) : 0.0;
    return rms > 1e-6 ? static_cast<float>(20.0 * std::log10(rms)) : -120.f;  // NaN lands here too
}

bool IsSpaceChar(wchar_t c) {
    return c == L' ' || (c >= 9 && c <= 13) || c == 0xA0 || c == 0x3000 || (c >= 0x2000 && c <= 0x200B) ||
           c == 0x202F || c == 0x205F || c == 0xFEFF;
}

// CaseFold plus Greek and Cyrillic (whisper's Russian hallucinations, all-caps markers).
wchar_t FoldChar(wchar_t c) {
    const uint32_t u = static_cast<uint32_t>(c);
    if (u >= 0x410 && u <= 0x42F) return static_cast<wchar_t>(u + 0x20);
    if (u >= 0x400 && u <= 0x40F) return static_cast<wchar_t>(u + 0x50);
    if (u >= 0x391 && u <= 0x3A9 && u != 0x3A2) return static_cast<wchar_t>(u + 0x20);
    return CaseFoldChar(c);
}

// Folded letters and digits only: "Thanks for watching!" -> "thanksforwatching".
std::wstring MatchKey(const std::wstring& s) {
    std::wstring k;
    for (wchar_t c : s)
        if (IsWordChar(c) && c != L'\'' && c != 0x2019) k += FoldChar(c);
    return k;
}

std::vector<std::wstring> SplitWords(const std::wstring& s) {
    std::vector<std::wstring> words;
    std::wstring cur;
    for (wchar_t c : s) {
        if (IsSpaceChar(c)) {
            if (!cur.empty()) words.push_back(std::move(cur));
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) words.push_back(std::move(cur));
    return words;
}

std::wstring JoinWords(const std::vector<std::wstring>& w, size_t from, size_t to) {
    std::wstring out;
    for (size_t i = from; i < to; ++i) {
        if (i > from) out += L' ';
        out += w[i];
    }
    return out;
}

// ---- catalog validation ----

bool SafeFileName(const std::string& f) {
    if (f.empty() || f.find("..") != std::string::npos) return false;
    for (char c : f)
        if (c == '/' || c == '\\' || c == ':' || static_cast<unsigned char>(c) < 0x20) return false;
    return true;
}

bool IsHttpsUrl(const std::string& u) {
    if (u.size() <= 8) return false;
    std::string scheme = u.substr(0, 8);
    for (char& c : scheme) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
    return scheme == "https://";
}

// Lower-cases in place; true for exactly 64 hex digits.
bool NormalizeSha256(std::string* s) {
    if (s->size() != 64) return false;
    for (char& c : *s) {
        if (c >= 'A' && c <= 'F') c = static_cast<char>(c + 32);
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    }
    return true;
}

double NumberOr(const JsonValue& o, const char* key, double def) {
    const JsonValue* v = o.Get(key);
    return (v && v->type == JsonValue::Type::Number) ? v->n : def;
}

// ---- transcript cleaning ----

constexpr wchar_t kCut = 1;  // where a marker was removed: no space is left before punctuation there

// Words whisper writes inside "( )" on non-speech ("(upbeat music)", "(Musik spielt)", "(lacht)"). A parenthesis is
// dropped only when every word of it is in here, so "(das ist Musik)" stays.
const wchar_t* const kSoundWords[] = {
    L"music", L"musik", L"musique", L"musica", L"música", L"applause", L"applaus", L"applaudissements", L"aplausos",
    L"applausi", L"laughter", L"laughs", L"laughing", L"laugh", L"lachen", L"lacht", L"gelächter", L"rires", L"rire",
    L"risas", L"risate", L"sighs", L"seufzt", L"coughs", L"coughing", L"husten", L"hustet", L"silence", L"stille",
    L"silencio", L"noise", L"noises", L"geräusch", L"geräusche", L"rauschen", L"bruit", L"ruido", L"static",
    L"inaudible", L"unintelligible", L"unverständlich", L"clapping", L"klatschen", L"cheering", L"breathing",
    L"atmet", L"wind", L"beep", L"background", L"upbeat", L"gentle", L"soft", L"dramatic", L"playing", L"plays",
    L"piano", L"spielt", L"leise", L"sanfte", L"ruhige", L"音楽", L"拍手", L"笑",
};

bool IsSoundWord(const std::wstring& key) {
    for (const wchar_t* w : kSoundWords)
        if (key == w) return true;
    return false;
}

// Content of "( )" that is a non-speech marker: no letters at all ("(...)"), or 1-3 words that are all sound words,
// or all capitals with 4+ letters ("(MUSIC)").
bool IsSoundMarker(const std::wstring& content) {
    const std::vector<std::wstring> words = SplitWords(content);
    size_t real = 0;
    bool allSound = true;
    for (const auto& w : words) {
        const std::wstring k = MatchKey(w);
        if (k.empty()) continue;
        ++real;
        if (!IsSoundWord(k)) allSound = false;
    }
    if (real == 0) return true;
    if (real > 3) return false;
    if (allSound) return true;
    int upper = 0;
    for (wchar_t c : content) {
        if (FoldChar(c) != c) ++upper;
        else if (IsWordChar(c) && c != L'\'') return false;  // lower case, digit or a script without case
    }
    return upper >= 4;
}

bool IsClosingPunct(wchar_t c) {
    return c == L'.' || c == L',' || c == L'!' || c == L'?' || c == L';' || c == L':' || c == 0x2026 ||
           c == 0x3001 || c == 0x3002 || c == 0xFF0C || c == 0xFF01 || c == 0xFF1F || c == 0x060C || c == 0x061F;
}

// Replaces every open..close pair whose content `drop` accepts with kCut.
template <typename Pred>
void CutPairs(std::wstring& t, wchar_t open, wchar_t close, size_t maxLen, Pred drop) {
    size_t i = 0;
    while ((i = t.find(open, i)) != std::wstring::npos) {
        const size_t j = t.find(close, i + 1);
        if (j == std::wstring::npos) break;
        const size_t nested = t.find(open, i + 1);
        if (nested != std::wstring::npos && nested < j && open != close) {
            i = nested;  // try the inner one
            continue;
        }
        const std::wstring content = t.substr(i + 1, j - i - 1);
        if (j - i - 1 <= maxLen && drop(content)) {
            t.replace(i, j - i + 1, 1, kCut);
            ++i;
        } else {
            i = open == close ? j : i + 1;
        }
    }
}

// Whitespace runs -> one space; a run with a cut in it before punctuation or at either end disappears.
std::wstring Collapse(const std::wstring& t) {
    std::wstring out;
    bool inRun = false, runCut = false;
    for (wchar_t c : t) {
        if (c == kCut || IsSpaceChar(c)) {
            inRun = true;
            runCut = runCut || c == kCut;
            continue;
        }
        if (inRun && !out.empty() && !(runCut && IsClosingPunct(c))) out += L' ';
        inRun = runCut = false;
        out += c;
    }
    return out;
}

// What whisper writes on silence or noise (subtitle credits of its training videos). Compared by MatchKey.
const wchar_t* const kHallucinations[] = {
    L"vielendankfürszuschauen",          // "Vielen Dank fürs Zuschauen"
    L"dankefürszuschauen",
    L"vielendankfürihreaufmerksamkeit",  // only when it is everything
    L"thankyouforwatching",
    L"thanksforwatching",
    L"you",                              // the classic single word on silence
    L"продолжениеследует",               // "Продолжение следует..." ("to be continued")
    L"ご視聴ありがとうございました",       // "thank you for watching"
};

bool IsHallucination(const std::wstring& key) {
    if (key.empty()) return true;  // ".", "...", "-": nothing to insert
    for (const wchar_t* h : kHallucinations)
        if (key == h) return true;
    // "Untertitel der Amara.org-Gemeinschaft", "Untertitel im Auftrag des ZDF für funk, 2017", "Untertitelung des ZDF".
    if (key.rfind(L"untertitel", 0) == 0 &&
        (key.find(L"zdf") != std::wstring::npos || key.find(L"amara") != std::wstring::npos))
        return true;
    // "Subtitles by the Amara.org community", "Субтитры сделал ..." (credits of a subtitler).
    return key.rfind(L"subtitlesby", 0) == 0 || key.rfind(L"субтитры", 0) == 0;
}

}  // namespace

// ---- model catalog ----

int VoiceModelInfo::QualityFor(const std::string& lang) const {
    if (englishOnly && lang != "en") return 0;
    if (langs.empty()) return 2;
    int lowest = 3;
    for (const auto& q : langs) {
        if (q.lang == lang) return q.quality;
        lowest = std::min(lowest, q.quality);
    }
    // The list names the big languages; one it does not name had less training data: the model's weakest.
    return lowest;
}

const VoiceModelInfo* VoiceCatalog::Find(const std::string& id) const {
    for (const auto& m : models)
        if (m.id == id) return &m;
    return nullptr;
}

const VoiceModelInfo* VoiceCatalog::Recommended() const {
    for (const auto& m : models)
        if (m.recommended) return &m;
    return models.empty() ? nullptr : &models.front();
}

std::wstring VoiceQualityWord(int q) {
    return q >= 3 ? Tr(L"very well") : q == 2 ? Tr(L"well") : q == 1 ? Tr(L"weakly") : Tr(L"hardly");
}

VoiceRecommendation RecommendVoiceModel(const VoiceCatalog& catalog, const VoiceModelInfo* current, const std::string& whisperLang) {
    VoiceRecommendation rec;
    if (whisperLang.empty()) {
        rec.supportedByWhisper = false;
        return rec;
    }
    rec.supportedByWhisper = true;
    if (!current) current = catalog.Recommended();
    if (current) rec.quality = current->QualityFor(whisperLang);
    if (rec.quality < 2) {
        const VoiceModelInfo* better = nullptr;
        for (const VoiceModelInfo& o : catalog.models) {
            if (current && o.bytes <= current->bytes) continue;
            if (o.QualityFor(whisperLang) >= 2 && (!better || o.bytes < better->bytes)) {
                better = &o;
            }
        }
        rec.recommended = better;
    }
    return rec;
}

// whisper.cpp v1.9.5 multilingual models (sizes and SHA-256 checked 2026-10-12). ramMb: measured-order estimates.
// No tiny: with the short encoder context it loops and loses half sentences (voice_bench, 2026-10-12), and with the
// full context it is slower than base with more errors.
// "langs": Whisper paper (Radford et al. 2022, arXiv:2212.04356), Table 13, FLEURS error rates of tiny / base / small /
// medium and large-v2 for the turbo model (large-v3 and turbo are at least as good; q5 quantization costs little):
// < 10 % -> 3, < 20 % -> 2, < 35 % -> 1, else 0. Arabic needs at least small (tiny 63 %, base 49 %, small 31 %).
const char* DefaultVoiceCatalogJson() {
    return R"json({
  "date": "2026-10-12",
  "models": [
    {"id": "base-q5_1", "name": "Whisper base (q5_1)", "provider": "whisper", "file": "ggml-base-q5_1.bin",
     "url": "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-base-q5_1.bin",
     "sha256": "422f1ae452ade6f30a004d7e5c6a43195e4433bc370bf23fac9cc591f01a8898", "bytes": 59707625,
     "ramMb": 200, "streaming": false, "englishOnly": false, "recommended": false, "licence": "MIT",
     "langs": {"en": 3, "de": 2, "fr": 1, "es": 3, "it": 2, "pt": 2, "nl": 1, "pl": 1, "ru": 1, "tr": 1, "ar": 0,
               "zh": 1, "ja": 1, "ko": 1}},
    {"id": "small-q5_1", "name": "Whisper small (q5_1)", "provider": "whisper", "file": "ggml-small-q5_1.bin",
     "url": "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-small-q5_1.bin",
     "sha256": "ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb", "bytes": 190085487,
     "ramMb": 500, "streaming": false, "englishOnly": false, "recommended": true, "licence": "MIT",
     "langs": {"en": 3, "de": 2, "fr": 2, "es": 3, "it": 3, "pt": 3, "nl": 2, "pl": 2, "ru": 2, "tr": 2, "ar": 1,
               "zh": 1, "ja": 2, "ko": 2}},
    {"id": "medium-q5_0", "name": "Whisper medium (q5_0)", "provider": "whisper", "file": "ggml-medium-q5_0.bin",
     "url": "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-medium-q5_0.bin",
     "sha256": "19fea4b380c3a618ec4723c3eef2eb785ffba0d0538cf43f8f235e7b3b34220f", "bytes": 539212467,
     "ramMb": 1100, "streaming": false, "englishOnly": false, "recommended": false, "licence": "MIT",
     "langs": {"en": 3, "de": 3, "fr": 3, "es": 3, "it": 3, "pt": 3, "nl": 3, "pl": 3, "ru": 3, "tr": 2, "ar": 1,
               "zh": 2, "ja": 3, "ko": 2}},
    {"id": "large-v3-turbo-q5_0", "name": "Whisper large-v3 turbo (q5_0)", "provider": "whisper",
     "file": "ggml-large-v3-turbo-q5_0.bin",
     "url": "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-large-v3-turbo-q5_0.bin",
     "sha256": "394221709cd5ad1f40c46e6031ca61bce88931e6e088c188294c6d5a55ffa7e2", "bytes": 574041195,
     "ramMb": 1100, "streaming": false, "englishOnly": false, "recommended": false, "licence": "MIT",
     "langs": {"en": 3, "de": 3, "fr": 3, "es": 3, "it": 3, "pt": 3, "nl": 3, "pl": 3, "ru": 3, "tr": 3, "ar": 2,
               "zh": 2, "ja": 3, "ko": 2}}
  ]
})json";
}

bool ParseVoiceCatalog(const std::string& json, VoiceCatalog* out) {
    if (!out) return false;
    *out = VoiceCatalog{};
    JsonValue root;
    if (!ParseJson(json, root) || !root.IsObject()) return false;
    out->date = root.GetString("date");
    const JsonValue* models = root.Get("models");
    if (!models || !models->IsArray()) return false;
    for (const JsonValue& m : models->arr) {
        if (!m.IsObject()) continue;
        VoiceModelInfo info;
        info.file = m.GetString("file");
        info.url = m.GetString("url");
        info.sha256 = m.GetString("sha256");
        // Sizes come as doubles: exact up to 2^53, far beyond any model.
        const double bytes = NumberOr(m, "bytes", 0);
        if (!SafeFileName(info.file) || !IsHttpsUrl(info.url) || !NormalizeSha256(&info.sha256) || !(bytes >= 1) ||
            bytes > 9007199254740992.0 || bytes != std::floor(bytes))
            continue;
        info.bytes = static_cast<uint64_t>(bytes);
        info.id = m.GetString("id");
        if (info.id.empty()) info.id = info.file;
        if (out->Find(info.id)) continue;  // ids stay unique
        info.name = m.GetString("name");
        if (info.name.empty()) info.name = info.id;
        info.provider = m.GetString("provider");
        if (info.provider.empty()) info.provider = "whisper";
        info.ramMb = static_cast<int>(std::clamp(NumberOr(m, "ramMb", 0), 0.0, 1e6));
        info.streaming = m.GetBool("streaming");
        info.englishOnly = m.GetBool("englishOnly");
        info.recommended = m.GetBool("recommended");
        info.licence = m.GetString("licence");
        if (const JsonValue* langs = m.Get("langs"); langs && langs->IsObject())
            for (const auto& kv : langs->obj)
                if (!kv.first.empty() && kv.second.type == JsonValue::Type::Number)
                    info.langs.push_back({kv.first, static_cast<int>(std::lround(std::clamp(kv.second.n, 0.0, 3.0)))});
        out->models.push_back(std::move(info));
    }
    return !out->models.empty();
}

// ---- speech in a recording ----

SpeechSpan FindSpeech(const float* pcm, size_t samples, const SpeechOptions& o) {
    SpeechSpan s;
    if (!pcm || samples == 0) return s;
    const size_t frames = (samples + kFrame - 1) / kFrame;
    std::vector<float> level(frames);
    for (size_t f = 0; f < frames; ++f) level[f] = RmsDb(pcm + f * kFrame, std::min(kFrame, samples - f * kFrame));

    // Noise floor = 10th percentile of the frame levels: the quiet moments before, between and after the words.
    std::vector<float> sorted(level);
    const auto nth = sorted.begin() + static_cast<std::ptrdiff_t>(sorted.size() / 10);
    std::nth_element(sorted.begin(), nth, sorted.end());
    s.noiseDb = *nth;
    s.peakDb = *std::max_element(level.begin(), level.end());

    const float gate = std::max(s.noiseDb + o.thresholdDb, o.absoluteFloorDb);
    size_t first = frames, last = 0, voiced = 0;
    for (size_t f = 0; f < frames; ++f) {
        if (level[f] < gate) continue;
        first = std::min(first, f);
        last = f;
        voiced += std::min(kFrame, samples - f * kFrame);
    }
    s.voicedMs = static_cast<int>(voiced * 1000 / kVoiceRate);
    if (first == frames) return s;

    const size_t pad = static_cast<size_t>(std::max(o.padMs, 0)) * kVoiceRate / 1000;
    s.begin = first * kFrame > pad ? first * kFrame - pad : 0;
    s.end = std::min(samples, (last + 1) * kFrame + pad);
    const double recordingMs = static_cast<double>(samples) * 1000.0 / kVoiceRate;
    s.speech = recordingMs >= o.minRecordingMs && s.voicedMs >= o.minSpeechMs;
    return s;
}

float LevelMeter(const float* pcm, size_t samples, int ms) {
    if (!pcm || samples == 0) return 0;
    const size_t want = std::max<size_t>(1, static_cast<size_t>(std::max(ms, 1)) * kVoiceRate / 1000);
    const size_t n = std::min(samples, want);
    return std::clamp((RmsDb(pcm + samples - n, n) + 60.f) / 60.f, 0.f, 1.f);
}

// ---- what the recognizer returns ----

std::wstring CleanTranscript(const std::string& utf8) {
    std::wstring t = FromUtf8(utf8);
    for (auto& c : t)
        if (c == kCut) c = L' ';
    // "[BLANK_AUDIO]", "[Musik]", "[ Silence ]": whisper uses brackets only for these.
    CutPairs(t, L'[', L']', std::wstring::npos, [](const std::wstring&) { return true; });
    CutPairs(t, L'(', L')', 60, IsSoundMarker);
    CutPairs(t, 0xFF08, 0xFF09, 60, IsSoundMarker);  // full-width （拍手）
    // "*lacht*", "*seufzt*": stage directions.
    CutPairs(t, L'*', L'*', 40, [](const std::wstring& c) { return !MatchKey(c).empty(); });
    for (auto& c : t)
        if (c >= 0x2669 && c <= 0x266C) c = kCut;  // ♩ ♪ ♫ ♬ around sung text
    std::wstring out = Collapse(t);
    // "- Hallo": whisper's speaker-turn dash.
    size_t lead = 0;
    while (lead < out.size() && (out[lead] == L'-' || out[lead] == 0x2013 || out[lead] == 0x2014) &&
           (lead + 1 == out.size() || out[lead + 1] == L' '))
        lead = std::min(out.size(), lead + 2);
    out = Trim(out.substr(lead));
    return IsHallucination(MatchKey(out)) ? std::wstring() : out;
}

LiveText SplitLive(const std::wstring& previous, const std::wstring& newest) {
    const std::vector<std::wstring> prev = SplitWords(previous), now = SplitWords(newest);
    size_t k = 0;
    while (k < prev.size() && k + 1 < now.size() && MatchKey(prev[k]) == MatchKey(now[k])) ++k;
    return {JoinWords(now, 0, k), JoinWords(now, k, now.size())};
}

// ---- recognizer settings ----

std::string WhisperLang(const std::wstring& code) {
    // whisper.cpp's languages (v1.9.5, incl. large-v3's "yue").
    static const char* const kWhisper[] = {
        "en", "zh", "de", "es", "ru", "ko", "fr", "ja", "pt", "tr", "pl", "ca", "nl", "ar", "sv", "it", "id", "hi",
        "fi", "vi", "he", "uk", "el", "ms", "cs", "ro", "da", "hu", "ta", "no", "th", "ur", "hr", "bg", "lt", "la",
        "mi", "ml", "cy", "sk", "te", "fa", "lv", "bn", "sr", "az", "sl", "kn", "et", "mk", "br", "eu", "is", "hy",
        "ne", "mn", "bs", "kk", "sq", "sw", "gl", "mr", "pa", "si", "km", "sn", "yo", "so", "af", "oc", "ka", "be",
        "tg", "sd", "gu", "am", "yi", "lo", "uz", "fo", "ht", "ps", "tk", "nn", "mt", "sa", "lb", "my", "bo", "tl",
        "mg", "as", "tt", "haw", "ln", "ha", "ba", "jw", "su", "yue"};
    // Codes whisper spells differently: Norwegian Bokmål, old ISO codes, Javanese, Filipino.
    static const char* const kAlias[][2] = {{"nb", "no"}, {"iw", "he"}, {"in", "id"}, {"ji", "yi"},
                                            {"jv", "jw"}, {"fil", "tl"}};
    std::string primary;
    for (wchar_t c : Trim(code)) {
        if (c == L'-' || c == L'_') break;
        if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32);
        if (c < L'a' || c > L'z' || primary.size() >= 3) return {};
        primary += static_cast<char>(c);
    }
    for (const auto& a : kAlias)
        if (primary == a[0]) primary = a[1];
    for (const char* w : kWhisper)
        if (primary == w) return primary;
    return {};
}

int WhisperAudioCtx(size_t samples) {
    // 50 encoder frames per second (whisper: 1500 for 30 s) + 64 so the last word is not cut off.
    const double seconds = static_cast<double>(samples) / kVoiceRate;
    const double ctx = std::ceil(seconds * 50.0) + 64.0;
    return static_cast<int>(std::clamp(ctx, 256.0, 1500.0));
}

int VoiceThreads(int setting, unsigned logicalProcessors) {
    if (setting > 0) return std::min(setting, 16);
    return static_cast<int>(std::clamp(logicalProcessors / 2u, 1u, 4u));
}

VoiceCorePick PickVoiceCores(const std::vector<float>& busy, const std::vector<std::vector<int>>& cores, int want) {
    VoiceCorePick pick;
    struct Core {
        float load;
        int best;    // its least busy logical processor
        int other;   // a sibling (-1 if none)
    };
    std::vector<Core> list;
    for (const std::vector<int>& c : cores) {
        Core k{0.0f, -1, -1};
        for (int i : c) {
            if (i < 0 || static_cast<size_t>(i) >= busy.size()) continue;
            k.load = std::max(k.load, busy[static_cast<size_t>(i)]);  // a busy sibling slows the whole core
            if (k.best < 0 || busy[static_cast<size_t>(i)] < busy[static_cast<size_t>(k.best)]) {
                if (k.best >= 0) k.other = k.best;
                k.best = i;
            } else if (k.other < 0) {
                k.other = i;
            }
        }
        if (k.best >= 0) list.push_back(k);
    }
    if (list.empty()) return pick;  // no data
    std::stable_sort(list.begin(), list.end(), [](const Core& a, const Core& b) { return a.load < b.load; });
    int n = want;
    if (n <= 0) {
        n = 0;
        for (const Core& k : list) n += k.load < 0.35f ? 1 : 0;
        n = std::clamp(n, 2, 6);
    }
    for (size_t i = 0; i < list.size() && static_cast<int>(pick.logical.size()) < n; ++i) pick.logical.push_back(list[i].best);
    for (size_t i = 0; i < list.size() && static_cast<int>(pick.logical.size()) < n; ++i)  // few cores: siblings too
        if (list[i].other >= 0) pick.logical.push_back(list[i].other);
    std::sort(pick.logical.begin(), pick.logical.end());
    pick.threads = std::min(n, static_cast<int>(pick.logical.size()));
    return pick;
}

// ---- audio format ----

std::vector<float> ToMono16k(const float* interleaved, size_t frames, int channels, int rate) {
    std::vector<float> mono;
    if (!interleaved || frames == 0 || channels <= 0 || rate <= 0) return mono;
    const size_t ch = static_cast<size_t>(channels);
    mono.resize(frames);
    for (size_t i = 0; i < frames; ++i) {
        double sum = 0;
        for (size_t c = 0; c < ch; ++c) sum += interleaved[i * ch + c];
        mono[i] = static_cast<float>(sum / static_cast<double>(ch));
    }
    if (rate == kVoiceRate) return mono;

    if (rate > kVoiceRate) {
        // Box low-pass over the decimation window, so most of what lies above 8 kHz does not fold back into speech.
        const size_t w = static_cast<size_t>(std::lround(static_cast<double>(rate) / kVoiceRate));
        if (w > 1) {
            std::vector<double> sum(frames + 1, 0.0);
            for (size_t i = 0; i < frames; ++i) sum[i + 1] = sum[i] + mono[i];
            const size_t before = (w - 1) / 2, after = w / 2;
            for (size_t i = 0; i < frames; ++i) {
                const size_t lo = i >= before ? i - before : 0, hi = std::min(frames, i + after + 1);
                mono[i] = static_cast<float>((sum[hi] - sum[lo]) / static_cast<double>(hi - lo));
            }
        }
    }
    // Linear interpolation onto the 16 kHz grid.
    const size_t outFrames = static_cast<size_t>(static_cast<uint64_t>(frames) * kVoiceRate / static_cast<unsigned>(rate));
    std::vector<float> out(outFrames);
    const double step = static_cast<double>(rate) / kVoiceRate;
    for (size_t j = 0; j < outFrames; ++j) {
        const double p = static_cast<double>(j) * step;
        const size_t i0 = std::min(static_cast<size_t>(p), frames - 1), i1 = std::min(i0 + 1, frames - 1);
        const double f = p - static_cast<double>(i0);
        out[j] = static_cast<float>(mono[i0] * (1.0 - f) + mono[i1] * f);
    }
    return out;
}

}  // namespace gct
