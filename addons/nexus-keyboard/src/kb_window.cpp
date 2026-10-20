// kb_window.cpp — the main window: header, chat, preview, word bar, input, footer, minimise / restore.

#include "kb_shared.hpp"
#include "core/window_layout.hpp"
#include "imgui/imgui_internal.h"  // FindWindowByName, MovingWindow

namespace kb {

bool g_collapsed = false;  // minimised to the small button (its bottom-left corner is the window's)

// The window's place is one small formula (core/window_layout): four numbers – the bottom-left corner, the width, the
// big window's height – and every frame's rect follows from them and the mode. Nothing accumulates: the mini mode,
// minimising, growing for a long input and shrinking back cannot move it. Measured each frame for the next one: the
// fitted window's parts, and what the big window's content needs at least.
namespace {
WinAnchor g_win;
float g_winFittedH = 0, g_winNeedH = 0;
WinRect g_winWas;              // the rect the window had at the end of the last frame (what we set, or what it was)
WinMode g_winMode = WinMode::Big;
bool g_winHolding = false;     // the left mouse went down on the window: ImGui may move or resize it now
bool g_winOwnDrag = false;     // ... our own hold-and-drag moves it (the formula keeps placing it)
bool g_winSizing = false;      // its size changed while held: you drag an edge
WinAnchor g_winPressed;        // where it was when the mouse went down (a move snaps when it ends)
ULONGLONG g_ownDownAt = 0;     // our own hold-and-drag (the input, the mini window): when the press began – in this
bool g_ownMoving = false;      // shape only; a new mode ends it (a stale press once dragged the opened window along)

WinSpace WindowSpace(float button) {
    const ImGuiIO& io = ImGui::GetIO();
    WinSpace s;
    s.screenW = io.DisplaySize.x;
    s.screenBottom = std::max(button, io.DisplaySize.y - Gw2BottomBarOffset(io.DisplaySize.y, g_uiSize.load()));
    s.fittedH = g_winFittedH;
    s.needH = g_winNeedH;
    s.button = button;
    s.minSizeW = ImGui::GetStyle().WindowMinSize.x;  // what ImGui enlarges any window to
    s.minSizeH = ImGui::GetStyle().WindowMinSize.y;
    return s;
}

// Before Begin: what changed the window since the last frame (you, ImGui's ini, anyone) is taken over first; then the
// formula sets this frame's rect – not while ImGui moves or resizes it under your mouse (a new mode ends that hold).
void PlaceWindow(WinMode mode, const WinSpace& s) {
    ImGuiWindow* w = ImGui::FindWindowByName("GW2Chatlator###kbmain");
    if (w) {  // the rect it has is the last mode's
        const WinRect now{w->Pos.x, w->Pos.y, w->SizeFull.x, w->SizeFull.y};  // SizeFull: a resize since lands there
        if (g_winHolding && !g_winOwnDrag && std::fabs(now.h - g_winWas.h) > 0.5f) g_winSizing = true;
        g_win = AdoptRect(g_winMode, g_win, now, g_winWas);
        g_winWas = now;
    }
    if (mode != g_winMode) {  // a new mode ends every hold – ImGui's own move too: a press on the chat (a link that
        // minimises) had ImGui drag the button to where the big window was grabbed, the corner crept up each time
        g_winHolding = g_winOwnDrag = g_winSizing = false;
        g_ownDownAt = 0;
        g_ownMoving = false;
        ImGuiContext& g = *ImGui::GetCurrentContext();
        if (w && g.MovingWindow && g.MovingWindow->RootWindow == w) {
            g.MovingWindow = nullptr;
            ImGui::ClearActiveID();
        }
    }
    g_winMode = mode;
    if (!g_win.known || (g_winHolding && !g_winOwnDrag)) return;
    const WinRect r = LayoutWindow(mode, g_win, s);
    ImGui::SetNextWindowPos(ImVec2(r.x, r.y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(r.w, r.h), ImGuiCond_Always);
    g_winWas = r;
}

// After Begin: the mouse – a press on the window starts a hold, letting go after a move snaps the corner to an edge
// near it (the button also onto GW2's chat edge, `chatLeft`).
void TakeWindow(WinMode mode, const WinSpace& s, float chatLeft) {
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        g_winHolding = true;
        g_winPressed = g_win;
    } else if (g_winHolding && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        g_winHolding = g_winOwnDrag = g_winSizing = false;
        const bool moved = g_win.left != g_winPressed.left || g_win.bottom != g_winPressed.bottom;
        if (moved && g_nx.snap)
            g_win = SnapAnchor(mode, g_win, s, 14.0f * std::max(1.0f, ImGui::GetIO().DisplaySize.y / 1080.0f), chatLeft);
    }
}

// Our own hold-and-drag (the mini window anywhere, the big one in its input): the corner follows the mouse – unless
// ImGui already moves the window itself.
void DragWindow(const ImVec2& d) {
    if (d.x == 0 && d.y == 0) return;
    if (ImGui::GetCurrentContext()->MovingWindow == ImGui::GetCurrentWindow()) return;
    g_winOwnDrag = true;
    g_win.left = std::round(g_win.left + d.x);
    g_win.bottom = std::round(g_win.bottom + d.y);
}
}  // namespace

// Text blocks (menu ≡): your own text, saved from the input, a click puts one back into it (after what is there).
// In the main window, g_mu held (the window body holds it).
void RenderSnippets() {
    if (g_openSnippets) {  // at our window's top left, over the chat – not wherever the mouse happens to be
        g_openSnippets = false;
        ImGui::OpenPopup("kbsnippets");
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetWindowPos().x + 8, ImGui::GetWindowPos().y + 8), ImGuiCond_Always);
    }
    if (!ImGui::BeginPopup("kbsnippets")) return;
    ImGui::TextUnformatted(UI(Tr(L"Text blocks")).c_str());
    ImGui::Separator();
    if (!Trim(g_wFlatInput).empty() && ImGui::Selectable(UI(Tr(L"Save the input as a text block")).c_str())) {
        switch (g_snippets.Add(g_wFlatInput)) {
            case gct::Snippets::Added::Yes:
                SaveSnippets();
                SetStatus(Tr(L"Text block saved"), 3000);
                break;
            case gct::Snippets::Added::Twice: SetStatus(Tr(L"This text block is already saved"), 3000); break;
            case gct::Snippets::Added::Full:
                SetStatus(TrF(L"{1} text blocks – delete one first", {std::to_wstring(gct::Snippets::kMax)}), 5000);
                break;
            default: break;
        }
    }
    const auto& items = g_snippets.Items();
    if (items.empty()) ImGui::TextDisabled("%s", UI(Tr(L"No text blocks yet – write one in the input and save it here.")).c_str());
    int remove = -1;
    for (size_t i = 0; i < items.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton("x")) remove = static_cast<int>(i);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Delete")).c_str());
        ImGui::SameLine();
        const std::wstring& b = items[i];
        std::wstring shown = b.size() > 56 ? b.substr(0, 55) + L"…" : b;
        for (wchar_t& c : shown)
            if (c == L'\n') c = L' ';
        if (ImGui::Selectable(UI(shown).c_str())) {
            const size_t len = std::strlen(g_input);
            const std::string add = (len && g_input[len - 1] != ' ' && g_input[len - 1] != '\n' ? " " : "") + U8(b);
            if (len + add.size() >= sizeof(g_input)) {
                SetStatus(Tr(L"The text block does not fit into the input – send or shorten it first."), 5000);
            } else {
                g_edit.active = true;
                g_edit.startByte = len;
                g_edit.lenBytes = 0;
                g_edit.insert = add;
                g_focusInput = true;
            }
            ImGui::CloseCurrentPopup();
        }
        if (ImGui::IsItemHovered() && b.size() > 56) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
            ImGui::TextUnformatted(UI(b).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
        ImGui::PopID();
    }
    if (remove >= 0 && g_snippets.Remove(static_cast<size_t>(remove))) SaveSnippets();
    ImGui::EndPopup();
}

void MinimizeWindow() { g_collapsed = true; }  // the button takes the window's corner (g_mu held or main thread)

