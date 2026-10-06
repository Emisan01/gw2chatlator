// fake_gw2.cpp — stand-in for the game, for end-to-end tests without GW2
// (works under Wine too). Reproduces exactly what the translator relies on:
//
//  * a window of class ArenaNet_Gr_Window_Class titled "Guild Wars 2";
//  * the chat line: Enter opens it, Enter again sends it. Every sent line is
//    appended to received.txt, followed one second later by the clipboard
//    content (to verify the translator restored it);
//  * a chat panel in the bottom-left corner that shows the last 10 lines of
//    fake_chat.txt ("RRGGBB|text") in their colour — tests append lines to
//    that file, and what you send is echoed into it like GW2 echoes your own
//    messages (fake_ocr.cpp reads the same file);
//  * a MumbleLink block (uiVersion 2, identity name "Emi Tester", ticking,
//    GameHasFocus / TextboxHasFocus bits, processId), like the real client.
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr wchar_t kOwnName[] = L"Emi Tester";
constexpr int kPanelW = 420, kPanelLines = 10, kLinePitch = 16;

HWND g_main = nullptr, g_edit = nullptr;
WNDPROC g_editOrig = nullptr;
HFONT g_font = nullptr;
std::string g_chatData;
std::wstring g_lastWhisperer = L"Some Player";

// ---------------------------------------------------------------------------
// MumbleLink (layout: GW2 wiki, API:MumbleLink)
struct LinkedMem {
    uint32_t uiVersion;
    uint32_t uiTick;
    float fAvatarPosition[3];
    float fAvatarFront[3];
    float fAvatarTop[3];
    wchar_t name[256];
    float fCameraPosition[3];
    float fCameraFront[3];
    float fCameraTop[3];
    wchar_t identity[256];
    uint32_t context_len;
    unsigned char context[256];
    wchar_t description[2048];
};
constexpr size_t kUiStateOffset = 48, kProcessIdOffset = 80;
constexpr uint32_t kGameHasFocus = 8, kTextboxHasFocus = 32;

LinkedMem* g_link = nullptr;

void InitMumble() {
    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(LinkedMem), L"MumbleLink");
    if (!map) return;
    g_link = static_cast<LinkedMem*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(LinkedMem)));
    if (!g_link) return;
    std::memset(g_link, 0, sizeof(LinkedMem));
    g_link->uiVersion = 2;
    wcscpy(g_link->name, L"Guild Wars 2");
    wcscpy(g_link->identity,
           L"{\"name\":\"Emi Tester\",\"profession\":4,\"spec\":0,\"race\":1,\"map_id\":50,\"world_id\":0,"
           L"\"team_color_id\":0,\"commander\":false,\"fov\":0.873,\"uisz\":1}");
    g_link->context_len = 88;
    const uint32_t pid = GetCurrentProcessId();
    std::memcpy(g_link->context + kProcessIdOffset, &pid, sizeof(pid));
}

void TickMumble() {
    if (!g_link) return;
    ++g_link->uiTick;
    uint32_t state = 0;
    if (GetForegroundWindow() == g_main) state |= kGameHasFocus;
    if (IsWindowVisible(g_edit)) state |= kTextboxHasFocus;
    std::memcpy(g_link->context + kUiStateOffset, &state, sizeof(state));
}

// ---------------------------------------------------------------------------
std::string ToUtf8(const std::wstring& s) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring FromUtf8(const std::string& s) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

void AppendFile(const wchar_t* path, const std::string& utf8) {
    HANDLE f = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w = 0;
    WriteFile(f, utf8.data(), static_cast<DWORD>(utf8.size()), &w, nullptr);
    CloseHandle(f);
}

void Log(const std::wstring& line) { AppendFile(L"received.txt", ToUtf8(line) + "\n"); }

std::string ReadAll(const wchar_t* path) {
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return {};
    std::string data;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(f, buf, sizeof(buf), &got, nullptr) && got > 0) data.append(buf, got);
    CloseHandle(f);
    return data;
}

// Your own message, the way GW2 shows it in the chat.
void EchoOwnLine(const std::wstring& line) {
    std::wstring color = L"7DDC69", text = line;  // say
    auto starts = [&](const wchar_t* p) { return text.compare(0, wcslen(p), p) == 0; };
    if (starts(L"/p ")) {
        color = L"6EAFFF";
        text = std::wstring(kOwnName) + L": " + text.substr(3);
    } else if (starts(L"/m ")) {
        color = L"F0A59B";
        text = std::wstring(kOwnName) + L": " + text.substr(3);
    } else if (starts(L"/g ")) {
        color = L"F5C850";
        text = std::wstring(kOwnName) + L": " + text.substr(3);
    } else if (starts(L"/r ")) {
        color = L"C88CFF";
        text = L"To " + g_lastWhisperer + L": " + text.substr(3);
    } else if (starts(L"/w ")) {
        color = L"C88CFF";
        const size_t comma = text.find(L',');
        const std::wstring name = comma == std::wstring::npos ? L"?" : text.substr(3, comma - 3);
        const std::wstring body = comma == std::wstring::npos ? text.substr(3) : text.substr(comma + 1);
        text = L"To " + name + L":" + body;
    } else if (starts(L"/")) {
        return;  // emotes and other commands print nothing here
    } else {
        text = std::wstring(kOwnName) + L": " + text;
    }
    AppendFile(L"fake_chat.txt", ToUtf8(color + L"|" + text) + "\n");
}

