// kb_options.cpp — the options window: its frame, the side bar, what a change sets off, and the row widgets the pages share.

#include "win/cpu_cores.hpp"
#include "kb_shared.hpp"
#include "kb_options_parts.hpp"
#include "imgui/imgui_internal.h"
#include "win/local_mt_packages.hpp"

namespace kb {

int g_optionsTab = -1;  // a tab of the options window to bring to the front (General 0, Window 1, Voice 3, Translator 5)
static int s_delayedTab = -1;  // cross-frame safe tab switch (set during frame, applied on next BeginTabBar)



void StatusRow(const std::string& what, int state, const std::string& text) {  // state: 2 ready, 1 busy, 0 missing
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(state == 2 ? ImVec4(0.45f, 0.85f, 0.45f, 1.f) : state == 1 ? ImVec4(0.94f, 0.75f, 0.35f, 1.f)
                                                                                  : ImVec4(0.95f, 0.55f, 0.35f, 1.f),
                       "%s", state == 2 ? "[OK]" : state == 1 ? "[..]" : "[--]");
    ImGui::SameLine();
    ImGui::TextUnformatted(what.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", text.c_str());
}

void Hint(const std::string& text) {  // grey and wrapped: long hints are never cut off
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void RenderOptionsWindow() {
    if (!g_showOptions) return;
    static std::string s_optsTitle;
    static UiLang s_optsLang = static_cast<UiLang>(-1);
    if (s_optsLang != g_cfg.uiLang || s_optsTitle.empty()) {
        s_optsLang = g_cfg.uiLang;
        s_optsTitle = UI(Tr(L"GW2Chatlator – Options")) + "###kbopts";
    }
    // While the UI scale slider is held this window keeps its size: it would move the slider under the mouse, and the
    // value with it. The chat window follows at once, this one when you let go.
    static float s_optsPx = 0.0f;
    if (!g_uiScaleDragging || s_optsPx <= 0.0f) s_optsPx = FollowUiPx();
    const float nexusPx = s_optsPx;
    RequestUiFont(nexusPx);
    ImFont* uf = g_uiFont.load();
    const float optsW = std::max(820.0f, 820.0f * (nexusPx / 16.0f));
    const float optsH = std::max(620.0f, 620.0f * (nexusPx / 16.0f));
    ImGui::SetNextWindowSize(ImVec2(optsW, optsH), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(660, 480), ImVec2(4000, 4000));
    const bool visible = ImGui::Begin(s_optsTitle.c_str(), &g_showOptions);
    if (visible && ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows)) {
        ImGui::GetIO().WantCaptureMouse = true;
    }
    bool pushedUf = false;
    if (uf && uf->FontSize > 0.0f) {
        ImGui::SetWindowFontScale(nexusPx / uf->FontSize);
        ImGui::PushFont(uf);
        pushedUf = true;
    } else {
        const float currentPx = std::max(1.0f, ImGui::GetFontSize());
        ImGui::SetWindowFontScale(nexusPx / currentPx);
    }
    if (visible) RenderOptionsContent();
    if (pushedUf) ImGui::PopFont();
    ImGui::End();
}

std::wstring FormatSeconds(int ms) {  // 1234 -> "1.2"
    wchar_t b[16];
    swprintf(b, 16, L"%.1f", ms / 1000.0);
    return b;
}

// A language combo over the languages our window draws (DrawnInOurWindow): `value` "" = `emptyLabel`.
bool LanguageCombo(const char* id, std::wstring& value, const std::wstring& emptyLabel) {
    std::string current = value.empty() ? U8(emptyLabel) : U8(value);
    for (const LangInfo& l : Languages())
        if (PrimaryLang(l.code) == PrimaryLang(value) && !value.empty()) current = U8(NativeName(l.code) + L" (" + l.code + L")");
    bool changed = false;
    if (ImGui::BeginCombo(id, current.c_str())) {
        if (!emptyLabel.empty() && ImGui::Selectable(U8(emptyLabel).c_str(), value.empty())) {
            value.clear();
            changed = true;
        }
        for (const LangInfo& l : Languages()) {
            if (!DrawnInOurWindow(l)) continue;
            const std::string n = U8(NativeName(l.code) + L" (" + l.code + L")");
            if (ImGui::Selectable(n.c_str(), value == l.code)) {
                value = l.code;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

namespace {
// ---- The options window (2.8, user 2026-11-01): a side bar of pages as Nexus has it; on each page framed groups with
// a small title and rows "label | control". Everything applies at once – there is no "Save" (ApplyOptions).

constexpr ImU32 kOnGreen = IM_COL32(47, 158, 101, 255);
// A switch that is on: signal colours are never a fill in the CBA scheme, there it takes the structure's colour.
ImU32 OnTrack() { return g_theme == Theme::Cba ? ImGui::GetColorU32(ImGuiCol_ButtonActive) : kOnGreen; }
constexpr ImU32 kOffTrack = IM_COL32(58, 61, 68, 255);
constexpr ImU32 kReadyGreen = IM_COL32(95, 211, 145, 255);
constexpr ImU32 kMissingOrange = IM_COL32(233, 167, 90, 255);

struct BoxFrame {
    ImVec2 at;
    std::string title;
};
std::vector<BoxFrame> s_boxes;
}  // namespace

float g_labelW = 0.0f;  // the label column: as wide as the widest label of the frame before
static float s_labelNeed = 0.0f;
static int s_page = kPageStart;

float ControlW() { return std::min(ImGui::GetFontSize() * 15.0f, std::max(ImGui::GetContentRegionAvail().x, 1.0f)); }

// An on/off switch with its label (a click on either flips it); not usable: dimmed, the tooltip says why.
bool Switch(const char* id, const std::string& label, bool* v, const char* tip, bool usable, float maxW) {
    const float fs = ImGui::GetFontSize();
    const float frameH = ImGui::GetFrameHeight();
    const float h = std::round(fs), w = std::round(h * 1.85f);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    // A long label in a narrow window wraps under itself instead of running out of the window.
    const float room = maxW > 0.0f ? std::min(maxW, ImGui::GetContentRegionAvail().x) : ImGui::GetContentRegionAvail().x;  // maxW: a column's width
    const float wrapW = std::max(fs * 4.0f, room - w - fs * 0.6f);
    const ImVec2 labelSize = ImGui::CalcTextSize(label.c_str(), nullptr, false, wrapW);
    ImGui::PushID(id);
    const bool pressed = ImGui::InvisibleButton(
        "##switch", ImVec2(w + fs * 0.6f + labelSize.x, std::max(frameH, labelSize.y + ImGui::GetStyle().FramePadding.y * 2.0f)));
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered();
    if (pressed && usable) *v = !*v;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float y = p.y + std::floor((frameH - h) * 0.5f);
    dl->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + w, y + h), usable && *v ? OnTrack() : kOffTrack, h * 0.5f);
    dl->AddCircleFilled(ImVec2(*v ? p.x + w - h * 0.5f : p.x + h * 0.5f, y + h * 0.5f), h * 0.5f - 2.0f,
                        !usable ? IM_COL32(120, 122, 128, 255) : *v ? IM_COL32_WHITE : IM_COL32(201, 204, 210, 255), 16);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + w + fs * 0.6f, p.y + ImGui::GetStyle().FramePadding.y),
                ImGui::GetColorU32(usable ? ImGuiCol_Text : ImGuiCol_TextDisabled), label.c_str(), nullptr, wrapW);
    if (hovered && tip) ImGui::SetTooltip("%s", tip);
    if (hovered && usable) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return pressed && usable;
}


// A row's label in the label column; the control follows on the same line.
void RowLabel(const std::string& label) {
    const float x0 = ImGui::GetCursorPosX();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label.c_str());
    s_labelNeed = std::max(s_labelNeed, ImGui::GetItemRectSize().x);
    ImGui::SameLine();
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), x0 + g_labelW));
}


