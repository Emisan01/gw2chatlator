// rapid_models.hpp — the RapidOCR recognition models (PP-OCRv5, open source,
// Apache-2.0, hosted by the RapidAI project on ModelScope) and which languages
// each covers. Only the groups for the user's languages are installed; Latin
// (English, German, French, Spanish, Turkish, Polish …) always.
#pragma once

#include <string>
#include <vector>

namespace gct {

struct RapidModelGroup {
    const wchar_t* id;       // "latin" (also the file prefix)
    const wchar_t* name;     // English UI text: "Latin script (EN, DE, FR, ES, TR, PL …)"
    const wchar_t* recUrl;   // .onnx
    const char* recSha256;   // lower-case hex
    const wchar_t* dictUrl;  // .txt
    const wchar_t* langs;    // primary codes covered, comma separated ("" = everything Latin)
    int sizeMb;              // rounded download size
};

const std::vector<RapidModelGroup>& RapidModelGroups();
const RapidModelGroup* FindRapidGroup(const std::wstring& id);

// Groups needed for these language codes ("DE", "AR", "ZH-HANS" …), Latin first and always.
std::vector<std::wstring> RapidGroupsFor(const std::vector<std::wstring>& langs);

// File names inside the models folder.
std::wstring RapidModelFile(const RapidModelGroup& g);  // "latin_rec.onnx"
std::wstring RapidDictFile(const RapidModelGroup& g);   // "latin_dict.txt"

}  // namespace gct