std::wstring ClipboardText() {
    std::wstring s = L"(leer)";
    if (!OpenClipboard(g_main)) return L"(blockiert)";
    if (HANDLE h = GetClipboardData(CF_UNICODETEXT)) {
        if (auto* p = static_cast<const wchar_t*>(GlobalLock(h))) {
            s = p;
            GlobalUnlock(h);
        }
    }
    CloseClipboard();
    return s;
}

void SetClipboard(const std::wstring& s) {
    if (!OpenClipboard(g_main)) return;
    EmptyClipboard();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (s.size() + 1) * sizeof(wchar_t));
    memcpy(GlobalLock(g), s.c_str(), (s.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(g);
    SetClipboardData(CF_UNICODETEXT, g);
    CloseClipboard();
}

void PaintChat(HWND h) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    HBRUSH bg = CreateSolidBrush(RGB(40, 44, 52));
    FillRect(dc, &rc, bg);
    DeleteObject(bg);

    // Chat panel: bottom-left, above the chat line.
    RECT panel{8, rc.bottom - 44 - kPanelLines * kLinePitch, 8 + kPanelW, rc.bottom - 40};
    HBRUSH pb = CreateSolidBrush(RGB(12, 12, 14));
    FillRect(dc, &panel, pb);
    DeleteObject(pb);

    std::vector<std::wstring> lines;
    const std::wstring all = FromUtf8(g_chatData);
    size_t start = 0;
    while (start < all.size()) {
        size_t end = all.find(L'\n', start);
        if (end == std::wstring::npos) end = all.size();
        std::wstring l = all.substr(start, end - start);
        if (!l.empty() && l.back() == L'\r') l.pop_back();
        if (l.size() > 7 && l[6] == L'|') lines.push_back(l);
        start = end + 1;
    }
    const size_t first = lines.size() > kPanelLines ? lines.size() - kPanelLines : 0;
    HGDIOBJ old = SelectObject(dc, g_font);
    SetBkMode(dc, TRANSPARENT);
    int y = panel.top + 2;
    for (size_t i = first; i < lines.size(); ++i, y += kLinePitch) {
        const unsigned long c = wcstoul(lines[i].substr(0, 6).c_str(), nullptr, 16);
        SetTextColor(dc, RGB((c >> 16) & 255, (c >> 8) & 255, c & 255));
        const std::wstring text = lines[i].substr(7);
        RECT r{panel.left + 4, y, panel.right - 4, y + kLinePitch};
        DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &r, DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    }
    SelectObject(dc, old);
    EndPaint(h, &ps);
}

LRESULT CALLBACK EditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_KEYDOWN && wp == VK_RETURN) {
        wchar_t buf[512] = {};
        GetWindowTextW(h, buf, 512);
        Log(std::wstring(L"chat: ") + buf);
        if (buf[0]) EchoOwnLine(buf);
        SetWindowTextW(h, L"");
        ShowWindow(h, SW_HIDE);
        SetFocus(g_main);
        SetTimer(g_main, 1, 1000, nullptr);
        return 0;
    }
    if (msg == WM_CHAR && wp == L'\r') return 0;
    return CallWindowProcW(g_editOrig, h, msg, wp, lp);
}

LRESULT CALLBACK MainProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_KEYDOWN:
            if (wp == VK_RETURN && !IsWindowVisible(g_edit)) {
                ShowWindow(g_edit, SW_SHOW);
                SetFocus(g_edit);
            }
            return 0;
        case WM_SIZE: {
            RECT rc;
            GetClientRect(h, &rc);
            MoveWindow(g_edit, 8, rc.bottom - 34, kPanelW, 26, TRUE);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            PaintChat(h);
            return 0;
        case WM_TIMER:
            if (wp == 1) {
                KillTimer(h, 1);
                Log(L"clipboard-after: " + ClipboardText());
            } else if (wp == 2) {
                TickMumble();
                std::string data = ReadAll(L"fake_chat.txt");
                if (data != g_chatData) {
                    g_chatData = std::move(data);
                    InvalidateRect(h, nullptr, FALSE);
                }
            }
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"ArenaNet_Gr_Window_Class";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&wc);
    g_font = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, L"Tahoma");
    g_main = CreateWindowExW(0, wc.lpszClassName, L"Guild Wars 2", WS_OVERLAPPEDWINDOW, 560, 60, 700, 420, nullptr,
                             nullptr, inst, nullptr);
    g_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL, 8, 300, kPanelW, 26, g_main,
                             nullptr, inst, nullptr);
    g_editOrig = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_edit, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(EditProc)));
    ShowWindow(g_main, SW_SHOW);
    InitMumble();
    SetClipboard(L"ORIGINAL-ZWISCHENABLAGE");
    SetTimer(g_main, 2, 100, nullptr);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
