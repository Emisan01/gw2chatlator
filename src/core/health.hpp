// health.hpp — GW2Chatlator functional health evaluation (pure, portable logic).

#pragma once

#include <string>
#include <vector>

namespace gct {

struct HealthInputs {            // numbers and switches only, never text or player names
    bool correctionOn = false;
    bool dictionaryReady = false;
    std::wstring typingLang;
    int wordsFinished = 0;       // since last evaluation
    int correctionChecks = 0;    // since last evaluation
    bool languageToolUsed = false;
    int ltRequests = 0;
    int ltFailsInRow = 0;
    bool translatorLocal = false;
    bool helperPresent = true;
    bool helperCrashed = false;
    int mtFailsInRow = 0;
    bool chatEventsLoaded = false;
    unsigned long long msSinceChatLine = 0;
    int sendFails = 0;
    int whisperElsewhere = 0;
};

enum class HealthKind {
    NoDictionary,
    CorrectionIdle,
    LanguageToolDown,
    HelperMissing,
    HelperCrashed,
    TranslatorFailing,
    ChatSilent,
    SendFailing,
    WhisperElsewhere
};

struct HealthFinding {
    HealthKind kind;
    int severity;  // 1: notice / advisory, 2: function broken / dead
};

std::vector<HealthFinding> EvaluateHealth(const HealthInputs& in);

const wchar_t* HealthKindName(HealthKind kind);

}  // namespace gct

