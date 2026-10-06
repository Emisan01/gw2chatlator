#include "core/tesseract_tsv.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <tuple>

#include "core/text.hpp"

namespace gct {
namespace {

std::vector<std::string> SplitTabs(const std::string& line) {
    std::vector<std::string> f;
    size_t s = 0;
    for (;;) {
        size_t tab = line.find('\t', s);
        f.push_back(line.substr(s, tab == std::string::npos ? std::string::npos : tab - s));
        if (tab == std::string::npos) break;
        s = tab + 1;
    }
    return f;
}

int ToInt(const std::string& s) { return static_cast<int>(std::strtol(s.c_str(), nullptr, 10)); }

bool Installed(const std::vector<std::string>& installed, const std::string& m) {
    return std::find(installed.begin(), installed.end(), m) != installed.end();
}

}  // namespace

std::vector<TsvLine> ParseTesseractTsv(const std::string& tsv, float minConfidence) {
    struct Acc {
        TsvLine line;
        int top = 1 << 30;
        float best = -1;
    };
    std::map<std::tuple<int, int, int, int>, Acc> lines;  // page, block, par, line
    size_t pos = 0;
    bool header = true;
    while (pos < tsv.size()) {
        size_t eol = tsv.find('\n', pos);
        if (eol == std::string::npos) eol = tsv.size();
        std::string row = tsv.substr(pos, eol - pos);
        pos = eol + 1;
        if (!row.empty() && row.back() == '\r') row.pop_back();
        if (header) {  // "level\tpage_num\t..."
            header = false;
            if (row.rfind("level", 0) == 0) continue;
        }
        const std::vector<std::string> f = SplitTabs(row);
        if (f.size() < 12 || ToInt(f[0]) != 5) continue;
        const std::wstring text = Trim(FromUtf8(f[11]));
        if (text.empty()) continue;
        TsvWord w;
        w.text = text;
        w.rect = {ToInt(f[6]), ToInt(f[7]), ToInt(f[8]), ToInt(f[9])};
        w.confidence = static_cast<float>(std::strtod(f[10].c_str(), nullptr));
        Acc& a = lines[{ToInt(f[1]), ToInt(f[2]), ToInt(f[3]), ToInt(f[4])}];
        a.top = std::min(a.top, w.rect.y);
        a.best = std::max(a.best, w.confidence);
        a.line.words.push_back(std::move(w));
    }
    std::vector<std::pair<int, TsvLine>> sorted;
    for (auto& [key, a] : lines) {
        if (a.best < minConfidence) continue;
        std::sort(a.line.words.begin(), a.line.words.end(),
                  [](const TsvWord& x, const TsvWord& y) { return x.rect.x < y.rect.x; });
        for (const TsvWord& w : a.line.words) a.line.text += (a.line.text.empty() ? L"" : L" ") + w.text;
        sorted.push_back({a.top, std::move(a.line)});
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const auto& x, const auto& y) { return x.first < y.first; });
    std::vector<TsvLine> out;
    for (auto& s : sorted) out.push_back(std::move(s.second));
    return out;
}

std::string TesseractModel(const std::wstring& code) {
    const std::wstring c = ToUpperAscii(Trim(code));
    static const std::pair<const wchar_t*, const char*> map[] = {
        {L"EN", "eng"},      {L"DE", "deu"},      {L"FR", "fra"},     {L"ES", "spa"},     {L"IT", "ita"},
        {L"PT", "por"},      {L"NL", "nld"},      {L"PL", "pol"},     {L"CS", "ces"},     {L"SK", "slk"},
        {L"HU", "hun"},      {L"RO", "ron"},      {L"SV", "swe"},     {L"DA", "dan"},     {L"NB", "nor"},
        {L"NO", "nor"},      {L"FI", "fin"},      {L"TR", "tur"},     {L"RU", "rus"},     {L"UK", "ukr"},
        {L"BG", "bul"},      {L"EL", "ell"},      {L"AR", "ara"},     {L"HE", "heb"},     {L"JA", "jpn"},
        {L"KO", "kor"},      {L"ZH-HANT", "chi_tra"}, {L"ZH-TW", "chi_tra"}, {L"ZH", "chi_sim"}, {L"LT", "lit"},
        {L"LV", "lav"},      {L"ET", "est"},      {L"SL", "slv"},     {L"ID", "ind"},     {L"VI", "vie"},
        {L"TH", "tha"},      {L"HI", "hin"},
    };
    for (const auto& [k, m] : map)
        if (c == k) return m;
    for (const auto& [k, m] : map) {  // "EN-GB" -> "EN", "ZH-HANS" -> "ZH"
        const std::wstring key = k;
        if (key.find(L'-') == std::wstring::npos && c.size() > key.size() && c.compare(0, key.size(), key) == 0 &&
            c[key.size()] == L'-')
            return m;
    }
    return {};
}

std::string ChooseTesseractLangs(const std::string& configured, const std::vector<std::string>& installed,
                                 bool chinese) {
    std::vector<std::string> pick;
    auto add = [&](const std::string& m) {
        if (!m.empty() && Installed(installed, m) && std::find(pick.begin(), pick.end(), m) == pick.end())
            pick.push_back(m);
    };
    if (!configured.empty()) {
        size_t s = 0;
        while (s <= configured.size()) {
            size_t plus = configured.find_first_of("+, ", s);
            if (plus == std::string::npos) plus = configured.size();
            add(configured.substr(s, plus - s));
            s = plus + 1;
        }
    }
    if (pick.empty()) {
        for (const char* m : {"eng", "deu", "fra", "spa"}) add(m);
        if (chinese) add("chi_sim");
    }
    if (pick.empty()) {
        if (Installed(installed, "eng")) pick.push_back("eng");
        else
            for (const std::string& m : installed)
                if (m != "osd" && m != "equ") {
                    pick.push_back(m);
                    break;
                }
    }
    std::string out;
    for (const std::string& m : pick) out += (out.empty() ? "" : "+") + m;
    return out;
}

std::string EncodePgm(const Image& img, bool invert) {
    if (img.Empty()) return {};
    std::string out = "P5\n" + std::to_string(img.width) + " " + std::to_string(img.height) + "\n255\n";
    const size_t header = out.size();
    out.resize(header + static_cast<size_t>(img.width) * img.height);
    for (size_t i = 0, n = static_cast<size_t>(img.width) * img.height; i < n; ++i) {
        const uint8_t b = img.bgra[i * 4], g = img.bgra[i * 4 + 1], r = img.bgra[i * 4 + 2];
        const int v = (r * 77 + g * 150 + b * 29) >> 8;
        out[header + i] = static_cast<char>(invert ? 255 - v : v);
    }
    return out;
}

}  // namespace gct
