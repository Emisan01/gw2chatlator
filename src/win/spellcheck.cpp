// spellcheck.cpp
#include "spellcheck.hpp"

#include <windows.h>
#include <objbase.h>
#include <spellcheck.h>

#ifdef _MSC_VER
#pragma comment(lib, "ole32.lib")
#endif

namespace gct {
namespace {

// Defined locally so no uuid library is needed (values from spellcheck.h).
const CLSID kClsidSpellCheckerFactory = {
    0x7ab36653, 0x1796, 0x484b, {0xbd, 0xfa, 0xe7, 0x4f, 0x1d, 0xb7, 0xc1, 0xdc}};
const IID kIidSpellCheckerFactory = {
    0x8e018a9d, 0x2415, 0x4677, {0xbf, 0x08, 0x79, 0x4e, 0xa6, 0x1f, 0x94, 0xbb}};

}  // namespace

SpellChecker::~SpellChecker() {
    if (checker_) checker_->Release();
}

bool SpellChecker::Init(const std::vector<std::wstring>& tags) {
    if (checker_) {
        checker_->Release();
        checker_ = nullptr;
        tag_.clear();
    }
    ISpellCheckerFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(kClsidSpellCheckerFactory, nullptr, CLSCTX_INPROC_SERVER, kIidSpellCheckerFactory,
                                reinterpret_cast<void**>(&factory))) ||
        !factory)
        return false;

    for (const std::wstring& tag : tags) {
        BOOL supported = FALSE;
        if (FAILED(factory->IsSupported(tag.c_str(), &supported)) || !supported) continue;
        ISpellChecker* c = nullptr;
        if (SUCCEEDED(factory->CreateSpellChecker(tag.c_str(), &c)) && c) {
            checker_ = c;
            tag_ = tag;
            break;
        }
    }
    factory->Release();
    return checker_ != nullptr;
}

std::vector<SpellIssue> SpellChecker::Check(const std::wstring& text) const {
    std::vector<SpellIssue> out;
    if (!checker_ || text.empty()) return out;

    IEnumSpellingError* errors = nullptr;
    if (FAILED(checker_->Check(text.c_str(), &errors)) || !errors) return out;

    ISpellingError* e = nullptr;
    while (errors->Next(&e) == S_OK && e) {
        ULONG start = 0, length = 0;
        CORRECTIVE_ACTION action = CORRECTIVE_ACTION_NONE;
        e->get_StartIndex(&start);
        e->get_Length(&length);
        e->get_CorrectiveAction(&action);

        SpellIssue issue;
        issue.span = {start, length};
        if (action == CORRECTIVE_ACTION_REPLACE) {
            LPWSTR rep = nullptr;
            if (SUCCEEDED(e->get_Replacement(&rep)) && rep) {
                issue.kind = SpellIssue::Kind::Replace;
                issue.replacement = rep;
                CoTaskMemFree(rep);
            }
        } else if (action == CORRECTIVE_ACTION_DELETE) {
            issue.kind = SpellIssue::Kind::Delete;
        }
        e->Release();
        e = nullptr;
        if (issue.span.end() <= text.size() && issue.span.length > 0) out.push_back(std::move(issue));
    }
    errors->Release();
    return out;
}

std::vector<std::wstring> SpellChecker::Suggest(const std::wstring& word, size_t max) const {
    std::vector<std::wstring> out;
    if (!checker_ || word.empty()) return out;
    IEnumString* list = nullptr;
    if (FAILED(checker_->Suggest(word.c_str(), &list)) || !list) return out;
    LPOLESTR s = nullptr;
    ULONG fetched = 0;
    while (out.size() < max && list->Next(1, &s, &fetched) == S_OK && fetched == 1) {
        if (s) {
            out.emplace_back(s);
            CoTaskMemFree(s);
            s = nullptr;
        }
    }
    list->Release();
    return out;
}

}  // namespace gct