// A grey line under a row, where its control starts.
void RowHint(const std::string& text) {
    ImGui::Indent(g_labelW);
    Hint(text);
    ImGui::Unindent(g_labelW);
}

// What a setting risks, in amber under it: nothing breaks, but you should know.
void RiskHint(const std::wstring& text) {
    if (text.empty()) return;
    ImGui::Indent(g_labelW);
    ImGui::PushStyleColor(ImGuiCol_Text, UiWarm());
    ImGui::TextWrapped("%s", UI(text).c_str());
    ImGui::PopStyleColor();
    ImGui::Unindent(g_labelW);
}

// A combo over `items` in a row.
bool RowCombo(const char* id, const std::vector<std::string>& items, int* sel) {
    bool changed = false;
    ImGui::SetNextItemWidth(ControlW());
    if (ImGui::BeginCombo(id, items[std::clamp(*sel, 0, static_cast<int>(items.size()) - 1)].c_str())) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i)
            if (ImGui::Selectable(items[i].c_str(), i == *sel)) {
                *sel = i;
                changed = true;
            }
        ImGui::EndCombo();
    }
    return changed;
}

// A slider in a row; `format` shows the value with its unit ("%d pt").
bool RowSlider(const char* id, int* v, int lo, int hi, const char* format) {
    ImGui::SetNextItemWidth(ControlW());
    return ImGui::SliderInt(id, v, lo, hi, format);
}

