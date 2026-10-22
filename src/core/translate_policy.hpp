// translate_policy.hpp — whether a chat line goes to the translator: one pure function of what asked for it and what
// is known of the line, so its equations can be checked for every state (core_tests). The automatic translation and
// "Translate visible chat once" follow the same rule – "once" is the thrifty mode (user, 2026-10-22); only a click on
// the line always translates: it asks for it. Portable, no windows.h.
#pragma once

#include <string>
#include <vector>

namespace gct {

enum class TranslateTrigger { Auto, Once, LineClick };

struct LineToTranslate {
    TranslateTrigger trigger = TranslateTrigger::Auto;
    bool chatOn = false;        // "Translate the chat" ([Nexus] TranslateChat) – only Auto asks it
    std::wstring detected;      // SureLanguage: "" = unsure
    std::wstring target;        // the language you write in (the chat is read in it)
    bool skipTarget = true;     // "My writing language" stands in the list of what is not translated by itself
    const std::vector<std::wstring>* understood = nullptr;  // "Languages I understand"
};

// LineClick: always. Auto (only while the chat's translation is on) and Once: only a line whose language is sure, not
// one you understand and – while `skipTarget` – not the language it would go into.
bool ShouldTranslate(const LineToTranslate& l);

}  // namespace gct
