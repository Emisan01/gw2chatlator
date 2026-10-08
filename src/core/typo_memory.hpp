// typo_memory.hpp — your own typing mistakes and what you meant ("shon" -> "schon"), per language.
// Learned from your texts (the typing profile) and from every correction you keep while typing; a correction you
// undo with Backspace is forgotten again. A mistake you make again is then corrected without guessing.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace gct {

class TypoMemory {
public:
    // `typo` was meant as `fix`.
    void Add(const std::wstring& typo, const std::wstring& fix, double weight = 1.0);
    // The typo was meant as typed (Backspace after a correction): every fix for it is dropped.
    bool Forget(const std::wstring& typo);
    // Every entry that writes `fix` (the word itself was forgotten).
    void ForgetFix(const std::wstring& fix);
    // What you meant with `typo`, written as stored; empty when unknown or when two fixes compete (the best needs
    // two thirds of the weight).
    std::wstring FixFor(const std::wstring& typo) const;
    size_t Size() const { return map_.size(); }
    void Clear();

    // "typo<TAB>fix<TAB>weight" lines (UTF-8).
    std::string Serialize() const;
    void Parse(const std::string& data);
    bool Dirty() const { return dirty_; }
    void ClearDirty() { dirty_ = false; }

    static constexpr size_t kMaxEntries = 5000;

private:
    struct Fix {
        std::wstring word;
        double weight = 0;
    };
    std::unordered_map<std::wstring, std::vector<Fix>> map_;  // WordKey(typo) -> fixes
    bool dirty_ = false;
};

}  // namespace gct