namespace {
// The page's name, bigger, and what it is for.
void PageHead(const std::string& title, const std::string& sub) {
    const float fs = ImGui::GetFontSize();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = ImGui::GetColorU32(ImGuiCol_Text);
    dl->AddText(ImGui::GetFont(), fs * 1.2f, p, col, title.c_str());
    dl->AddText(ImGui::GetFont(), fs * 1.2f, ImVec2(p.x + 0.7f, p.y), col, title.c_str());  // a little bolder
    ImGui::Dummy(ImVec2(0.0f, fs * 1.25f));
    ImGui::TextDisabled("%s", sub.c_str());
}

void DrawVectorMicIcon(ImDrawList* dl, ImVec2 c, float u, ImU32 col) {
    dl->AddRectFilled(ImVec2(c.x - 2.5f * u, c.y - 6 * u), ImVec2(c.x + 2.5f * u, c.y + 1.5f * u), col, 2.5f * u);
    dl->PathArcTo(ImVec2(c.x, c.y - 1 * u), 4.5f * u, 0.0f, 3.14159f, 10);
    dl->PathStroke(col, false, 1.4f * u);
    dl->AddLine(ImVec2(c.x, c.y + 3.5f * u), ImVec2(c.x, c.y + 6 * u), col, 1.4f * u);
    dl->AddLine(ImVec2(c.x - 3 * u, c.y + 6 * u), ImVec2(c.x + 3 * u, c.y + 6 * u), col, 1.4f * u);
}

void DrawVectorGlobeIcon(ImDrawList* dl, ImVec2 c, float u, ImU32 col) {
    dl->AddCircle(c, 6.5f * u, col, 16, 1.4f * u);
    dl->AddLine(ImVec2(c.x - 6.5f * u, c.y), ImVec2(c.x + 6.5f * u, c.y), col, 1.2f * u);
    dl->AddLine(ImVec2(c.x, c.y - 6.5f * u), ImVec2(c.x, c.y + 6.5f * u), col, 1.2f * u);
    dl->PathArcTo(ImVec2(c.x - 2.5f * u, c.y), 4.0f * u, -1.57f, 1.57f, 8);
    dl->PathStroke(col, false, 1.0f * u);
    dl->PathArcTo(ImVec2(c.x + 2.5f * u, c.y), 4.0f * u, 1.57f, 4.71f, 8);
    dl->PathStroke(col, false, 1.0f * u);
}

// The side bar's symbols, drawn in a 20 x 20 box around `c` (`s`: pixels per unit).
void DrawPageIcon(ImDrawList* dl, ImVec2 c, float s, int page, ImU32 col) {
    auto P = [&](float x, float y) { return ImVec2(c.x + (x - 10.0f) * s, c.y + (y - 10.0f) * s); };
    const float t = 1.5f * s;
    auto poly = [&](std::initializer_list<ImVec2> pts) {
        for (const ImVec2& q : pts) dl->PathLineTo(q);
        dl->PathStroke(col, true, t);
    };
    switch (page) {
        case kPageStart:  // a house
            poly({P(3, 9.5f), P(10, 3.5f), P(17, 9.5f), P(17, 17), P(12.5f, 17), P(12.5f, 12.5f), P(7.5f, 12.5f),
                  P(7.5f, 17), P(3, 17)});
            break;
        case kPageChat:  // a speech bubble
            poly({P(3, 4.5f), P(17, 4.5f), P(17, 13.5f), P(8, 13.5f), P(4.5f, 16.5f), P(4.5f, 13.5f), P(3, 13.5f)});
            break;
        case kPageTyping:  // a keyboard
            dl->AddRect(P(2, 5), P(18, 15), col, 1.5f * s, ImDrawCornerFlags_All, t);
            for (float x : {5.0f, 8.0f, 11.0f, 14.0f}) dl->AddRectFilled(P(x, 7.8f), P(x + 1.3f, 9.1f), col);
            dl->AddLine(P(6, 11.8f), P(14, 11.8f), col, t);
            break;
        case kPageVoice:
            DrawVectorMicIcon(dl, c, s * 1.2f, col);
            break;
        case kPageTranslator:
            DrawVectorGlobeIcon(dl, c, s * 7.0f / 6.5f, col);
            break;
        case kPageWindow:  // a window with lines
            dl->AddRect(P(2.5f, 3.5f), P(17.5f, 16.5f), col, 1.5f * s, ImDrawCornerFlags_All, t);
            dl->AddLine(P(2.5f, 7), P(17.5f, 7), col, t);
            dl->AddLine(P(6, 10.5f), P(14, 10.5f), col, t);
            dl->AddLine(P(6, 13.5f), P(11, 13.5f), col, t);
            break;
        default:  // Technical: a cog
            dl->AddCircle(c, 2.6f * s, col, 12, t);
            dl->AddCircle(c, 5.4f * s, col, 20, t);
            for (int k = 0; k < 8; ++k) {
                const float a = k * 3.14159265f / 4.0f;
                dl->AddLine(ImVec2(c.x + std::cos(a) * 5.6f * s, c.y + std::sin(a) * 5.6f * s),
                            ImVec2(c.x + std::cos(a) * 8.0f * s, c.y + std::sin(a) * 8.0f * s), col, t * 1.3f);
            }
    }
}

// One page in the side bar: its symbol and name; the page shown is lit.
bool NavItem(int page, const std::string& label, bool selected) {
    const float fs = ImGui::GetFontSize();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = std::max(ImGui::GetContentRegionAvail().x, 1.0f), h = std::round(fs * 2.0f);
    ImGui::PushID(page);
    const bool pressed = ImGui::InvisibleButton("##page", ImVec2(w, h));
    ImGui::PopID();
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    if (selected || hovered)
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), selected ? IM_COL32(255, 255, 255, 34) : IM_COL32(255, 255, 255, 16), 4.0f);
    const ImU32 col = ImGui::GetColorU32(selected || hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
    const float s = fs / 18.0f;
    DrawPageIcon(dl, ImVec2(p.x + fs * 0.6f + 10.0f * s, p.y + h * 0.5f), s, page, col);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(p.x + fs * 1.2f + 20.0f * s, p.y + (h - fs) * 0.5f), col, label.c_str());
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    return pressed;
}
}  // namespace

