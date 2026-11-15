// kb_window_footer.cpp — the main window's footer: channel, send language, translator, what is on, the counter.

#include "kb_shared.hpp"

namespace kb {

// The choices at "Send" in a menu that is open – the footer's and the options' "I send" (one setting, user
// 2026-10-29): "as written" first when GW2 can show the typing language, your languages, then more (a chosen one
// becomes the first of yours). `tabAware`: a whisper tab in front keeps its own choice (the footer); without it the
// one for everything else. What you say follows it too (send_route). g_mu held.
void SendLanguageItems(bool tabAware) {
    const bool inTab = tabAware && g_activeTab >= 0 && g_activeTab < static_cast<int>(g_tabs.size());
    const size_t n = g_cfg.writeLangs.size();
    auto chosen = [&] {
        ++g_inputGen;
        g_preview.clear();
        g_inputChanged = GetTickCount64();
        if (tabAware) g_focusInput = true;
        ImGui::CloseCurrentPopup();
    };
    auto choose = [&](size_t i) {
        if (!g_nx.translateChat) SetChatTranslation(true);
        if (inTab) {  // this player's tab only
            g_tabs[g_activeTab].sendLang = i == n ? std::wstring(L"=") : g_cfg.writeLangs[i];
            g_sendOverride = g_tabs[g_activeTab].sendLang;
        } else {
            g_sendIdx = static_cast<int>(i);
            RememberSend();
        }
        LogState(L"the send language");
        chosen();
    };
    const bool tabChoice = inTab && !g_sendOverride.empty();
    const bool isOff = !g_nx.translateChat;
    if (ImGui::Selectable(UI(Tr(L"Off (no translation)")).c_str(), isOff)) {
        if (!isOff) SetChatTranslation(false);
        chosen();
        return;
    }
    if (SendableInGw2(TypingLang())) {
        const bool current = !isOff && (tabChoice ? g_sendOverride == L"=" : g_sendIdx == static_cast<int>(n));
        if (ImGui::Selectable(UI(Tr(L"as written")).c_str(), current)) choose(n);
    }
    for (size_t i = 0; i < n; ++i) {
        const bool current = !isOff && (tabChoice ? g_sendOverride == g_cfg.writeLangs[i] : g_sendIdx == static_cast<int>(i));
        if (ImGui::Selectable(U8(NativeName(g_cfg.writeLangs[i])).c_str(), current)) choose(i);
    }
    ImGui::Separator();
    for (const LangInfo& l : Languages()) {  // more languages: the chosen one becomes the first
        if (!l.latinScript || std::find(g_cfg.writeLangs.begin(), g_cfg.writeLangs.end(), l.code) != g_cfg.writeLangs.end())
            continue;
        if (ImGui::Selectable(U8(NativeName(l.code)).c_str(), false)) {
            if (!g_nx.translateChat) SetChatTranslation(true);
            g_cfg.writeLangs.insert(g_cfg.writeLangs.begin(), l.code);
            g_cfg.SaveAll();
            if (inTab) {
                ++g_sendIdx;  // everything else keeps its language: the list moved by one
                g_tabs[g_activeTab].sendLang = l.code;
                g_sendOverride = l.code;
            } else {
                g_sendIdx = 0;
                RememberSend();
            }
            chosen();
        }
    }
}

float FooterHeight() {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float fs = ImGui::GetFontSize();
    const float capFs = std::max(8.0f, std::round(fs * 0.75f));
    return std::ceil(st.FramePadding.y * 2.0f + capFs + 1.0f + ImGui::GetTextLineHeight());
}

static bool FooterField(const char* id, const char* caption, const char* value, const ImVec4& valueColor, float width) {
    const ImGuiStyle& st = ImGui::GetStyle();
    const float h = FooterHeight();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const bool clicked = ImGui::InvisibleButton(id, ImVec2(width, h));
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImU32 bg = ImGui::GetColorU32(active ? ImGuiCol_ButtonActive : hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + h), bg, st.FrameRounding);
    if (st.FrameBorderSize > 0.0f) {
        dl->AddRect(pos, ImVec2(pos.x + width, pos.y + h), ImGui::GetColorU32(ImGuiCol_Border), st.FrameRounding, 0, st.FrameBorderSize);
    }
    ImFont* font = ImGui::GetFont();
    const float fs = ImGui::GetFontSize();
    const float capFs = std::max(8.0f, std::round(fs * 0.75f));
    const float x = pos.x + st.FramePadding.x + 4.0f;
    const float yCap = pos.y + st.FramePadding.y;
    const float yVal = yCap + capFs + 1.0f;
    dl->AddText(font, capFs, ImVec2(x, yCap), ImGui::GetColorU32(ImGuiCol_TextDisabled), caption);
    dl->AddText(font, fs, ImVec2(x, yVal), ImGui::GetColorU32(valueColor), value);
    return clicked;
}