// The footer chips: channel | "Write: German" | "Send: English (UK) · MyMemory" (orange) | auto | counter.
void RenderFooter() {  // g_mu held
    static const Channel kChannels[] = {Channel::Unknown, Channel::Say,   Channel::Map,  Channel::Party,
                                        Channel::Squad,   Channel::Guild, Channel::Team, Channel::Whisper};
    auto chipWidth = [](const std::string& s) { return ImGui::CalcTextSize(s.c_str()).x + ImGui::GetFrameHeight() + 12; };
    const float iconH = ImGui::GetFrameHeight();

    // Native minimize button (bottom-left corner of the window)
    if (Lay().minButton) {
        if (ImGui::Button(">##minchat", ImVec2(iconH, iconH))) MinimizeWindow();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Minimize chat")).c_str());
        ImGui::SameLine();
    }

    // Footer chips cached: precalculated strings and sizes avoid allocations on idle frames.
    struct FooterCache {
        Channel channel = Channel::Unknown;
        std::wstring whisperTo;
        std::wstring writeIn;
        std::wstring sendLang;
        bool sendOrig = false;
        std::string labelFull, labelNarrow;
        std::string labelBtnFull, labelBtnNarrow;
        std::string writeFull, writeNarrow;
        std::string writeBtnFull, writeBtnNarrow;
        std::string sendFull, sendNarrow;
        std::string sendBtnFull, sendBtnNarrow;
        float labelWFull = 0, labelWNarrow = 0;
        float writeWFull = 0, writeWNarrow = 0;
        float sendWFull = 0, sendWNarrow = 0;
        float totalNeedFull = 0;
    };
    static FooterCache s_fc;
    const bool sendOrig = SendOriginal();
    const std::wstring sendLang = SendLang();
    if (s_fc.channel != g_channel || s_fc.whisperTo != g_whisperTo || s_fc.writeIn != g_cfg.writeIn ||
        s_fc.sendLang != sendLang || s_fc.sendOrig != sendOrig) {
        s_fc.channel = g_channel;
        s_fc.whisperTo = g_whisperTo;
        s_fc.writeIn = g_cfg.writeIn;
        s_fc.sendLang = sendLang;
        s_fc.sendOrig = sendOrig;

        s_fc.labelFull = g_channel == Channel::Unknown ? UI(Tr(L"Active channel")) : UI(ChannelLabel(g_channel));
        if (g_channel == Channel::Whisper && !g_whisperTo.empty()) s_fc.labelFull += " " + U8(g_whisperTo);
        s_fc.labelNarrow = g_channel == Channel::Unknown ? UI(Tr(L"Active")) : s_fc.labelFull;
        s_fc.labelBtnFull = s_fc.labelFull + "##chbtn";
        s_fc.labelBtnNarrow = s_fc.labelNarrow + "##chbtn";

        s_fc.writeFull = UI(TrF(L"Write: {1}", {g_cfg.writeIn.empty() ? Tr(L"like Windows") : NativeName(g_cfg.writeIn)}));
        s_fc.writeNarrow = g_cfg.writeIn.empty() ? std::string("Win") : U8(ToUpperAscii(PrimaryLang(g_cfg.writeIn)));
        s_fc.writeBtnFull = s_fc.writeFull + "##writebtn";
        s_fc.writeBtnNarrow = s_fc.writeNarrow + "##writebtn";

        const std::wstring sendText = sendOrig ? Tr(L"as written") : NativeName(sendLang);
        s_fc.sendFull = UI(TrF(L"Send: {1}", {sendText}));
        s_fc.sendNarrow = "> " + (sendOrig ? UI(Tr(L"as written")) : U8(ToUpperAscii(PrimaryLang(sendLang))));
        s_fc.sendBtnFull = s_fc.sendFull + "##sendbtn";
        s_fc.sendBtnNarrow = s_fc.sendNarrow + "##sendbtn";

        s_fc.labelWFull = chipWidth(s_fc.labelFull);
        s_fc.labelWNarrow = chipWidth(s_fc.labelNarrow);
        s_fc.writeWFull = chipWidth(s_fc.writeFull);
        s_fc.writeWNarrow = chipWidth(s_fc.writeNarrow);
        s_fc.sendWFull = chipWidth(s_fc.sendFull);
        s_fc.sendWNarrow = chipWidth(s_fc.sendNarrow);
        s_fc.totalNeedFull = iconH + s_fc.labelWFull + s_fc.writeWFull + s_fc.sendWFull +
                             ImGui::GetFrameHeight() * 4 + ImGui::CalcTextSize("000/199").x;
    }

    const bool narrow = s_fc.totalNeedFull > ImGui::GetContentRegionAvail().x;
    const std::string& chBtnLabel = narrow ? s_fc.labelBtnNarrow : s_fc.labelBtnFull;
    const float chBtnW = narrow ? s_fc.labelWNarrow : s_fc.labelWFull;

    const ImVec2 chBtnPos = ImGui::GetCursorScreenPos();
    ImGui::PushStyleColor(ImGuiCol_Text, NameColorOf(g_channel == Channel::Unknown ? Channel::Say : g_channel));
    const bool chLeft = ImGui::Button(chBtnLabel.c_str(), ImVec2(chBtnW, iconH));
    ImGui::PopStyleColor();
    const bool chRight = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);

    if (chLeft || chRight) {
        ImGui::OpenPopup("##channel_popup");
    }

    // Dropdown menu opens UPWARDS directly above the button, overlaying the window upwards!
    ImGui::SetNextWindowPos(ImVec2(chBtnPos.x, chBtnPos.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(chBtnW, 0.0f), ImVec2(FLT_MAX, ImGui::GetFontSize() * 16.0f));
    if (ImGui::BeginPopup("##channel_popup")) {
        for (Channel c : kChannels) {
            const std::string name = c == Channel::Unknown ? UI(Tr(L"Active channel")) : UI(ChannelLabel(c));
            ImGui::PushStyleColor(ImGuiCol_Text, NameColorOf(c == Channel::Unknown ? Channel::Say : c));
            if (ImGui::Selectable(name.c_str(), c == g_channel)) {
                if (c != Channel::Whisper && g_activeTab >= 0) LeaveWhisperTab();  // the whisper tab is left
                g_channel = c;
                if (c != Channel::Whisper) g_whisperTo.clear();
                g_focusInput = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndPopup();
    }
    // The language you write in (spelling, learned words and the prediction follow it).
    ImGui::SameLine();
    const ImVec2 writeBtnPos = ImGui::GetCursorScreenPos();
    const float writeBtnW = narrow ? s_fc.writeWNarrow : s_fc.writeWFull;
    const std::string& writeBtnLabel = narrow ? s_fc.writeBtnNarrow : s_fc.writeBtnFull;
    const bool writeLeft = ImGui::Button(writeBtnLabel.c_str(), ImVec2(writeBtnW, iconH));
    const bool writeRight = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);
    if (writeLeft || writeRight) {
        ImGui::OpenPopup("##write_popup");
    }
    ImGui::SetNextWindowPos(ImVec2(writeBtnPos.x, writeBtnPos.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(writeBtnW, 0.0f), ImVec2(FLT_MAX, ImGui::GetFontSize() * 20.0f));
    if (ImGui::BeginPopup("##write_popup")) {
        std::wstring chosen = g_cfg.writeIn;
        if (ImGui::Selectable(UI(Tr(L"like Windows")).c_str(), chosen.empty())) {
            chosen.clear();
            ImGui::CloseCurrentPopup();
        }
        for (const LangInfo& l : Languages()) {
            if (l.latinScript && ImGui::Selectable(U8(l.native).c_str(), g_cfg.writeIn == l.code)) {
                chosen = l.code;
                ImGui::CloseCurrentPopup();
            }
        }
        if (chosen != g_cfg.writeIn) {
            g_cfg.writeIn = chosen;
            g_cfg.SaveValue(L"Spelling", L"WriteIn", chosen);
            g_worker.Post([] {  // spelling and learned words follow the language you write in
                FreeWorkerState();
                InitSpelling();
            });
            ++g_inputGen;
            g_inputChanged = GetTickCount64();
            if (g_nx.translateChat) TranslateRecentLines(false);  // the chat is read in the language you write in
        }
        ImGui::EndPopup();
    }

    // What the chat gets (orange); who translates is told by the status line.
    ImGui::SameLine();
    const ImVec2 sendBtnPos = ImGui::GetCursorScreenPos();
    const float sendBtnW = narrow ? s_fc.sendWNarrow : s_fc.sendWFull;
    const std::string& sendBtnLabel = narrow ? s_fc.sendBtnNarrow : s_fc.sendBtnFull;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.94f, 0.75f, 0.35f, 1.f));
    const bool sendLeft = ImGui::Button(sendBtnLabel.c_str(), ImVec2(sendBtnW, iconH));
    ImGui::PopStyleColor();
    const bool sendRight = ImGui::IsItemHovered() && ImGui::IsMouseClicked(1);
    if (sendLeft || sendRight) {
        ImGui::OpenPopup("##send_popup");
    }
    ImGui::SetNextWindowPos(ImVec2(sendBtnPos.x, sendBtnPos.y - 4.0f), ImGuiCond_Always, ImVec2(0.0f, 1.0f));
    ImGui::SetNextWindowSizeConstraints(ImVec2(sendBtnW, 0.0f), ImVec2(FLT_MAX, ImGui::GetFontSize() * 20.0f));
    if (ImGui::BeginPopup("##send_popup")) {
        for (size_t i = 0; i <= g_cfg.writeLangs.size(); ++i) {
            const std::string n = i == g_cfg.writeLangs.size() ? UI(Tr(L"as written")) : U8(NativeName(g_cfg.writeLangs[i]));
            const bool current = g_sendOverride.empty() ? static_cast<int>(i) == g_sendIdx
                                 : i == g_cfg.writeLangs.size() ? g_sendOverride == L"="
                                                                : g_sendOverride == g_cfg.writeLangs[i];
            if (ImGui::Selectable(n.c_str(), current)) {
                if (g_activeTab >= 0 && g_activeTab < static_cast<int>(g_tabs.size())) {  // this player's tab only
                    g_tabs[g_activeTab].sendLang = i == g_cfg.writeLangs.size() ? std::wstring(L"=") : g_cfg.writeLangs[i];
                    g_sendOverride = g_tabs[g_activeTab].sendLang;
                } else {
                    g_sendIdx = static_cast<int>(i);
                }
                ++g_inputGen;
                g_preview.clear();
                g_inputChanged = GetTickCount64();
                g_focusInput = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::Separator();
        for (const LangInfo& l : Languages()) {  // more languages: the chosen one becomes the first
            if (!l.latinScript || std::find(g_cfg.writeLangs.begin(), g_cfg.writeLangs.end(), l.code) != g_cfg.writeLangs.end())
                continue;
            if (ImGui::Selectable(U8(l.native).c_str(), false)) {
                g_cfg.writeLangs.insert(g_cfg.writeLangs.begin(), l.code);
                g_cfg.SaveAll();
                if (g_activeTab >= 0 && g_activeTab < static_cast<int>(g_tabs.size())) {  // this player's tab only
                    g_tabs[g_activeTab].sendLang = l.code;
                    g_sendOverride = l.code;
                } else {
                    g_sendIdx = 0;
                }
                ++g_inputGen;
                g_preview.clear();
                g_inputChanged = GetTickCount64();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    if (g_cfg.voiceEnabled) {  // send right after speaking (off by default)
        ImGui::SameLine();
        if (ImGui::Checkbox(UI(Tr(L"auto") + L"##autosend").c_str(), &g_cfg.voiceAutoSend))
            g_cfg.SaveBool(L"Voice", L"AutoSend", g_cfg.voiceAutoSend);
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26);
            ImGui::TextUnformatted(UI(Tr(L"auto: send right after speaking")).c_str());
            ImGui::TextDisabled("%s", UI(Tr(L"When you stop talking (key released, or the pause after speaking), the recognized "
                                            L"text goes into the GW2 chat at once – translated if a send language is "
                                            L"chosen. Off: it waits in the box until you press Enter.")).c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
    const size_t n = CodePointCount(g_wFlatInput);
    std::string counter = std::to_string(n) + "/" + std::to_string(g_cfg.maxLength);
    if (n > static_cast<size_t>(g_cfg.maxLength)) {  // sent as several chat lines
        const bool current = g_previewGen == g_inputGen && !g_preview.empty();
        const size_t parts = SplitForChat(Translating() && current ? g_preview : g_wFlatInput, ComposePrefix(),
                                          static_cast<size_t>(g_cfg.maxLength)).size();
        counter += "  (" + std::to_string(parts) + "x)";
    }
    // At the right edge, never over the chips: without room it goes into the next line.
    const float counterX = ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(counter.c_str()).x;
    const float after = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetStyle().ItemSpacing.x * 2;
    if (counterX >= after) ImGui::SameLine(counterX);
    else ImGui::SetCursorPosX(std::max(counterX, ImGui::GetCursorPosX()));
    if (!g_layout.infoText && !g_input[0]) return;  // info texts off: the counter only while you write
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("%s", counter.c_str());
}

// The status text (drawn in the input box): a message, else what is going on.
std::wstring StatusText() {  // g_mu held
    if (!g_status.empty()) return g_status;
    if (g_recording) return Tr(L"Hearing you … release the key when you are done");
    if (!g_listenPausedFor.empty()) return TrF(L"Voice activation paused – {1} uses the microphone", {g_listenPausedFor});
    if (g_listening) return Tr(L"Voice activation listens – just speak");
    const bool voiceOn = g_cfg.voiceEnabled && g_cfg.voiceTranslate;
    if (!g_nx.translateChat && !Translating()) {  // nothing goes to a translator: no counter of one
        if (!g_cfg.voiceEnabled) return Tr(L"Prediction only");  // the typing help is all that is on
        return voiceOn ? TrF(L"Translation off · what you say: into {1}", {ToUpperAscii(PrimaryLang(VoiceLang()))})
                       : Tr(L"Translation off");
    }
    if (const std::wstring q = MyMemoryQuotaText(); !q.empty()) return q;
    std::wstring route = SendOriginal() ? Tr(L"as written") : ToUpperAscii(PrimaryLang(SendLang()));
    if (Translating() && g_translator) route += L" · " + g_translator->Name();
    return TrF(L"Send: {1}", {route});
}

void RenderWindow() {
    if (!ImGui::GetCurrentContext()) return;  // failsafe: no context, no drawing
    CostFrame costFrame;  // what this frame costs on GW2's render thread, by part (Options > Technical)
    WaypointFrameTick();  // the waypoint click asked for one frame of the game (kb_waypoint.cpp)
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (!g_input[0] && !g_edit.active) g_inputSpoken = false;  // empty again (sent, deleted): the next one decides
    }
    KickSave();
    VoiceIdleUnload();
    ImGuiIO& io = ImGui::GetIO();
    if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0) return;
    struct PopColoursAtEnd {
        int n = 0;
        ~PopColoursAtEnd() { if (n) ImGui::PopStyleColor(n); }
    } popColours;
    popColours.n = PushTheme();  // for everything drawn below: the options, our window, its popups
    g_keyboardFree = !io.WantCaptureKeyboard;
    g_fps = static_cast<int>(io.Framerate);
    {
        // MumbleLink (every 250 ms): shown only on a map – never at character select or on a loading screen (like the
        // exe waiting for GW2); your character's name; the GW2 chat line opening (focus behaviour).
        static ULONGLONG next = 0;
        static MumbleLink mumble;
        static bool chatWasOpen = false;
        if (GetTickCount64() > next) {
            next = GetTickCount64() + 250;
            const MumbleState ms = mumble.Read();
            std::lock_guard<std::mutex> lock(g_mu);
            g_inMap = ms.live && ms.inMap;
            g_mapOpen = ms.live && ms.MapOpen();
            if (ms.live) {
                g_ownName = ms.identity.name;
                g_uiSize.store(ms.identity.uiSize);
                g_mapId.store(ms.identity.mapId);
            }
            const bool chatOpen = ms.live && ms.TextboxHasFocus();
            g_gameChatOpen = chatOpen;
            const bool chatOpened = chatOpen && !chatWasOpen && g_show && GetTickCount64() > g_suppressChatFocusUntil &&
                                    g_redirect == 0;
            if (chatOpened && g_cfg.focusOnGameChat) g_focusInput = true;   // the focus comes over (opens it as well)
            else if (chatOpened && g_layout.autoOpen && g_collapsed) g_expandRequest = true;  // only opened
            static bool s_redirectChatSeen = false;  // GW2's chat line was open during this redirect
            if (g_redirect == 0) s_redirectChatSeen = false;
            if (g_redirect != 0 && chatOpen) s_redirectChatSeen = true;
            if (g_redirect == 1 && ms.MapOpen()) g_redirect = 2;       // a waypoint: the map went open …
            else if (g_redirect == 3 && chatOpen) g_redirect = 4;       // anything else: GW2's chat line is open …
            const bool done = (g_redirect == 2 && !ms.MapOpen()) || (g_redirect == 4 && !chatOpen && !ms.MapOpen()) ||
                              (g_redirect == 1 && s_redirectChatSeen && !chatOpen && !ms.MapOpen());  // closed, no map
            if (done || (g_redirect != 0 && GetTickCount64() - g_redirectSince > 45000)) {  // … and closed again
                g_redirect = 0;
                g_expandRequest = true;
            }
            chatWasOpen = chatOpen;
        }
    }
    if (g_wipeDone != 0) {  // everything removed: only the notice with the last step
        RenderRemovedNotice();
        return;
    }
    ChatEventsPresent();
    CostMark(kCostOptions);
    RenderOptionsWindow();
    CostMark(kCostWindow);
    bool inMap;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        inMap = (g_inMap || g_test.forceMap) && (!g_mapOpen || GetTickCount64() < g_showOnMapUntil);
        if (g_focusInput && g_collapsed) g_collapsed = false;  // something wants the input: the window opens (at its corner)
    }
    if (!g_show || !inMap) return;

    const float iconH = ImGui::GetFrameHeight();
    const float minBtn = iconH + 8.0f;
    if (g_collapsed) {
        g_lastUse = 0;  // opened again: "not in use" counts from then
        const WinSpace space = WindowSpace(minBtn);
        PlaceWindow(WinMode::Button, space);
        // Its size exactly as the formula has it – whole pixels: ImGui floors a window's size, and the raw button size
        // (Nexus' font: not whole pixels) let the formula see a pixel less each frame and move the button up by it.
        const WinRect btn = LayoutWindow(WinMode::Button, g_win, space);
        ImGui::SetNextWindowSize(ImVec2(btn.w, btn.h), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(std::clamp(g_cfg.opacity, 60, 255) / 255.0f);
        ImGuiWindowFlags minFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoScrollWithMouse;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(2, 2));
        if (WaypointPassThrough()) minFlags |= ImGuiWindowFlags_NoInputs;  // the Waypoint/POI click: the mouse goes through
        const bool beginOk = ImGui::Begin("GW2Chatlator###kbmain", nullptr, minFlags);
        ImGui::PopStyleVar();
        if (beginOk) {
            const float resScale = std::max(1.0f, io.DisplaySize.y / 1080.0f);
            const float uiScale = (g_uiSize.load() == 0 ? 0.89f : g_uiSize.load() == 2 ? 1.11f : g_uiSize.load() == 3 ? 1.25f : 1.0f);
            TakeWindow(WinMode::Button, space, std::round(10.0f * uiScale * resScale));  // also onto GW2's chat edge
            const bool clickedOpen = ImGui::Button("<##expand", ImVec2(iconH, iconH));
            if (clickedOpen || g_expandRequest) {  // the window opens at its corner, upwards
                std::lock_guard<std::mutex> lock(g_mu);
                g_expandRequest = false;
                g_redirect = 0;
                g_collapsed = false;
                g_suppressChatFocusUntil = 0;
                if (clickedOpen) g_focusInput = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Open the window")).c_str());
            if (g_autoSendPending && !g_edit.active && g_input[0]) {
                g_autoSendPending = false;
                StartSend();
            }
            ImGui::End();
            return;
        }
        ImGui::End();
        return;
    }

    // Without the chat the window is exactly its parts (fitted below, down to nothing but the input).
    const WinMode winMode = Lay().chat ? WinMode::Big : WinMode::Fitted;
    const WinSpace space = WindowSpace(minBtn);
    ImGui::SetNextWindowSize(ImVec2(520, 360), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(space.minW, Lay().chat ? space.minH : 16.0f), ImVec2(FLT_MAX, FLT_MAX));
    PlaceWindow(winMode, space);
    ImGui::SetNextWindowBgAlpha(std::clamp(g_cfg.opacity, 0, 255) / 255.0f);  // up to fully see-through (the small button keeps 60)
    ImGuiWindowFlags wflags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                              ImGuiWindowFlags_NoScrollWithMouse;
    // Without the chat: a thin edge only, the window is hardly more than its parts (the grey input in the end).
    if (!Lay().chat) ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
    if (WaypointPassThrough()) wflags |= ImGuiWindowFlags_NoInputs;  // the Waypoint/POI click: the mouse goes through
    const bool begun = ImGui::Begin("GW2Chatlator###kbmain", nullptr, wflags);
    if (!Lay().chat) ImGui::PopStyleVar();
    if (!begun) {
        ImGui::End();
        return;
    }
    TakeWindow(winMode, space, 0.0f);
    if (g_minimizeRequest) {  // asked from inside the chat area (a waypoint click): done on the main window
        g_minimizeRequest = false;
        g_expandRequest = false;
        MinimizeWindow();
        ImGui::End();
        return;
    }
    if (g_expandRequest) g_expandRequest = false;  // already open
    struct PopFontAtEnd {
        bool on = false;
        ~PopFontAtEnd() { if (on) ImGui::PopFont(); }
    } popFont;
    {
        const float px = PtToPx(g_cfg.fontPt);
        ImFont* tf = g_textFont.load();
        if (tf && std::abs(tf->FontSize - px) < 0.5f) {  // sharp: the font built at this size
            ImGui::SetWindowFontScale(1.0f);
            ImGui::PushFont(tf);
            popFont.on = true;
        } else {  // not (yet) there: Nexus' font scaled
            ImGui::SetWindowFontScale(px / std::max(1.0f, ImGui::GetFont()->FontSize));
        }
    }
    std::unique_lock<std::mutex> lock(g_mu);
    if (g_statusUntil && GetTickCount64() > g_statusUntil) {
        g_status.clear();
        g_statusUntil = 0;
    }
    const ImVec4 kAccent(0.94f, 0.75f, 0.35f, 1.f), kPreview(0.59f, 0.80f, 1.f, 1.f);
    const ImGuiStyle& st = ImGui::GetStyle();

    CostMark(kCostHeader);
    // ---- header: the tabs (the chat, whisper tabs; right-click: a new one) · microphone · V / A · menu · close
    const float icon = ImGui::GetFrameHeight();
    const float gap = std::round(icon * 0.6f);
    const float rightW = (g_cfg.voiceEnabled ? icon + gap : 0) + icon + gap + icon + st.ItemSpacing.x + icon;
    const float tabsW = std::max(60.0f, ImGui::GetContentRegionAvail().x - rightW - gap);
    bool newTab = false, tabMenu = false;
    static bool s_lineRightClick = false;  // a right-click on a chat line (it translates the line, no menu)
    if (Lay().header) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##hdr", ImVec2(tabsW, icon), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
        ImGui::PopStyleVar();
        if (ImGui::BeginTabBar("##wtabs", ImGuiTabBarFlags_FittingPolicyResizeDown | ImGuiTabBarFlags_NoTooltip)) {
            static std::string s_chatTabLabel;
            static UiLang s_chatTabLang = static_cast<UiLang>(-1);
            if (s_chatTabLang != g_cfg.uiLang || s_chatTabLabel.empty()) {
                s_chatTabLang = g_cfg.uiLang;
                s_chatTabLabel = UI(Tr(L"Chat")) + "###wt_all";
            }
            if (ImGui::BeginTabItem(s_chatTabLabel.c_str(), nullptr,
                                    g_selectTab == -1 ? ImGuiTabItemFlags_SetSelected : 0)) {
                if (g_selectTab == -1) g_selectTab = -2;
                else if (g_selectTab == -2 && g_activeTab != -1) LeaveWhisperTab();
                ImGui::EndTabItem();
            }
            int close = -1;
            for (int i = 0; i < static_cast<int>(g_tabs.size()); ++i) {
                bool open = true;
                const ImGuiTabItemFlags f = (g_selectTab == i ? ImGuiTabItemFlags_SetSelected : 0) |
                                            (g_tabs[i].unread ? ImGuiTabItemFlags_UnsavedDocument : 0);
                const bool shown = ImGui::BeginTabItem(g_tabs[i].Label(), &open, f);
                if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    tabMenu = true;  // not the header's menu
                    if (g_tabs[i].custom) g_tabSettings = static_cast<int>(i);
                }
                if (shown) {
                    if (g_selectTab == i) g_selectTab = -2;
                    else if (g_selectTab == -2 && g_activeTab != i) ActivateTab(i);
                    ImGui::EndTabItem();
                }
                if (!open) close = i;
            }
            ImGui::EndTabBar();
            if (close >= 0) {
                const bool active = close == g_activeTab, custom = g_tabs[close].custom;
                g_tabs.erase(g_tabs.begin() + close);
                if (active) LeaveWhisperTab();
                else if (close < g_activeTab) g_selectTab = --g_activeTab;
                if (custom) SaveCustomTabs();
            }
        }
        if (!tabMenu && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
            newTab = true;
        ImGui::EndChild();
    }
    // Right-click on the header: a tab of your own (a copy of the chat to set up) or a whisper tab.
    if (newTab) ImGui::OpenPopup("##hdrmenu");
    static bool newTabNext = false;  // asked for in a menu (opened next frame, at this level)
    if (ImGui::BeginPopup("##hdrmenu")) {
        if (ImGui::MenuItem(UI(Tr(L"New tab")).c_str())) {
            const int at = NewCustomTab();
            if (at >= 0) g_tabSettings = at;
            else SetStatus(Tr(L"Six tabs of your own at most – close one first"), 6000);
        }
        if (ImGui::MenuItem(UI(Tr(L"New whisper tab …")).c_str())) newTabNext = true;
        ImGui::EndPopup();
    }
    if (newTabNext) ImGui::OpenPopup("##newtab");
    newTabNext = false;
    // The settings of a tab of your own (right-click on it): name, the channels it shows, where you write from it.
    if (g_tabSettings >= 0) {
        g_tabSettingsOpen = g_tabSettings;
        g_tabSettings = -1;
        ImGui::OpenPopup("##tabcfg");
    }
    if (ImGui::BeginPopup("##tabcfg")) {
        const int i = g_tabSettingsOpen;
        if (i < 0 || i >= static_cast<int>(g_tabs.size()) || !g_tabs[i].custom) {
            ImGui::CloseCurrentPopup();
        } else {
            WhisperTab& t = g_tabs[i];
            static char name[64] = {};
            if (ImGui::IsWindowAppearing()) strncpy_s(name, U8(t.name).c_str(), _TRUNCATE);
            bool changed = false;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
            if (ImGui::InputText(UI(Tr(L"Name") + L"##tabname").c_str(), name, sizeof(name)) && !Trim(W(name)).empty()) {
                t.SetName(Trim(W(name)));
                changed = true;
            }
            ImGui::TextUnformatted(UI(Tr(L"Shows")).c_str());
            static const Channel kShown[] = {Channel::Say,   Channel::Map,     Channel::Party, Channel::Squad,
                                             Channel::Team,  Channel::Guild,   Channel::Whisper, Channel::System};
            for (int k = 0; k < 8; ++k) {
                bool on = (t.channels & ChannelBit(kShown[k])) != 0;
                if (k % 4) ImGui::SameLine(static_cast<float>(k % 4) * ImGui::GetFontSize() * 6.5f);
                if (ImGui::Checkbox((UI(ChannelLabel(kShown[k])) + "##ch" + std::to_string(k)).c_str(), &on)) {
                    t.channels = on ? (t.channels | ChannelBit(kShown[k])) : (t.channels & ~ChannelBit(kShown[k]));
                    if (kShown[k] == Channel::System)  // lines without a channel go with the system lines
                        t.channels = on ? (t.channels | ChannelBit(Channel::Unknown)) : (t.channels & ~ChannelBit(Channel::Unknown));
                    changed = true;
                }
            }
            static const Channel kWrite[] = {Channel::Unknown, Channel::Say, Channel::Map,  Channel::Party,
                                             Channel::Squad,   Channel::Team, Channel::Guild};
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 12);
            if (ImGui::BeginCombo(UI(Tr(L"Write in") + L"##tabwrite").c_str(),
                                  t.write == Channel::Unknown ? UI(Tr(L"Active channel")).c_str() : UI(ChannelLabel(t.write)).c_str())) {
                for (Channel c : kWrite)
                    if (ImGui::Selectable(c == Channel::Unknown ? UI(Tr(L"Active channel")).c_str() : UI(ChannelLabel(c)).c_str(),
                                          c == t.write)) {
                        t.write = c;
                        changed = true;
                        if (g_activeTab == i) g_channel = c;
                    }
                ImGui::EndCombo();
            }
            if (ImGui::Button(UI(Tr(L"Remove tab")).c_str())) {
                g_tabs.erase(g_tabs.begin() + i);
                if (g_activeTab == i) LeaveWhisperTab();
                else if (i < g_activeTab) g_selectTab = --g_activeTab;
                changed = true;
                ImGui::CloseCurrentPopup();
            }
            if (changed) SaveCustomTabs();
        }
        ImGui::EndPopup();
    }
    if (ImGui::BeginPopup("##newtab")) {  // a whisper tab of your own: whom you want to write to
        static char name[96] = {};
        if (ImGui::IsWindowAppearing()) {
            name[0] = 0;
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::TextUnformatted(UI(Tr(L"New whisper tab")).c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        bool go = ImGui::InputTextWithHint("##tabname", UI(Tr(L"Character or account name")).c_str(), name, sizeof(name),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        go |= ImGui::Button(UI(Tr(L"Open")).c_str());
        if (go && !Trim(W(name)).empty()) {
            OpenWhisperTab(W(name));
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (Lay().header) {
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - rightW);
        if (g_cfg.voiceEnabled) {
            // Hold with the left button to speak; a right-click: on until a pause (or the next right-click).
            bool right = false;
            const ImU32 tint = g_listening ? IM_COL32(110, 205, 130, 255) : !g_listenPausedFor.empty() ? IM_COL32(240, 190, 90, 255) : 0;
            const bool clicked = IconButton("##mic", Icon::Mic, g_recording, &right, tint);
            static bool held = false;
            if (g_cfg.voiceActivation) {  // voice activation: a click switches listening on / off
                if (clicked) {
                    const bool on = g_listening || g_listenWanted;
                    g_listenUserOff = on;
                    lock.unlock();
                    if (on) StopListening();
                    else StartListening();
                    lock.lock();
                }
            } else if (ImGui::IsItemActivated()) {
                held = true;
                lock.unlock();
                TalkDown();
                lock.lock();
            } else if (held && !ImGui::IsItemActive()) {
                held = false;
                if (!g_micLocked && !g_cfg.voiceToggle) TalkUp();
            }
            if (right) {
                const bool stop = g_recording;
                g_micLocked = !stop;
                lock.unlock();
                if (stop) TalkUp();
                else TalkLocked();
                lock.lock();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", UI(g_cfg.voiceActivation ? Tr(L"Voice activation: click switches it on / off · right-click: speak until a pause")
                                                             : Tr(L"Hold to speak (or the push-to-talk key) · right-click: speak until a pause")).c_str());
            ImGui::SameLine(0, gap);
        }
        if (IconButton("##fold", Icon::Collapse, false, nullptr)) {
            MinimizeWindow();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Minimize chat")).c_str());
        ImGui::SameLine(0, gap);
        if (IconButton("##menu", Icon::Menu, false, nullptr)) ImGui::OpenPopup("kbmenu");
    }
    if (ImGui::BeginPopup("kbmenu")) {
        if (!Lay().header) {  // what the header offers: the tabs, minimise
            if (ImGui::MenuItem(UI(Tr(L"Chat")).c_str(), nullptr, g_activeTab < 0)) LeaveWhisperTab();
            for (int i = 0; i < static_cast<int>(g_tabs.size()); ++i)
                if (ImGui::MenuItem(g_tabs[i].Label(), nullptr, g_activeTab == i)) {
                    ActivateTab(i);
                    g_selectTab = i;  // the tab bar follows when the header comes back
                }
            if (ImGui::MenuItem(UI(Tr(L"Minimize chat")).c_str())) g_minimizeRequest = true;
            ImGui::Separator();
        }
        if (ImGui::MenuItem(UI(Tr(L"Options …")).c_str())) g_showOptions = true;
        if (const bool mini = ProfileOn(1); ImGui::MenuItem(UI(Tr(L"Mini mode")).c_str(), nullptr, mini)) SetProfile(1, !mini);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", UI(Tr(L"Nothing but the input with its help. Three quick clicks on it minimise it; right-click in it: Menu … brings the rest back.")).c_str());
        if (const bool compact = ProfileOn(0); ImGui::MenuItem(UI(Tr(L"Compact view")).c_str(), nullptr, compact))
            SetProfile(0, !compact);
        if (ImGui::MenuItem(UI(Tr(L"Translate the chat once")).c_str()) && TranslateRecentLines(true) == 0)
            SetStatus(Tr(L"No chat lines of other players in view"), 4000);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", UI(Tr(L"What you see of the chat, into the language you write in – once, whatever the chat settings say. Again after you changed the language: in the new one.")).c_str());
        if (ImGui::MenuItem(UI(Tr(L"New whisper tab …")).c_str())) newTabNext = true;
        if (ImGui::MenuItem(UI(Tr(L"Text blocks …")).c_str())) g_openSnippets = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", UI(Tr(L"Your own text, saved from the input – a click puts it back.")).c_str());
        if (!g_chatEventsPresent && !g_chatEventsFile && ImGui::MenuItem(UI(Tr(L"Install Events: Chat (every channel) …")).c_str()))
            g_showOptions = true;
        ImGui::Separator();
        if (ImGui::MenuItem(UI(Tr(L"Hide (Ctrl+Alt+K shows it again)")).c_str())) g_show = false;
        ImGui::EndPopup();
    }
    RenderSnippets();
    if (Lay().header) {
        ImGui::SameLine();
        if (IconButton("##close", Icon::Close, false, nullptr)) g_show = false;
    }
    if (!g_recording && g_micLocked) g_micLocked = false;  // the recording ended (pause, maximum length)
    // Voice activation listens from the map on (unless you switched it off with the microphone).
    if (g_cfg.voiceEnabled && g_cfg.voiceActivation && !g_listenWanted && !g_listenUserOff) {
        lock.unlock();
        StartListening();
        lock.lock();
    }
    if (g_collapsed) {  // folded up: only the header; what was going on goes on (sending, dictation)
        if (g_autoSendPending && !g_edit.active && g_input[0]) {
            g_autoSendPending = false;
            StartSend();
        }
        lock.unlock();
        ImGui::End();
        return;
    }

    CostMark(kCostChat);
    // ---- the chat (the newest at the bottom): name white – a click opens a whisper tab –, text in the channel colour,
    // the scroll bar at the left like GW2's own chat
    const float line = ImGui::GetTextLineHeightWithSpacing();
    const bool showPreview = Lay().preview, showWords = Lay().words;
    const float smallLine = ImGui::GetFontSize() * 0.85f + 2;
    // The input grows with what you write (its soft-wrapped lines) up to 8 lines and falls back once it is sent. A small
    // extra line at its bottom: the translation in compact view (only while something is translated).
    const int textLines = 1 + static_cast<int>(std::count(g_input, g_input + std::strlen(g_input), '\n'));
    g_boxLines = std::clamp(textLines, Lay().inputLines, std::max(8, Lay().inputLines));
    const float boxH = ImGui::GetTextLineHeight() * g_boxLines + st.FramePadding.y * 2 +
                       (!showPreview && Translating() ? smallLine : 0);  // no preview: the translation small in the box
    const float lowerH = (showPreview ? line * (g_layout.previewLines + 0.2f) + st.ItemSpacing.y : 0) +
                         (showWords ? ImGui::GetFrameHeightWithSpacing() : 0) +
                         (Lay().footer ? ImGui::GetFrameHeightWithSpacing() : 0) + boxH + st.ItemSpacing.y;
    if (!Lay().chat) {  // no chat: the window is exactly as high as its parts – the layout keeps its bottom where it is
        // + the thin bottom edge (above); never below ImGui's own minimum (the layout rounds to whole pixels).
        const float want = std::ceil(std::max(ImGui::GetCursorPosY() + lowerH - st.ItemSpacing.y + 4.0f, st.WindowMinSize.y));
        g_winFittedH = want;
        const ImVec2 p = ImGui::GetWindowPos(), sz = ImGui::GetWindowSize();
        // You drag its top (or bottom) edge: the height becomes whole lines of the input – "Lines of the input" in the
        // options follows at once; while the mouse is down the window keeps your size, then it fits to the lines.
        if (g_winSizing) {
            const float lh = ImGui::GetTextLineHeight();
            const int lines = std::clamp(static_cast<int>(std::lround((sz.y - (want - lh * g_boxLines)) / lh)), 1, 8);
            if (lines != g_layout.inputLines) {
                g_layout.inputLines = lines;
                SaveLayout();
            }
        }
        // Held down for a moment, then dragged: the whole window moves (not only at its edges) – a quick drag still
        // selects text. Not when the press was on an edge (that resizes).
        const ImVec2 mp = ImGui::GetIO().MousePos;
        const float edge = 6.0f;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            mp.y > p.y + edge && mp.y < p.y + sz.y - edge && mp.x > p.x + edge && mp.x < p.x + sz.x - edge)
            g_ownDownAt = GetTickCount64();
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            g_ownDownAt = 0;
            g_ownMoving = false;
        } else if (g_ownDownAt && !g_ownMoving && !g_winSizing && GetTickCount64() - g_ownDownAt > 250) {
            g_ownMoving = true;
        }
        if (g_ownMoving) {
            DragWindow(ImGui::GetIO().MouseDelta);
        }
    } else {  // the big window: what its content needs at least – a long input grows it upwards, it shrinks back by itself
        g_winNeedH = ImGui::GetWindowSize().y + lowerH + line * 2 - ImGui::GetContentRegionAvail().y;
    }
    if (Lay().chat) {
        static float scrollY = 0, scrollMax = 0, viewH = 1, dragOffset = 0, lastScrollMax = 0;
        static bool atBottom = true;
        static int lastActiveTab = -999;
        const float h = std::max(line * 2, ImGui::GetContentRegionAvail().y - lowerH);
        const float sb = std::max(8.0f, st.ScrollbarSize * 0.8f);
        const ImVec2 track = ImGui::GetCursorScreenPos();
        float setScroll = -1;
        ImGui::InvisibleButton("##chatsb", ImVec2(sb, h));
        {
            const float grabH = scrollMax > 0 ? std::max(sb * 2, h * viewH / (viewH + scrollMax)) : h;
            const float grabY = track.y + (scrollMax > 0 ? (h - grabH) * scrollY / scrollMax : 0);
            if (ImGui::IsItemActivated()) {
                const float my = io.MousePos.y;
                dragOffset = my >= grabY && my <= grabY + grabH ? my - grabY : grabH * 0.5f;
            }
            if (ImGui::IsItemActive() && scrollMax > 0)
                setScroll = std::clamp((io.MousePos.y - dragOffset - track.y) / std::max(1.0f, h - grabH), 0.0f, 1.0f) * scrollMax;
            if (scrollMax > 0) {
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(track, ImVec2(track.x + sb, track.y + h), ImGui::GetColorU32(ImGuiCol_ScrollbarBg), sb * 0.5f);
                const ImGuiCol gc = ImGui::IsItemActive() ? ImGuiCol_ScrollbarGrabActive
                                    : ImGui::IsItemHovered() ? ImGuiCol_ScrollbarGrabHovered : ImGuiCol_ScrollbarGrab;
                dl->AddRectFilled(ImVec2(track.x + 1, grabY), ImVec2(track.x + sb - 1, grabY + grabH), ImGui::GetColorU32(gc), sb * 0.5f);
            }
        }
        ImGui::SameLine(0, 4);
        ImVec4 chatBg = ImGui::GetStyleColorVec4(ImGuiCol_ChildBg);  // fades with the window, like everything but text
        chatBg.w = g_look.chatBg ? chatBg.w * std::clamp(g_cfg.opacity, 0, 255) / 255.0f : 0.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, chatBg);
        ImGui::BeginChild("chat", ImVec2(0, h), false, ImGuiWindowFlags_NoScrollbar);
        ImGui::PopStyleColor();
        if (setScroll >= 0) ImGui::SetScrollY(setScroll);
        const ImVec2 childP0 = ImGui::GetWindowPos();
        const ImVec2 childP1 = ImVec2(childP0.x + ImGui::GetWindowSize().x, childP0.y + ImGui::GetWindowSize().y);
        const bool chatHovered = ImGui::IsMouseHoveringRect(childP0, childP1);
        const float wheel = io.MouseWheel;
        if (chatHovered && wheel > 0.0f) {
            atBottom = false;
        }
        if (g_activeTab != lastActiveTab) {
            lastActiveTab = g_activeTab;
            atBottom = true;
        }
        static const std::wstring s_emptyW;
        const WhisperTab* front = g_activeTab >= 0 && g_activeTab < static_cast<int>(g_tabs.size()) ? &g_tabs[g_activeTab] : nullptr;
        const std::wstring& tabName = front && !front->custom ? front->nameFolded : s_emptyW;
        const uint32_t tabChannels = front && front->custom ? front->channels : 0xFFFFFFFFu;
        RenderChatSourceNotice();  // reading Events: Chat is off or paused: why, and what you can do
        if (g_lines.empty() && g_layout.infoText) {  // nothing arrived yet: what is ready, what is missing, where translation is switched on
            lock.unlock();
            RenderModules(true);
            lock.lock();
        }
        g_visibleLines.clear();
        // Only what is in view is drawn: a line measured before (same width, font, text) that lies outside the view
        // (plus a margin) is just a placeholder of its height – the layout stays exactly the same.
        const float rowWrap = ImGui::GetContentRegionAvail().x, rowFont = ImGui::GetFontSize();
        const float viewTop = ImGui::GetScrollY() - line * 4;
        const float viewBottom = ImGui::GetScrollY() + ImGui::GetWindowHeight() + line * 4;
        const float rowGap = ImGui::GetStyle().ItemSpacing.y;
        for (ChatLine& l : g_lines) {
            if (!tabName.empty() && (l.channel != Channel::Whisper || l.speakerFolded != tabName)) continue;
            if (!(tabChannels & ChannelBit(l.channel))) continue;  // a tab of your own: only its channels
            const size_t rowLen = l.translated.empty() ? l.text.size() : l.translated.size() + 1;
            const float y0 = ImGui::GetCursorPosY();
            if (l.rowH > 0 && l.rowW == rowWrap && l.rowFont == rowFont && l.rowLen == rowLen &&
                (y0 + l.rowH < viewTop || y0 > viewBottom)) {
                ImGui::Dummy(ImVec2(1.0f, std::max(0.0f, l.rowH - rowGap)));
                continue;
            }
            ImGui::PushID(static_cast<int>(l.id));
            if (!l.speaker.empty()) {  // the name in the channel colour – a click: whisper with that player in their tab
                LookText(l.speakerDisplay.c_str(), NameColorOf(l.channel), false);
                const bool canWhisper = !l.own || (l.outgoing && l.channel == Channel::Whisper);
                if (canWhisper && ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y - 1), ImVec2(b.x, b.y - 1),
                                                        ImGui::GetColorU32(NameColorOf(l.channel)));
                }
                if (canWhisper && ImGui::IsItemClicked()) OpenWhisperTab(l.speaker);
                ImGui::SameLine(0, ImGui::CalcTextSize(" ").x);
            }
            const std::wstring& shown = l.translated.empty() ? l.text : l.translated;
            const std::string& u8shown = l.translated.empty() ? l.u8text : l.u8translated;
            const auto& segs = l.translated.empty() ? l.textSegs : l.transSegs;
            bool clicked = false;
            if (segs.size() <= 1 && (segs.empty() || segs[0].kind == PieUI::ChatLinks::SegmentKind::Plain)) {
                LookText(u8shown.c_str(), ColorOf(l.channel), true);
            } else {
                clicked = RichLine(shown, segs, ColorOf(l.channel), l.receivedTime, l.speaker, l.id);
            }
            if (!l.translated.empty() && ImGui::IsItemHovered()) ImGui::SetTooltip("%s", l.u8text.c_str());
            // A click on the text of a line someone else wrote: just this line into your language.
            if (!l.own && ImGui::IsItemVisible()) g_visibleLines.push_back(l.id);  // in view: "translate once" takes it
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) s_lineRightClick = true;
            if (!clicked && !l.own && l.translated.empty() && (ImGui::IsItemClicked() || ImGui::IsItemClicked(ImGuiMouseButton_Right)))
                g_worker.Post([id = l.id, t = l.text] { TranslateLine(id, t, true); });
            ImGui::PopID();
            l.rowH = ImGui::GetCursorPosY() - y0;  // what it took, for the frames it is out of view
            l.rowW = rowWrap;
            l.rowFont = rowFont;
            l.rowLen = rowLen;
        }
        // Auto-scroll to bottom only when new lines arrive or content expands (e.g. item names resolved),
        // provided the user was at the bottom and is not actively scrolling up with the mouse wheel.
        const bool contentGrew = (scrollMax > lastScrollMax + 1.0f);
        const bool shouldScrollDown = (g_scrollDown || contentGrew);
        if (atBottom && shouldScrollDown && setScroll < 0 && (!chatHovered || wheel <= 0.0f)) {
            ImGui::SetScrollHereY(1.0f);
        }
        g_scrollDown = false;
        scrollY = ImGui::GetScrollY();
        scrollMax = ImGui::GetScrollMaxY();
        lastScrollMax = scrollMax;
        viewH = ImGui::GetWindowHeight();
        atBottom = scrollY >= scrollMax - 4;
        ImGui::EndChild();
    } else {  // no chat: the input stays the lowest line, the space above stays empty (see-through like the window)
        const float fill = ImGui::GetContentRegionAvail().y - lowerH;
        if (fill > 0) ImGui::Dummy(ImVec2(0, fill));
    }
    // A link from the chat opens only after your yes (http / https only).
    if (g_openLinkAsk) {
        g_openLinkAsk = false;
        ImGui::OpenPopup("###openlink");
    }
    if (ImGui::BeginPopupModal((UI(Tr(L"Leaving GW2Chatlator")) + "###openlink").c_str(), nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        // Like Discord: where it goes, big; the whole link small; you decide.
        ImGui::TextUnformatted(UI(Tr(L"This link goes to")).c_str());
        ImGui::TextColored(ImVec4(0.45f, 0.7f, 1.f, 1.f), "%s", U8(LinkHost(g_openLink)).c_str());
        ImGui::TextDisabled("%s", Fit(U8(g_openLink), ImGui::GetFontSize() * 30).c_str());
        ImGui::Spacing();
        ImGui::TextUnformatted(UI(Tr(L"Are you sure you want to open it?")).c_str());
        ImGui::Spacing();
        if (ImGui::Button(UI(Tr(L"Open link")).c_str())) {
            SafeOpenUrl(g_openLink);
            g_openLink.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button(UI(Tr(L"Go back")).c_str()) || ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape))) {
            g_openLink.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    CostMark(kCostInput);
    // ---- the preview: what arrives in the GW2 chat, the back-translation small under it
    const bool previewCurrent = g_previewGen == g_inputGen && !g_preview.empty();
    if (showPreview) {
        // The same light field as the input below: the two belong together.
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
        ImGui::BeginChild("preview", ImVec2(0, line * (g_layout.previewLines + 0.2f)), false);
        if (g_recording) {
            ImGui::TextColored(kPreview, "%s", g_voiceLive.empty() ? "..." : U8(g_voiceLive).c_str());
        } else if (!g_input[0]) {
            // empty: no hint text here – the chat above is what you read
        } else if (!Translating()) {
            LookText(U8(ComposePrefix() + SanitizeChatText(g_wFlatInput)).c_str(), Bright(kPreview), true);
        } else if (previewCurrent) {
            LookText(U8(ComposePrefix() + g_preview).c_str(), Bright(kPreview), true);
            if (!g_back.empty()) LookText(U8(g_back).c_str(), Bright(ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)), true);
        } else if (g_layout.infoText) {
            ImGui::TextDisabled("%s", UI(Tr(L"translating …")).c_str());
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // ---- the word bar: three cells across the line, what Tab takes in orange
    if (showWords) {
        const float w = ImGui::GetContentRegionAvail().x;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float h = ImGui::GetFrameHeight();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool have = g_cfg.suggestions && g_help.text == g_wFlatInput;
        for (int i = 0; i < 3; ++i) {
            const ImVec2 a(p.x + w * i / 3.0f, p.y), b(p.x + w * (i + 1) / 3.0f, p.y + h);
            if (i) dl->AddLine(ImVec2(a.x, a.y + h * 0.25f), ImVec2(a.x, b.y - h * 0.25f), ImGui::GetColorU32(ImGuiCol_Separator));
            if (!have || i >= static_cast<int>(g_help.bar.words.size())) continue;
            const std::string word = U8(g_help.bar.words[i]);
            ImGui::SetCursorScreenPos(a);
            ImGui::PushID(i);
            if (ImGui::InvisibleButton("##word", ImVec2(b.x - a.x, h))) TakeWord(g_help.bar.words[i]);
            const bool hot = ImGui::IsItemHovered();
            ImGui::PopID();
            if (hot) dl->AddRectFilled(a, b, ImGui::GetColorU32(ImGuiCol_FrameBgHovered));
            const ImVec2 size = ImGui::CalcTextSize(word.c_str());
            const bool pick = g_pick >= 0 ? i == g_pick : i == g_help.bar.autoIndex;
            dl->AddText(ImVec2((a.x + b.x - size.x) * 0.5f, a.y + (h - size.y) * 0.5f),
                        pick ? ImGui::GetColorU32(kAccent) : ImGui::GetColorU32(ImGuiCol_Text), word.c_str());
        }
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + h + st.ItemSpacing.y));
    }

    // ---- the input line; momentary messages (status) appear inside it
    if (g_focusInput && !g_sending) {
        ImGui::SetKeyboardFocusHere();
        g_focusInput = false;
    }
    if (!Lay().footer && Lay().minButton) {  // no footer: its minimise button stands left of the input, at its bottom
        const float btn = ImGui::GetFrameHeight(), rowY = ImGui::GetCursorPosY();
        ImGui::SetCursorPosY(rowY + std::max(0.0f, boxH - btn));
        if (ImGui::Button(">##mininput", ImVec2(btn, btn))) g_minimizeRequest = true;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", UI(Tr(L"Minimize chat")).c_str());
        ImGui::SameLine();
        ImGui::SetCursorPosY(rowY);
    }
    const ImVec2 boxSize(-1, boxH);
    g_wrapWidth = ImGui::GetContentRegionAvail().x - st.FramePadding.x * 2 - st.ScrollbarSize - 4;
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CtrlEnterForNewLine |
                                      ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackCharFilter |
                                      ImGuiInputTextFlags_CallbackAlways;
    ImGui::PushStyleColor(ImGuiCol_Text, Bright(ImGui::GetStyleColorVec4(ImGuiCol_Text)));  // your brightness here too
    const bool enter = ImGui::InputTextMultiline("##input", g_input, sizeof(g_input), boxSize, flags, InputCallback);
    ImGui::PopStyleColor();
    const std::string nowFlat = Flat(g_input);
    if (nowFlat != g_flatInput) {
        g_flatInput = nowFlat;
        g_wFlatInput = W(nowFlat);
    }
    const ImVec2 inputMin = ImGui::GetItemRectMin(), inputMax = ImGui::GetItemRectMax();
    const bool inputActive = ImGui::IsItemActive();
    // Minimise when not in use: nobody wrote, pointed at it or talked for a while – down to the small button.
    {
        const ULONGLONG now = GetTickCount64();
        const bool inUse = inputActive || g_sending || g_sendPending || g_focusInput || g_recording || g_showOptions ||
                           g_gameChatOpen || g_redirect != 0 || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId) ||
                           ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (inUse || g_lastUse == 0) {
            g_lastUse = now;
        } else if (g_layout.autoMinimize && now - g_lastUse > static_cast<ULONGLONG>(g_layout.minimizeAfter) * 1000) {
            g_minimizeRequest = true;  // done on the main window next frame
            g_lastUse = 0;
        }
    }
    if (inputActive && !g_history.empty() && !g_edit.active) {
        const bool up = ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow));
        const bool down = ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow));
        const std::wstring& cur = g_wFlatInput;
        const bool browsing = cur.empty() || (g_historyPos >= 0 && g_historyPos < static_cast<int>(g_history.size()) &&
                                              cur == g_history[g_historyPos]);
        if ((up || down) && browsing) {
            g_historyPos = up ? std::min<int>(g_historyPos + 1, static_cast<int>(g_history.size()) - 1)
                              : std::max(g_historyPos - 1, -1);
            g_edit.active = true;
            g_edit.startByte = 0;
            g_edit.lenBytes = std::strlen(g_input);
            g_edit.insert = g_historyPos >= 0 ? U8(g_history[g_historyPos]) : std::string();
            g_inputSpoken = false;  // a message from the history: as typed
        }
    }
    // Right-click in the input (your own words only): the learning mode – what you corrected yourself while typing is
    // learned (autocorrection writes it right next time), and abbreviations get their meaning (for the translator).
    static bool teach = false, s_menuNext = false;
    // A right-click on a word you wrote: the learning mode for that word. Anywhere else in the input (no word under the
    // mouse, an empty input): the menu – so it is always there, in the mini mode too.
    if (ImGui::IsMouseHoveringRect(inputMin, inputMax) && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Right)) {
        const std::wstring hit = WordAtMouse(ImVec2(inputMin.x + st.FramePadding.x, inputMin.y + st.FramePadding.y));
        if (!hit.empty()) {
            g_teachWord = hit;
            teach = true;
        } else if (!ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
            ImGui::OpenPopup("kbmenu");
        }
    }
    else if (!Lay().header && !s_lineRightClick && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) &&
             ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        ImGui::OpenPopup("kbmenu");  // without the header the window's own right-click is the menu
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGui::IsMouseReleased(ImGuiMouseButton_Right))
        s_lineRightClick = false;
    // The normal window: held down in the input for a moment and dragged, it moves too (elsewhere ImGui moves it by
    // itself; a quick drag in the input still selects text). The mini mode does this in its fitting above.
    if (Lay().chat) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(inputMin, inputMax) &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
            g_ownDownAt = GetTickCount64();
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            g_ownDownAt = 0;
            g_ownMoving = false;
        } else if (g_ownDownAt && !g_ownMoving && GetTickCount64() - g_ownDownAt > 250) {
            g_ownMoving = true;
        }
        if (g_ownMoving) {
            DragWindow(io.MouseDelta);
        }
    }
    // Three quick left clicks on the window (not in the input – there clicks place the caret and pick words –, not on a
    // button) minimise it (Fallen's idea): the way when header, footer and the minimise button are hidden.
    {
        static ULONGLONG s_clicks[3] = {};
        static ImVec2 s_firstAt;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            (!Lay().chat || (!ImGui::IsMouseHoveringRect(inputMin, inputMax) && !ImGui::IsAnyItemHovered())) &&
            !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)) {
            const ULONGLONG now = GetTickCount64();
            const float radius = ImGui::GetFontSize() * 2;
            if (now - s_clicks[2] > 700 || std::abs(io.MousePos.x - s_firstAt.x) > radius || std::abs(io.MousePos.y - s_firstAt.y) > radius) {
                s_clicks[0] = s_clicks[1] = 0;  // a new series
                s_firstAt = io.MousePos;
            }
            s_clicks[0] = s_clicks[1];
            s_clicks[1] = s_clicks[2];
            s_clicks[2] = now;
            if (s_clicks[0] && now - s_clicks[0] <= 900) {
                s_clicks[0] = s_clicks[1] = s_clicks[2] = 0;
                g_minimizeRequest = true;  // done on the main window next frame
            }
        }
    }
    if (teach) {
        teach = false;
        ImGui::OpenPopup("##teach");
    }
    {  // opens at our window's top right corner, beside it (1 px; on its left when there is no room to the right) –
       // in every mode, never where the mouse was. Placed when it opens: then you can drag it wherever you like.
        const ImVec2 wp = ImGui::GetWindowPos(), ws = ImGui::GetWindowSize();
        const bool right = wp.x + ws.x + ImGui::GetFontSize() * 26 < ImGui::GetIO().DisplaySize.x;
        ImGui::SetNextWindowPos(right ? ImVec2(wp.x + ws.x + 1, wp.y) : ImVec2(wp.x - 1, wp.y), ImGuiCond_Appearing,
                                right ? ImVec2(0, 0) : ImVec2(1, 0));
    }
    if (ImGui::BeginPopup("##teach")) {
        static char typed[64] = {}, fixed[64] = {}, word[64] = {}, meaning[160] = {};
        if (ImGui::IsWindowAppearing()) {  // the last word you typed, and what is known of it
            const std::wstring& text = g_wFlatInput;
            const std::vector<Span> spans = WordSpans(text);
            const std::wstring last = !g_teachWord.empty() ? g_teachWord  // the word you right-clicked
                                      : spans.empty() ? std::wstring() : text.substr(spans.back().start, spans.back().length);
            typed[0] = 0;
            strncpy_s(fixed, U8(last).c_str(), _TRUNCATE);
            strncpy_s(word, U8(last).c_str(), _TRUNCATE);
            strncpy_s(meaning, U8(g_myWords.MeaningOf(last)).c_str(), _TRUNCATE);
        }
        auto teachFix = [](const std::wstring& t, const std::wstring& f) {
            g_worker.Post([t, f] {
                const bool ok = g_spell && g_spell->TeachFix(t, f);
                std::lock_guard<std::mutex> lock(g_mu);
                SetStatus(ok ? TrF(L"Learned: {1} > {2}", {t, f}) : TrF(L"\"{1}\" is a real word – not learned as a typo", {t}), 6000);
            });
        };
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(UI(Tr(L"Learn your corrections")).c_str());
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight()));
        if (IconButton("##teachclose", Icon::Close, false, nullptr)) ImGui::CloseCurrentPopup();  // the X: top right
        Hint(UI(Tr(L"What you corrected yourself while typing: learned once, autocorrection writes it right next time.")));
        int learned = -1;
        for (size_t i = 0; i < g_selfFixes.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            ImGui::AlignTextToFramePadding();
            ImGui::Text("%s  >  %s", U8(g_selfFixes[i].typed).c_str(), U8(g_selfFixes[i].fixed).c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton(UI(Tr(L"Learn")).c_str())) learned = static_cast<int>(i);
            ImGui::PopID();
        }
        if (learned >= 0) {
            teachFix(g_selfFixes[learned].typed, g_selfFixes[learned].fixed);
            g_selfFixes.erase(g_selfFixes.begin() + learned);
        }
        if (g_selfFixes.empty()) ImGui::TextDisabled("%s", UI(Tr(L"No corrections of your own yet – or enter one:")).c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputTextWithHint("##typed", UI(Tr(L"I type")).c_str(), typed, sizeof(typed));
        ImGui::SameLine();
        ImGui::TextUnformatted(">");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputTextWithHint("##fixed", UI(Tr(L"I mean")).c_str(), fixed, sizeof(fixed));
        ImGui::SameLine();
        if (ImGui::Button(UI(Tr(L"Learn") + L"##fix").c_str()) && !Trim(W(typed)).empty() && !Trim(W(fixed)).empty()) {
            teachFix(Trim(W(typed)), Trim(W(fixed)));
            typed[0] = 0;
        }
        ImGui::Separator();
        ImGui::TextUnformatted(UI(Tr(L"Your word or abbreviation – what it means")).c_str());
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8);
        ImGui::InputText("##w", word, sizeof(word));
        ImGui::SameLine();
        ImGui::TextUnformatted("=");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14);
        const bool go = ImGui::InputTextWithHint("##m", UI(Tr(L"meaning, e.g. I think")).c_str(), meaning, sizeof(meaning),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button(UI(Tr(L"Learn") + L"##mean").c_str()) || go) && !Trim(W(word)).empty()) {
            const std::wstring w = Trim(W(word)), m = Trim(W(meaning));
            if (m.empty()) g_myWords.Remove(w);
            else g_myWords.Set(w, m);
            SaveMyWords();
            g_worker.Post([w] { if (g_spell) g_spell->AddUserWord(w); });
            ++g_inputGen;  // translated again with it
            g_inputChanged = GetTickCount64();
            SetStatus(m.empty() ? TrF(L"\"{1}\" forgotten", {w}) : TrF(L"Learned: {1} = {2}", {w, m}), 6000);
        }
        Hint(UI(Tr(L"The translator gets the meaning; your text stays as you wrote it.")));
        if (!Lay().header) {  // without the header (maybe nothing but the input left): the way to the menu
            ImGui::Separator();
            if (ImGui::Selectable(UI(Tr(L"Menu …")).c_str())) {
                s_menuNext = true;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::EndPopup();
    }
    if (s_menuNext) {  // opened here, at the window's level, where the menu lives
        s_menuNext = false;
        ImGui::OpenPopup("kbmenu");
    }
    const ImVec4 clip(inputMin.x, inputMin.y, inputMax.x, inputMax.y);
    const ImVec2 textAt(inputMin.x + st.FramePadding.x, inputMin.y + st.FramePadding.y);
    if (const std::wstring rest = GhostRest(g_wFlatInput); !rest.empty() && inputActive) {  // what Space / Tab writes
        // In the line where the text ends (the box is its own child window: drawn on top, clipped to the box).
        const size_t inLen = std::strlen(g_input);
        const char* lastLine = std::strrchr(g_input, '\n');
        const int lineNo = static_cast<int>(std::count(g_input, g_input + inLen, '\n'));
        const ImVec2 typed = ImGui::CalcTextSize(lastLine ? lastLine + 1 : g_input);
        if (lineNo < g_boxLines)
            BoxText(ImVec2(textAt.x + typed.x, textAt.y + lineNo * ImGui::GetTextLineHeight()), clip,
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), UI(rest));
    }
    // Status: in the empty box where you would write; while you write, a momentary message small at the bottom right.
    if (!g_input[0]) {
        if (g_recording && !showPreview && !g_voiceLive.empty())
            BoxText(textAt, clip, ImGui::GetColorU32(kPreview), U8(g_voiceLive));
        else {
            static std::wstring s_lastStatusW;
            static std::string s_lastStatusFit;
            static float s_lastStatusWidth = 0.0f;
            const float availW = inputMax.x - textAt.x - st.FramePadding.x;
            const std::wstring stW = StatusText();
            if (stW != s_lastStatusW || std::abs(availW - s_lastStatusWidth) > 1.0f) {
                s_lastStatusW = stW;
                s_lastStatusWidth = availW;
                s_lastStatusFit = Fit(UI(stW), availW);
            }
            if (g_layout.infoText || !g_status.empty())
                BoxText(textAt, clip, g_status.empty() ? ImGui::GetColorU32(ImGuiCol_TextDisabled) : ImGui::GetColorU32(kAccent),
                        s_lastStatusFit);
        }
    } else if (!g_status.empty()) {
        const std::string s = Fit(UI(g_status), (inputMax.x - inputMin.x) * 0.6f);
        const float w = ImGui::CalcTextSize(s.c_str()).x * 0.85f;
        BoxText(ImVec2(inputMax.x - w - st.FramePadding.x - st.ScrollbarSize, inputMax.y - smallLine - 1), clip,
                ImGui::GetColorU32(kAccent), s, 0.85f);
    }
    // Compact view: what the chat will get, small in the box's last line (instead of the preview above).
    if (!showPreview && g_input[0] && Translating() && (previewCurrent || g_layout.infoText)) {
        const std::string t = previewCurrent ? "> " + U8(g_preview) : UI(Tr(L"translating …"));
        BoxText(ImVec2(textAt.x, inputMax.y - smallLine - 1), clip, ImGui::GetColorU32(kPreview),
                Fit(t, (inputMax.x - inputMin.x) * (g_status.empty() ? 0.95f : 0.38f)), 0.85f);
    }

    CostMark(kCostFooter);
    // ---- the footer: channel | write | send · translator | auto | counter
    if (Lay().footer) RenderFooter();
    // Changes of the text: a pause translates it; the typing help follows every key.
    if (g_flatInput != g_lastInput) {
        g_lastInput = g_flatInput;
        NoteSelfFix(g_wFlatInput);
        g_inputChanged = GetTickCount64();
        ++g_inputGen;
        g_back.clear();
        g_pick = -1;
        if (!g_helpRequested) {
            g_helpRequested = true;
            g_worker.Post(ComputeTyping);
        }
    }
    if (g_inputChanged && GetTickCount64() - g_inputChanged > 400 && !g_flatInput.empty() && Translating()) {
        g_inputChanged = 0;
        g_worker.Post([gen = g_inputGen, t = SanitizeChatText(g_wFlatInput)] { TranslateInput(gen, t); });
    }
    if (enter) StartSend();
    if (g_autoSendPending && !g_edit.active && g_input[0]) {  // the dictation is in the line now
        g_autoSendPending = false;
        StartSend();
    }
    lock.unlock();
    ImGui::End();
}

}  // namespace kb