// Start's head: the one switch for the chat's translation, big, and beside it what is ready. True when clicked.
bool Hero(bool on, const std::string& title, const std::string& sub, const std::string& state1, bool ok1,
          const std::string& state2, bool ok2, bool* toMissing) {
    const float fs = ImGui::GetFontSize();
    ImFont* font = ImGui::GetFont();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = std::max(ImGui::GetContentRegionAvail().x, 1.0f);
    const float smallPx = fs * 0.92f;
    const float rightW = std::max(font->CalcTextSizeA(smallPx, FLT_MAX, 0.0f, state1.c_str()).x,
                                  font->CalcTextSizeA(smallPx, FLT_MAX, 0.0f, state2.c_str()).x) + fs * 1.8f;
    const float swH0 = std::round(fs * 1.125f), textW = std::max(fs * 4.0f, w - rightW - fs * 1.8f - std::round(swH0 * 1.8f));
    const float subH = font->CalcTextSizeA(smallPx, FLT_MAX, textW, sub.c_str()).y;  // a narrow window: the grey line wraps
    const float h = std::round(std::max(fs * 3.3f, fs * 1.75f + subH + fs * 0.45f));
    const bool pressed = ImGui::InvisibleButton("##hero", ImVec2(std::max(1.0f, w - rightW), h));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    if (toMissing && (!ok1 || !ok2)) {  // an orange state: a click goes to what is still missing (the same height: no shift)
        const ImVec2 after = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(p.x + w - rightW, p.y));
        if (ImGui::InvisibleButton("##herostate", ImVec2(rightW, h))) *toMissing = true;
        if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
        ImGui::SetCursorScreenPos(after);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(255, 255, 255, hovered ? 16 : 8), 4.0f);
    dl->AddRect(p, ImVec2(p.x + w, p.y + h), ImGui::GetColorU32(ImGuiCol_Separator), 4.0f);
    const float swH = std::round(fs * 1.125f), swW = std::round(swH * 1.8f);  // three quarters of what it was (Emi)
    const ImVec2 s(p.x + fs * 0.9f, p.y + std::floor((h - swH) * 0.5f));
    dl->AddRectFilled(s, ImVec2(s.x + swW, s.y + swH), on ? OnTrack() : kOffTrack, swH * 0.5f);
    dl->AddCircleFilled(ImVec2(on ? s.x + swW - swH * 0.5f : s.x + swH * 0.5f, s.y + swH * 0.5f), swH * 0.5f - 3.0f,
                        on ? IM_COL32_WHITE : IM_COL32(201, 204, 210, 255), 20);
    const float tx = s.x + swW + fs * 0.9f;
    const ImVec4 clip(tx, p.y, p.x + w - rightW, p.y + h);  // long words in a narrow window: cut, never over the right
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_Text);
    dl->AddText(font, fs * 1.08f, ImVec2(tx, p.y + fs * 0.5f), text, title.c_str(), nullptr, 0.0f, &clip);
    dl->AddText(font, fs * 1.08f, ImVec2(tx + 0.7f, p.y + fs * 0.5f), text, title.c_str(), nullptr, 0.0f, &clip);
    dl->AddText(font, smallPx, ImVec2(tx, p.y + fs * 1.75f), ImGui::GetColorU32(ImGuiCol_TextDisabled), sub.c_str(), nullptr,
                std::max(1.0f, p.x + w - rightW - tx), &clip);
    auto state = [&](float y, const std::string& t, bool ok) {
        const float x = p.x + w - fs * 0.8f - font->CalcTextSizeA(smallPx, FLT_MAX, 0.0f, t.c_str()).x;
        dl->AddCircleFilled(ImVec2(x - fs * 0.55f, y + smallPx * 0.55f), fs * 0.22f, ok ? kReadyGreen : kMissingOrange, 12);
        dl->AddText(font, smallPx, ImVec2(x, y), ok ? text : kMissingOrange, t.c_str());
    };
    state(p.y + fs * 0.6f, state1, ok1);
    state(p.y + fs * 1.75f, state2, ok2);
    return pressed;
}

