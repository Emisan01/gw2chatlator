// kb_options_parts.hpp — what the files of the options window share: the row widgets and the pages. Only for those files.
#pragma once

#include "kb_shared.hpp"

namespace kb {

extern float g_labelW;  // the label column's width (kb_options.cpp)

// The row widgets (kb_options.cpp)
float ControlW();
bool Switch(const char* id, const std::string& label, bool* v, const char* tip = nullptr, bool usable = true, float maxW = 0.0f);
void RowHint(const std::string& text);
void RiskHint(const std::wstring& text);
bool RowCombo(const char* id, const std::vector<std::string>& items, int* sel);
bool RowSlider(const char* id, int* v, int lo, int hi, const char* format);
bool Hero(bool on, const std::string& title, const std::string& sub, const std::string& state1, bool ok1,
          const std::string& state2, bool ok2);

// Start and the modules (kb_options_start.cpp)
void RenderChatModule();
bool RenderVoiceModule(bool guide);
void RenderRemoveEverything();
void RenderUiLanguageRow(Config& edit);
void RenderStartPage(Config& edit);

// Translator (kb_options_translator.cpp)
std::wstring ReadingLangNow();
const wchar_t* EngineLabel(Engine e);
bool IsLocalEngine(Engine e);
bool LocalModelThere(const LocalMtSummary& local, Engine e);
void RenderTranslatorRows(Config& edit, bool full);
void RenderReadRow(Config& edit);
void RenderUnderstood(Config& edit);
void RenderSendRow();
void RenderTranslatorPage(Config& edit);

// Voice (kb_options_voice.cpp)
void RenderVoiceStart(Config& edit);
void RenderVoicePage(Config& edit, bool entered);

// Chat and Typing (kb_options_typing.cpp)
void RenderWordLists();
void RenderChatPage(Config& edit);
void RenderTypingPage(Config& edit);

// Window (kb_options_window.cpp)
void RenderWindowPage(Config& edit);

// Technical (kb_options_tech.cpp)
void RenderTranslatorLive();
void RenderTechnicalPage(Config& edit);

}  // namespace kb
