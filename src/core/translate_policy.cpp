// translate_policy.cpp — see translate_policy.hpp.

#include "core/translate_policy.hpp"

#include "core/langs.hpp"

namespace gct {

bool ShouldTranslate(const LineToTranslate& l) {
    if (l.trigger == TranslateTrigger::LineClick) return true;
    if (l.trigger == TranslateTrigger::Auto && !l.chatOn) return false;
    if (l.detected.empty() || PrimaryLang(l.detected) == PrimaryLang(l.target)) return false;
    if (l.understood)
        for (const std::wstring& u : *l.understood)
            if (PrimaryLang(u) == PrimaryLang(l.detected)) return false;
    return true;
}

}  // namespace gct