// A framed group with its title on the top line (Nexus' look); EndBox draws the frame around what came between.
void BeginBox(const std::string& title) {
    const float fs = ImGui::GetFontSize();
    ImGui::Dummy(ImVec2(0.0f, fs * 0.45f));
    s_boxes.push_back({ImGui::GetCursorScreenPos(), title});
    ImGui::Dummy(ImVec2(0.0f, fs * 0.4f));
    ImGui::Indent(fs * 0.75f);
    ImGui::BeginGroup();
}

void EndBox() {
    ImGui::EndGroup();
    const float fs = ImGui::GetFontSize();
    ImGui::Unindent(fs * 0.75f);
    if (s_boxes.empty()) return;
    const BoxFrame b = s_boxes.back();
    s_boxes.pop_back();
    const float bottom = std::floor(ImGui::GetItemRectMax().y + fs * 0.45f);
    const float right = std::floor(ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - 1.0f);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 line = ImGui::GetColorU32(ImGuiCol_Separator);
    const ImVec2 ts = ImGui::CalcTextSize(b.title.c_str());
    const float tx = b.at.x + fs * 0.6f;
    dl->AddLine(b.at, ImVec2(tx - 4.0f, b.at.y), line);
    dl->AddLine(ImVec2(tx + ts.x + 4.0f, b.at.y), ImVec2(right, b.at.y), line);
    dl->AddLine(b.at, ImVec2(b.at.x, bottom), line);
    dl->AddLine(ImVec2(right, b.at.y), ImVec2(right, bottom), line);
    dl->AddLine(ImVec2(b.at.x, bottom), ImVec2(right + 1.0f, bottom), line);
    dl->AddText(ImGui::GetFont(), fs, ImVec2(tx, b.at.y - std::floor(ts.y * 0.5f)), ImGui::GetColorU32(ImGuiCol_Text),
                b.title.c_str());
    ImGui::SetCursorScreenPos(ImVec2(b.at.x, bottom + 1.0f));
    ImGui::Dummy(ImVec2(0.0f, fs * 0.25f));
}

void SwitchOptionsTab(int tab) {
    s_delayedTab = tab;
    g_showOptions = true;
}

// The settings this window edits through its copy (`edit`); sizes, the layout and the looks are taken and saved at
// once where they are drawn. One list for both: what counts as changed here, and what goes into the settings.
#define KB_VOICE_OPTIONS(X)                                                                                            \
    X(voiceEnabled) X(voiceModel) X(voiceLang) X(voiceToggle) X(voiceLive) X(voiceThreads)              \
    X(voiceDevice) X(voiceAutoSend) X(voiceBoost) X(voiceActivation) X(voicePauseMs) X(voicePreRollMs)                \
    X(voiceSensitivity) X(voiceMinMs) X(voiceGlobalKey) X(voiceChatPause)
