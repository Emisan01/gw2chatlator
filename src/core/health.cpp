// health.cpp — GW2Chatlator functional health evaluation rules.

#include "health.hpp"

namespace gct {

std::vector<HealthFinding> EvaluateHealth(const HealthInputs& in) {
    std::vector<HealthFinding> findings;

    // 1. NoDictionary: correction enabled, but no OS dictionary ready
    if (in.correctionOn && !in.dictionaryReady) {
        findings.push_back({HealthKind::NoDictionary, 2});
    }

    // 2. CorrectionIdle: correction enabled with dictionary, but 20+ words typed without a single check
    if (in.correctionOn && in.dictionaryReady && in.wordsFinished >= 20 && in.correctionChecks == 0) {
        findings.push_back({HealthKind::CorrectionIdle, 2});
    }

    // 3. LanguageToolDown: LanguageTool enabled, but 3 or more requests in a row failed
    if (in.languageToolUsed && in.ltFailsInRow >= 3) {
        findings.push_back({HealthKind::LanguageToolDown, 1});
    }

    // 4. HelperMissing: local translator selected, but helper executable missing
    if (in.translatorLocal && !in.helperPresent) {
        findings.push_back({HealthKind::HelperMissing, 2});
    }

    // 5. HelperCrashed: local translator helper faulted / crashed
    if (in.translatorLocal && in.helperCrashed) {
        findings.push_back({HealthKind::HelperCrashed, 2});
    }

    // 6. TranslatorFailing: 5 or more translation requests failed in a row
    if (in.mtFailsInRow >= 5) {
        findings.push_back({HealthKind::TranslatorFailing, 2});
    }

    // 7. ChatSilent: Events: Chat companion loaded, but no lines received for > 15 minutes (900,000 ms)
    if (in.chatEventsLoaded && in.msSinceChatLine > 15 * 60 * 1000ULL) {
        findings.push_back({HealthKind::ChatSilent, 1});
    }

    // 8. SendFailing: sending to game failed 2 or more times
    if (in.sendFails >= 2) {
        findings.push_back({HealthKind::SendFailing, 2});
    }

    // 9. WhisperElsewhere: whisper detected as landing in another channel
    if (in.whisperElsewhere >= 1) {
        findings.push_back({HealthKind::WhisperElsewhere, 1});
    }

    return findings;
}

const wchar_t* HealthKindName(HealthKind kind) {
    switch (kind) {
        case HealthKind::NoDictionary: return L"NoDictionary";
        case HealthKind::CorrectionIdle: return L"CorrectionIdle";
        case HealthKind::LanguageToolDown: return L"LanguageToolDown";
        case HealthKind::HelperMissing: return L"HelperMissing";
        case HealthKind::HelperCrashed: return L"HelperCrashed";
        case HealthKind::TranslatorFailing: return L"TranslatorFailing";
        case HealthKind::ChatSilent: return L"ChatSilent";
        case HealthKind::SendFailing: return L"SendFailing";
        case HealthKind::WhisperElsewhere: return L"WhisperElsewhere";
        default: return L"Unknown";
    }
}

}  // namespace gct

