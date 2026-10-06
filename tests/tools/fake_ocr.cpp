// fake_ocr.cpp — deterministic stand-in for win/ocr.cpp (Wine has no
// Windows.Media.Ocr). Linked into GW2ChatTranslator_fakes.
//
// Instead of recognising pixels it reads fake_chat.txt from the working
// directory — the same file fake_gw2.exe paints into its chat panel, so the
// screen capture still sees a change whenever a line arrives. Format, UTF-8,
// one chat line per text line, the last 10 lines are "visible":
//
//   RRGGBB|[M] Some Player: bonjour à tous
//
// Lines are reported with their colour (as the real reader samples it) and
// a 16 px line pitch, like GW2 at "normal" interface size.
#include "win/ocr.hpp"

#include <windows.h>

#include <fstream>
#include <sstream>

#include "core/text.hpp"

namespace gct {

struct OcrEngine::Impl {
    bool ready = false;
};

OcrEngine::OcrEngine() : impl_(std::make_unique<Impl>()) {}
OcrEngine::~OcrEngine() = default;

bool OcrEngine::Init(const std::wstring&, std::wstring*) {
    impl_->ready = true;
    return true;
}

bool OcrEngine::Ready() const { return impl_->ready; }
std::wstring OcrEngine::Language() const { return L"fake"; }
int OcrEngine::MaxImageDimension() const { return 10000; }

bool OcrEngine::Recognize(const Image& img, std::vector<OcrTextLine>& out, std::wstring* error) {
    out.clear();
    std::ifstream f("fake_chat.txt", std::ios::binary);
    if (!f) {
        if (error) *error = L"fake_chat.txt fehlt";
        return true;  // an empty chat is not an error
    }
    std::stringstream ss;
    ss << f.rdbuf();
    std::vector<std::wstring> lines;
    std::wstring all = FromUtf8(ss.str());
    size_t start = 0;
    while (start <= all.size()) {
        size_t end = all.find(L'\n', start);
        if (end == std::wstring::npos) end = all.size();
        std::wstring line = all.substr(start, end - start);
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty()) lines.push_back(line);
        start = end + 1;
    }
    const size_t first = lines.size() > 10 ? lines.size() - 10 : 0;
    // Boxes as the real engine reports them: in the prepared image, which the
    // reader enlarged by OcrScale (default 2) and scales back.
    (void)img;
    constexpr int zoom = 2;
    int row = 0;
    for (size_t i = first; i < lines.size(); ++i, ++row) {
        const size_t bar = lines[i].find(L'|');
        if (bar != 6) continue;
        OcrTextLine tl;
        tl.hasColor = RgbFromHex(lines[i].substr(0, 6), tl.color);
        tl.text = lines[i].substr(7);
        OcrWordBox w;
        w.text = tl.text;
        w.rect = RectI{2 * zoom, (4 + row * 16) * zoom, 200 * zoom, 12 * zoom};
        tl.words.push_back(w);
        out.push_back(std::move(tl));
    }
    return true;
}

}  // namespace gct