#define KB_OPTIONS(X)                                                                                                  \
    X(engine) X(writeIn) X(backTranslate) X(chatCodeNames) X(understoodLangs) X(skipTypingLang) X(readLang) X(showOriginal) X(noJunk)   \
    X(localQuality) X(useGpu) X(beamStepDown) X(beamStepUp) X(thinkAlong) X(versions) X(mtThreads) X(m2m.patience) X(m2m.lengthPenalty) X(m2m.coveragePenalty) X(m2m.repetitionPenalty)   \
    X(m2m.noRepeatNgram) X(m2m.disableUnk) X(m2mCompute) X(basicEmail) X(deeplKey) X(googleKey) X(msKey) X(msRegion) X(supertextKey) X(libreUrl) X(libreKey)   \
    X(uiLangCode) X(spellEnabled) X(autoCorrect) X(suggestions) X(learnWords) X(learnLater) X(returnFocus)            \
    X(copyOnly) X(focusOnGameChat) X(send.autoTiming) X(send.keyHoldMs) X(send.stepDelayMs) KB_VOICE_OPTIONS(X)

namespace {
bool OptionsEdited(const Config& edit, const Config& opened) {
#define KB_DIFFERS(f) if (!(edit.f == opened.f)) return true;
    KB_OPTIONS(KB_DIFFERS)
#undef KB_DIFFERS
    return false;
}

// What was changed here goes into the settings at once – only that: what the footer or a menu changed meanwhile
// stays. Then what depends on it follows: the translator, the typing help, the voice module (g_mu NOT held).
void ApplyOptions(Config& edit, Config& opened) {
    bool typingChanged, learnChanged, voiceChanged, voiceTuned = false, pttChanged, listenChanged;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        Config next = g_cfg;
#define KB_TAKE(f) if (!(edit.f == opened.f)) next.f = edit.f;
        KB_OPTIONS(KB_TAKE)
#undef KB_TAKE
        next.uiLang = EffectiveUiLang(next.uiLangCode);
        const Config& old = g_cfg;
        typingChanged = next.writeIn != old.writeIn || next.spellEnabled != old.spellEnabled;
        learnChanged = next.learnWords != old.learnWords || next.learnLater != old.learnLater;
        voiceChanged = next.voiceEnabled != old.voiceEnabled;
#define KB_TUNED(f) voiceTuned = voiceTuned || !(next.f == old.f);
        KB_VOICE_OPTIONS(KB_TUNED)
#undef KB_TUNED
        pttChanged = next.voiceGlobalKey != old.voiceGlobalKey;
        listenChanged = voiceChanged || next.voiceActivation != old.voiceActivation;
        const bool engineChanged = next.engine != old.engine;
        const int physical = static_cast<int>(gct::PhysicalCores().size());
        // Only a changed translator or key builds a new one: a new local helper loads its model again.
        const bool translatorChanged = engineChanged || next.deeplKey != old.deeplKey || next.googleKey != old.googleKey ||
                                       next.msKey != old.msKey || next.msRegion != old.msRegion || next.supertextKey != old.supertextKey ||
                                       next.libreUrl != old.libreUrl || next.libreKey != old.libreKey ||
                                       next.basicEmail != old.basicEmail || next.mtThreads != old.mtThreads ||
                                       next.m2mCompute != old.m2mCompute ||  // the helper reads threads and calculation at its start
                                       (next.mtThreads == 0 && gct::LocalAutoThreads(next.localQuality, physical) != gct::LocalAutoThreads(old.localQuality, physical)) ||
                                       next.useGpu != old.useGpu;
        const bool qualityChanged = next.localQuality != old.localQuality;
        const bool versionsChanged = next.versions != old.versions;
        const bool decodeChanged = versionsChanged || (!(next.m2m == old.m2m) && next.engine == Engine::M2m);
        const bool readChanged = next.readLang != old.readLang || next.understoodLangs != old.understoodLangs ||
                                 next.skipTypingLang != old.skipTypingLang;
        const bool uiChanged = next.uiLang != old.uiLang;
        g_cfg = next;
        g_cfg.SaveAll();
        if (uiChanged) {
            SetUiLang(g_cfg.uiLang);
            PrepareFaultAlert();  // the alert after a fault in the language chosen now
        }
        if (engineChanged) g_myMemorySessionFallback = false;  // a translator you choose is the one that runs
        if (translatorChanged || !g_translator) {
            g_translator = MakeTranslator(g_cfg);
            WarmUpTranslator(g_translator);
        }
        if (decodeChanged && !translatorChanged) {
            if (g_cfg.engine == Engine::M2m) SetLocalMtDecode(g_translator, g_cfg.m2m);
            SetLocalMtVersions(g_translator, g_cfg.versions);
        }
        if (translatorChanged || typingChanged || decodeChanged) {
            g_preview.clear();
            ++g_inputGen;
            g_inputChanged = GetTickCount64();
        }
        // the chat in view comes again (a line remembers the target and beam it was made with)
        if (g_nx.translateChat && (translatorChanged || typingChanged || qualityChanged || decodeChanged || readChanged))
            TranslateRecentLines(false);
        if (engineChanged)
            SetStatus(TrF(L"Translator: {1}", {g_translator ? g_translator->Name() : std::wstring(L"-")}), 3000);
        else if (qualityChanged)
            SetStatus(TrF(L"Quality set: beam {1} – the visible chat is translated again", {std::to_wstring(g_cfg.localQuality)}), 4000);
        else if (decodeChanged && !versionsChanged)
            SetStatus(Tr(L"M2M100 settings set – the visible chat is translated again"), 4000);
        edit = opened = g_cfg;
    }
    if (typingChanged) g_worker.Post([] {
        FreeWorkerState();
        InitSpelling();
    });
    else if (learnChanged) g_worker.Post([learn = edit.learnWords] { if (g_spell) g_spell->SetLearnChoices(learn); });
    if (pttChanged) ApplyPushToTalkKey(edit.voiceGlobalKey);
    if (listenChanged) {
        if (!edit.voiceActivation || !edit.voiceEnabled) StopListening();
        else g_listenUserOff = false;
    }
    if (voiceChanged && edit.voiceEnabled && !g_voice.Running() && GetFileAttributesW(HelperExe().c_str()) != INVALID_FILE_ATTRIBUTES)
        g_worker.Post([dir = edit.dataDir] {
            Config::FlushWrites();
            std::lock_guard<std::mutex> lock(g_mu);
            if (!g_voice.Running()) g_voice.Start(HelperExe(), dir);
        });
    else if (voiceTuned && g_voice.Running())
        VoiceReload();
}

