// rapid_models.cpp — URLs and checksums from RapidOCR's default_models.yaml (v3.9.2).
#include "rapid_models.hpp"

#include <algorithm>

#include "text.hpp"

namespace gct {

namespace {
#define GCT_RAPID_BASE L"https://www.modelscope.cn/models/RapidAI/RapidOCR/resolve/v3.9.2/"
}  // namespace

const std::vector<RapidModelGroup>& RapidModelGroups() {
    static const std::vector<RapidModelGroup> groups = {
        {L"latin", L"Latin script (EN, DE, FR, ES, IT, PT, NL, PL, TR …)",
         GCT_RAPID_BASE L"onnx/PP-OCRv5/rec/latin_PP-OCRv5_rec_mobile.onnx",
         "b20bd37c168a570f583afbc8cd7925603890efbcdc000a59e22c269d160b5f5a",
         GCT_RAPID_BASE L"paddle/PP-OCRv5/rec/latin_PP-OCRv5_rec_mobile/ppocrv5_latin_dict.txt", L"", 8},
        {L"cyrillic", L"Cyrillic (RU, UK, BG …)", GCT_RAPID_BASE L"onnx/PP-OCRv5/rec/cyrillic_PP-OCRv5_rec_mobile.onnx",
         "90f761b4bfcce0c8c561c0cb5c887b0971d3ec01c32164bdf7374a35b0982711",
         GCT_RAPID_BASE L"paddle/PP-OCRv5/rec/cyrillic_PP-OCRv5_rec_mobile/ppocrv5_cyrillic_dict.txt", L"RU,UK,BG,SR,BE,KK", 8},
        {L"arabic", L"Arabic script (AR, FA, UR)", GCT_RAPID_BASE L"onnx/PP-OCRv5/rec/arabic_PP-OCRv5_rec_mobile.onnx",
         "c1192e632d0baa9146ae5b756a0e635e3dc63c1733737ebfd1629e87144e9295",
         GCT_RAPID_BASE L"paddle/PP-OCRv5/rec/arabic_PP-OCRv5_rec_mobile/ppocrv5_arabic_dict.txt", L"AR,FA,UR", 8},
        {L"korean", L"Korean", GCT_RAPID_BASE L"onnx/PP-OCRv5/rec/korean_PP-OCRv5_rec_mobile.onnx",
         "cd6e2ea50f6943ca7271eb8c56a877a5a90720b7047fe9c41a2e541a25773c9b",
         GCT_RAPID_BASE L"paddle/PP-OCRv5/rec/korean_PP-OCRv5_rec_mobile/ppocrv5_korean_dict.txt", L"KO", 13},
        {L"chinese", L"Chinese and Japanese (with English)", GCT_RAPID_BASE L"onnx/PP-OCRv5/rec/ch_PP-OCRv5_rec_mobile.onnx",
         "5825fc7ebf84ae7a412be049820b4d86d77620f204a041697b0494669b1742c5",
         GCT_RAPID_BASE L"paddle/PP-OCRv5/rec/ch_PP-OCRv5_rec_mobile/ppocrv5_dict.txt", L"ZH,JA", 17},
    };
    return groups;
}

const RapidModelGroup* FindRapidGroup(const std::wstring& id) {
    for (const RapidModelGroup& g : RapidModelGroups())
        if (id == g.id) return &g;
    return nullptr;
}

std::vector<std::wstring> RapidGroupsFor(const std::vector<std::wstring>& langs) {
    std::vector<std::wstring> out{L"latin"};
    for (const std::wstring& l : langs) {
        std::wstring p = ToUpperAscii(Trim(l));
        p = p.substr(0, p.find(L'-'));
        for (const RapidModelGroup& g : RapidModelGroups()) {
            if (!*g.langs) continue;
            const std::wstring list = std::wstring(L",") + g.langs + L",";
            if (list.find(L"," + p + L",") != std::wstring::npos &&
                std::find(out.begin(), out.end(), g.id) == out.end())
                out.push_back(g.id);
        }
    }
    return out;
}

std::wstring RapidModelFile(const RapidModelGroup& g) { return std::wstring(g.id) + L"_rec.onnx"; }
std::wstring RapidDictFile(const RapidModelGroup& g) { return std::wstring(g.id) + L"_dict.txt"; }

}  // namespace gct