// The footer chips: channel | write | send · translator | auto | counter.
void RenderFooter() {  // g_mu held
    static const Channel kChannels[] = {Channel::Unknown, Channel::Say,   Channel::Map,  Channel::Party,
                                        Channel::Squad,   Channel::Guild, Channel::Team, Channel::Whisper};
    auto fieldWidth = [](const std::string& cap, const std::string& val) {
        ImFont* font = ImGui::GetFont();
        const float fs = ImGui::GetFontSize();
        const float capFs = std::max(8.0f, std::round(fs * 0.75f));
        const float cw = font->CalcTextSizeA(capFs, FLT_MAX, 0.0f, cap.c_str()).x;
        const float vw = ImGui::CalcTextSize(val.c_str()).x;
        return std::ceil(std::max(cw, vw) + ImGui::GetStyle().FramePadding.x * 2.0f + 8.0f);
    };
    const float iconH = ImGui::GetFrameHeight();
    const float fieldH = FooterHeight();

    // Native minimize button (bottom-left corner of the window)
    if (Shown().minButton) {
        if (ImGui::Button(">##minchat", ImVec2(iconH, fieldH))) MinimizeWindow();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Minimize chat")).c_str());
        ImGui::SameLine();
    }

    // Footer chips cached: precalculated strings and sizes avoid allocations on idle frames.
    struct FooterCache {
        UiLang uiLang = static_cast<UiLang>(-1);
        float fontSize = 0.0f;
        Channel channel = Channel::Unknown;
        int guildSlot = 0;
        std::wstring whisperTo;
        std::wstring writeIn;
        std::wstring typingLang;
        std::wstring sendLang;
        bool sendOrig = false;
        bool translateChat = false;
        std::string capChannel, capWrite, capSend;
        std::string labelFull, labelNarrow;
        std::string writeFull, writeNarrow;
        std::string sendFull, sendNarrow;
        float labelWFull = 0, labelWNarrow = 0;
        float writeWFull = 0, writeWNarrow = 0;
        float sendWFull = 0, sendWNarrow = 0;
        float totalNeedFull = 0;
    };
    static FooterCache s_fc;
    const bool sendOrig = SendOriginal();
    const std::wstring typingLang = TypingLang();
    const std::wstring sendLang = SendLang();
    const float curFontPx = ImGui::GetFontSize();
    if (s_fc.uiLang != g_cfg.uiLang || s_fc.fontSize != curFontPx || s_fc.channel != g_channel || s_fc.guildSlot != g_guildSlot || s_fc.whisperTo != g_whisperTo ||
        s_fc.writeIn != g_cfg.writeIn || s_fc.typingLang != typingLang || s_fc.sendLang != sendLang || s_fc.sendOrig != sendOrig || s_fc.translateChat != g_nx.translateChat) {
        s_fc.uiLang = g_cfg.uiLang;
        s_fc.fontSize = curFontPx;
        s_fc.channel = g_channel;
        s_fc.guildSlot = g_guildSlot;
        s_fc.whisperTo = g_whisperTo;
        s_fc.writeIn = g_cfg.writeIn;
        s_fc.typingLang = typingLang;
        s_fc.sendLang = sendLang;
        s_fc.sendOrig = sendOrig;
        s_fc.translateChat = g_nx.translateChat;

        s_fc.capChannel = UI(Tr(L"Channel"));
        s_fc.capWrite = UI(Tr(L"I write in"));
        s_fc.labelFull = g_channel == Channel::Unknown ? UI(Tr(L"Active channel")) : UI(ChannelLabel(g_channel));
        if (g_channel == Channel::Guild && g_guildSlot > 0) s_fc.labelFull = "G" + std::to_string(g_guildSlot);
        if (g_channel == Channel::Whisper && !g_whisperTo.empty()) {
            if (g_whisperTo == g_ownName) s_fc.labelFull = "/self";
            else s_fc.labelFull += " " + U8(g_whisperTo);
        }
        s_fc.labelNarrow = g_channel == Channel::Unknown ? UI(Tr(L"Active")) : s_fc.labelFull;

        s_fc.writeFull = U8(NativeName(typingLang));
        s_fc.writeNarrow = U8(ToUpperAscii(PrimaryLang(typingLang)));

        const std::wstring sendText = NativeName(sendLang);
        if (!g_nx.translateChat) {
            s_fc.capSend = UI(Tr(L"Translation"));
            s_fc.sendFull = UI(Tr(L"off"));
            s_fc.sendNarrow = s_fc.sendFull;
        } else {
            s_fc.capSend = UI(Tr(L"Goes out as"));
            s_fc.sendFull = sendOrig ? UI(Tr(L"as written")) : U8(sendText);
            s_fc.sendNarrow = sendOrig ? UI(Tr(L"as written")) : "> " + U8(ToUpperAscii(PrimaryLang(sendLang)));
        }

        s_fc.labelWFull = fieldWidth(s_fc.capChannel, s_fc.labelFull);
        s_fc.labelWNarrow = fieldWidth(s_fc.capChannel, s_fc.labelNarrow);
        s_fc.writeWFull = fieldWidth(s_fc.capWrite, s_fc.writeFull);
        s_fc.writeWNarrow = fieldWidth(s_fc.capWrite, s_fc.writeNarrow);
        s_fc.sendWFull = fieldWidth(s_fc.capSend, s_fc.sendFull);
        s_fc.sendWNarrow = fieldWidth(s_fc.capSend, s_fc.sendNarrow);
        s_fc.totalNeedFull = iconH + s_fc.labelWFull + s_fc.writeWFull + s_fc.sendWFull +
                             ImGui::GetFrameHeight() * 5 + ImGui::CalcTextSize("000/199").x;
    }

    const bool narrow = s_fc.totalNeedFull > ImGui::GetContentRegionAvail().x;
    const std::string& chVal = narrow ? s_fc.labelNarrow : s_fc.labelFull;
    const float chBtnW = narrow ? s_fc.labelWNarrow : s_fc.labelWFull;

    const ImVec2 chBtnPos = ImGui::GetCursorScreenPos();
    const ImVec4 chColor = NameColorOf(g_channel == Channel::Unknown ? Channel::Say : g_channel);
    const bool chLeft = FooterField("##chbtn", s_fc.capChannel.c_str(), chVal.c_str(), chColor, chBtnW);
    const bool chRight = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);

    if (chLeft || chRight) {
        ImGui::OpenPopup("##channel_popup");
    }

    // Dropdown menu opens UPWARDS directly above the button, overlaying the window upwards!
    ImGui::SetNextWindowPos(ImVec2(chBtnPos.x, chBtnPos.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(chBtnW, 0.0f), ImVec2(FLT_MAX, ImGui::GetFontSize() * 18.0f));
    if (ImGui::BeginPopup("##channel_popup")) {
        ScopedPopupFont popupFont;
        const auto choose = [](Channel c, int slot) {
            if (c != Channel::Whisper && g_activeTab >= 0) LeaveWhisperTab();  // the whisper tab is left
            g_channel = c;
            g_guildSlot = slot;
            if (c != Channel::Whisper) g_whisperTo.clear();
            g_focusInput = true;
            RequestChannelSync();  // GW2's line follows – every click, also on the channel already chosen
            ImGui::CloseCurrentPopup();
        };
        const bool toSelf = g_channel == Channel::Whisper && !g_ownName.empty() && g_whisperTo == g_ownName;
        for (Channel c : kChannels) {
            const std::string name = c == Channel::Unknown ? UI(Tr(L"Active channel")) : UI(ChannelLabel(c));
            ImGui::PushStyleColor(ImGuiCol_Text, NameColorOf(c == Channel::Unknown ? Channel::Say : c));
            if (ImGui::Selectable(name.c_str(), c == g_channel && (c != Channel::Guild || g_guildSlot == 0) && !(c == Channel::Whisper && toSelf))) {
                if (c == Channel::Whisper && toSelf) g_whisperTo.clear();
                choose(c, 0);
            }
            if (c == Channel::Whisper && !g_ownName.empty()) {  // a whisper to yourself, on whichever character you are
                if (ImGui::Selectable("/self", toSelf)) {
                    if (g_activeTab >= 0) LeaveWhisperTab();
                    g_whisperTo = g_ownName;
                    choose(Channel::Whisper, 0);
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Whisper yourself")).c_str());
            }
            if (c == Channel::Guild) {  // your guilds by their slot, one row: also those you do not represent
                for (int slot = 1; slot <= 5; ++slot) {
                    const std::string label = "G" + std::to_string(slot);
                    if (slot > 1) ImGui::SameLine();
                    if (ImGui::Selectable(label.c_str(), g_channel == Channel::Guild && g_guildSlot == slot, 0, ImVec2(ImGui::CalcTextSize(label.c_str()).x, 0.0f)))
                        choose(Channel::Guild, slot);
                }
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }
    // The language you write in (spelling, learned words and the prediction follow it).
    ImGui::SameLine();
    const ImVec2 writeBtnPos = ImGui::GetCursorScreenPos();
    const float writeBtnW = narrow ? s_fc.writeWNarrow : s_fc.writeWFull;
    const std::string& writeVal = narrow ? s_fc.writeNarrow : s_fc.writeFull;
    const ImVec4 textColor = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const bool writeLeft = FooterField("##writebtn", s_fc.capWrite.c_str(), writeVal.c_str(), textColor, writeBtnW);
    const bool writeRight = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);
    if (writeLeft || writeRight) {
        ImGui::OpenPopup("##write_popup");
    }
    ImGui::SetNextWindowPos(ImVec2(writeBtnPos.x, writeBtnPos.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(writeBtnW, 0.0f), ImVec2(FLT_MAX, ImGui::GetFontSize() * 20.0f));
    if (ImGui::BeginPopup("##write_popup")) {
        ScopedPopupFont popupFont;
        std::wstring chosen = g_cfg.writeIn;
        if (ImGui::Selectable(UI(Tr(L"System")).c_str(), chosen.empty())) {
            chosen.clear();
            ImGui::CloseCurrentPopup();
        }
        for (const LangInfo& l : Languages()) {
            // every language our window draws – Cyrillic, Greek too: sent, it is always translated (send_route)
            if (DrawnInOurWindow(l) && ImGui::Selectable(U8(NativeName(l.code)).c_str(), g_cfg.writeIn == l.code)) {
                chosen = l.code;
                ImGui::CloseCurrentPopup();
            }
        }
        if (chosen != g_cfg.writeIn) {
            g_cfg.writeIn = chosen;
            g_cfg.SaveValue(L"Spelling", L"WriteIn", chosen);
            LogState(L"the typing language (footer)");
            g_worker.Post([] {  // spelling and learned words follow the language you write in
                FreeWorkerState();
                InitSpelling();
            });
            ++g_inputGen;
            g_inputChanged = GetTickCount64();
            // the chat is read in its own language (the chat tab's menu): nothing to translate again here
        }
        ImGui::EndPopup();
    }

    // What the chat gets (the theme's accent); who translates is told by the status line.
    ImGui::SameLine();
    const ImVec2 sendBtnPos = ImGui::GetCursorScreenPos();
    const float sendBtnW = narrow ? s_fc.sendWNarrow : s_fc.sendWFull;
    const std::string& sendVal = narrow ? s_fc.sendNarrow : s_fc.sendFull;
    const ImVec4 sendColor = !g_nx.translateChat ? ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled) : UiAccent();
    const bool sendLeft = FooterField("##sendbtn", s_fc.capSend.c_str(), sendVal.c_str(), sendColor, sendBtnW);
    if (!g_nx.translateChat && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", UI(Tr(L"Translation is switched off. Click to enable or select language.")).c_str());
    }
    const bool sendRight = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);
    if (sendLeft || sendRight) {
        ImGui::OpenPopup("##send_popup");
    }
    ImGui::SetNextWindowPos(ImVec2(sendBtnPos.x, sendBtnPos.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(sendBtnW, 0.0f), ImVec2(FLT_MAX, ImGui::GetFontSize() * 20.0f));
    if (ImGui::BeginPopup("##send_popup")) {
        ScopedPopupFont popupFont;
        SendLanguageItems(true);
        ImGui::EndPopup();
    }
    if (g_myMemorySessionFallback) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, UiAccent());
        const std::string fbLabel = UI(Tr(L"MyMemory (fallback)"));
        if (ImGui::Button((fbLabel + "##fbchip").c_str())) {
            g_myMemorySessionFallback = false;
            const Config cfg = g_cfg;
            g_worker.Post([cfg] {
                auto tr = MakeTranslator(cfg);
                std::lock_guard<std::mutex> lock(g_mu);
                g_translator = std::move(tr);
            });
            ++g_inputGen;
            g_inputChanged = GetTickCount64();
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", UI(Tr(L"Click to restore original translator")).c_str());
        }
    }
    // The counter comes only once a text no longer fits one chat line, and shows the lines it goes out as: "199/100".
    size_t lines = 0;
    std::string counter;
    const bool sendsPreview = (Translating() || NamesSwapped()) && g_previewGen == g_inputGen && !g_preview.empty();
    const size_t limit = static_cast<size_t>(g_cfg.maxLength);
    if (g_input[0] && CodePointCount(sendsPreview ? g_preview : g_wFlatInput) + 64 >= limit) {  // far below it: no prefix is this long
        const ChatSplit own = SplitChatCommand(SanitizeChatText(g_wFlatInput));  // as StartSend counts it
        const std::wstring& body = own.prefix.empty() ? g_wFlatInput : own.body;
        const std::vector<std::wstring> parts = SplitForChat(sendsPreview ? g_preview : body, own.prefix.empty() ? ComposePrefix() : own.prefix, limit);
        lines = parts.size();
        if (lines >= 2)
            for (const std::wstring& part : parts) counter += (counter.empty() ? "" : "/") + std::to_string(CodePointCount(part));
    }
    // Bottom right, never over the chips: the button stays at the edge, the counter comes left of it; without room they go into the next line.
    // ImGui's own resize corner takes the clicks about one font size from the window's corner: the button stays clear of it.
    const float gripRoom = std::max(0.0f, ImGui::GetFontSize() * 1.05f - ImGui::GetStyle().WindowPadding.x);
    const float infoX = ImGui::GetWindowContentRegionMax().x - iconH - gripRoom;
    const float rowX = counter.empty() ? infoX : infoX - ImGui::CalcTextSize(counter.c_str()).x - ImGui::GetStyle().ItemSpacing.x;
    const float after = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetStyle().ItemSpacing.x * 2;
    if (rowX >= after) ImGui::SameLine(rowX);
    else ImGui::SetCursorPosX(std::max(rowX, ImGui::GetCursorPosX()));
    if (!counter.empty()) {
        const std::wstring count = std::to_wstring(lines);
        const bool tooMany = lines > kMaxPartsPerEnter;  // one Enter does not send this: StartSend refuses it
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(tooMany ? UiWarm() : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), "%s", counter.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", UI(tooMany ? TrF(L"Not sent: {1} chat lines – at most 3 per Enter. Shorten it.", {count})
                                               : TrF(L"Goes out as {1} chat lines, one after the other", {count})).c_str());
        ImGui::SameLine(infoX);
    }
    // The info texts again: in the box they are often gone before you have read them.
    const ImVec2 infoBtnPos = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    if (ImGui::Button("i##infolog", ImVec2(iconH, fieldH))) ImGui::OpenPopup("##infolog_popup");
    ImGui::PopStyleColor();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Info texts of this session")).c_str());
    const float infoW = ImGui::GetFontSize() * 26.0f;
    ImGui::SetNextWindowPos(ImVec2(std::max(infoW, infoBtnPos.x + iconH), infoBtnPos.y - 4.0f), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(infoW, 0.0f), ImVec2(infoW, ImGui::GetFontSize() * 20.0f));
    if (ImGui::BeginPopup("##infolog_popup")) {
        ScopedPopupFont popupFont;
        if (g_infoLog.empty()) ImGui::TextDisabled("%s", UI(Tr(L"No info texts yet")).c_str());
        for (auto it = g_infoLog.rbegin(); it != g_infoLog.rend(); ++it) {  // the newest on top
            ImGui::TextDisabled("%s", it->clock.c_str());
            ImGui::SameLine();
            std::string shown = UI(it->text);
            if (it->count > 1) shown += " (" + std::to_string(it->count) + "x)";
            ImGui::TextWrapped("%s", shown.c_str());
        }
        ImGui::EndPopup();
    }
}

}  // namespace kb