std::wstring PageName(int page) {
    switch (page) {
        case kPageStart: return Tr(L"Start");
        case kPageChat: return Tr(L"Chat");
        case kPageTyping: return Tr(L"Typing");
        case kPageVoice: return Tr(L"Voice");
        case kPageTranslator: return Tr(L"Translator");
        case kPageWindow: return Tr(L"Window");
        default: return Tr(L"Technical");
    }
}

std::wstring PageSub(int page) {
    switch (page) {
        case kPageStart: return Tr(L"Three settings – then it runs.");
        case kPageChat: return Tr(L"How lines appear and where Enter goes.");
        case kPageTyping: return Tr(L"Suggestions and spelling while you type.");
        case kPageVoice: return Tr(L"Voice module and microphone – runs on this PC.");
        case kPageTranslator: return Tr(L"Who translates – on this PC or online.");
        case kPageWindow: return Tr(L"How the chat window looks and what it shows.");
        default: return Tr(L"Modules, diagnostics, lists – for the curious.");
    }
}
}  // namespace

// Our options window: the pages in the side bar, the one chosen beside it. Everything applies at once.
void RenderOptionsContent() {
    static Config edit, opened;  // `opened`: the settings as this window took them; what differs in `edit` is applied
    static bool loaded = false;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        if (!loaded || !OptionsEdited(edit, opened)) edit = opened = g_cfg;  // what the footer and the menus changed too
        loaded = true;
    }
    const float fs = ImGui::GetFontSize();
    g_labelW = std::clamp(s_labelNeed + fs * 0.8f, fs * 6.0f, fs * 14.0f);
    s_labelNeed = 0.0f;
    if (s_delayedTab >= 0) {
        g_optionsTab = s_delayedTab;
        s_delayedTab = -1;
    }
    static int s_shown = -1;
    if (g_optionsTab >= 0 && g_optionsTab < kPageCount) s_page = g_optionsTab;
    g_optionsTab = -1;
    const bool entered = s_page != s_shown;
    s_shown = s_page;

    // The side bar: a symbol and a name per page, as wide as the longest name.
    std::string names[kPageCount];
    float longest = 0.0f;
    for (int i = 0; i < kPageCount; ++i) {
        names[i] = UI(PageName(i));
        longest = std::max(longest, ImGui::CalcTextSize(names[i].c_str()).x);
    }
    const float sideW = std::ceil(longest + fs * 3.8f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 70));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 0.35f, fs * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(fs * 0.3f, fs * 0.15f));
    if (ImGui::BeginChild("##side", ImVec2(sideW, 0.0f), false,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysUseWindowPadding)) {
        for (int i = 0; i < kPageCount; ++i)
            if (NavItem(i, names[i], s_page == i)) s_page = i;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(fs * 1.0f, fs * 0.6f));
    if (ImGui::BeginChild("##page", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysUseWindowPadding)) {
        ImGui::PushTextWrapPos(0.0f);  // a narrow window: every text of the page wraps at its edge, none is cut off
        PageHead(names[s_page], UI(PageSub(s_page)));
        switch (s_page) {
            case kPageStart: RenderStartPage(edit); break;
            case kPageChat: RenderChatPage(edit); break;
            case kPageTyping: RenderTypingPage(edit); break;
            case kPageVoice: RenderVoicePage(edit, entered); break;
            case kPageTranslator: RenderTranslatorPage(edit); break;
            case kPageWindow: RenderWindowPage(edit); break;
            default: RenderTechnicalPage(edit); break;
        }
        while (!s_boxes.empty()) EndBox();  // never a frame left open
        ImGui::PopTextWrapPos();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    // Applied once you let go: not while a slider is held or a field is being written in.
    const ImGuiContext& g = *GImGui;
    const bool editing = g.ActiveId != 0 && g.ActiveIdWindow && g.ActiveIdWindow->RootWindow == ImGui::GetCurrentWindow()->RootWindow;
    if (!editing && OptionsEdited(edit, opened)) ApplyOptions(edit, opened);
}
#undef KB_OPTIONS
#undef KB_VOICE_OPTIONS

// Nexus → Addons → GW2 Keyboard: only the button that opens / closes our own options window.
void RenderNexusOptions() {
    if (!ImGui::GetCurrentContext()) return;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    if (g.CurrentWindowStack.Size <= 1) {
        SuppressImplicitDebugWindow();
        return;
    }
    ImGuiWindow* cur = ImGui::GetCurrentWindowRead();
    if (!cur || cur->IsFallbackWindow || (cur->Name && (std::strcmp(cur->Name, "Debug##Default") == 0 || std::strcmp(cur->Name, "Debug") == 0))) {
        SuppressImplicitDebugWindow();
        return;
    }
    if (ImGui::GetFont() && ImGui::GetFontSize() > 0.0f)
        s_nexusOptionsFontPx.store(ImGui::GetFontSize());  // includes Nexus' per-window scale and the active Windows/game DPI scale
    if (ImGui::Button(U8(g_showOptions ? Tr(L"Close the GW2Chatlator options") : Tr(L"Open the GW2Chatlator options")).c_str()))
        g_showOptions = !g_showOptions;
    // The chat window back without a key of yours (none is set by us): closed with its X, hidden from the menu.
    if (ImGui::Button(U8(g_show ? Tr(L"Hide the chat window") : Tr(L"Show the chat window")).c_str())) OnToggle(nullptr, false);
}

void SuppressImplicitDebugWindow() {
    if (!ImGui::GetCurrentContext()) return;
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (int i = 0; i < g.Windows.Size; ++i) {
        ImGuiWindow* w = g.Windows[i];
        if (!w) continue;
        const bool isFallback = w->IsFallbackWindow ||
                                (w->Name && (std::strcmp(w->Name, "Debug##Default") == 0 || std::strcmp(w->Name, "Debug") == 0));
        if (isFallback) {
            w->Active = false;
            w->WasActive = false;
            w->WriteAccessed = false;
            w->Hidden = true;
            w->HiddenFramesCanSkipItems = 10;
            w->HiddenFramesCannotSkipItems = 10;
            w->Flags |= ImGuiWindowFlags_NoSavedSettings;
        }
    }
    if (g.CurrentWindow && (g.CurrentWindow->IsFallbackWindow ||
        (g.CurrentWindow->Name && (std::strcmp(g.CurrentWindow->Name, "Debug##Default") == 0 || std::strcmp(g.CurrentWindow->Name, "Debug") == 0)))) {
        g.CurrentWindow->WriteAccessed = false;
        g.CurrentWindow->Active = false;
    }
    RenderDiscordVoiceMenu();
}

}  // namespace kb
