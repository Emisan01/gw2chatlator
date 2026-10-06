// mumble_link.cpp — layout from the GW2 wiki (API:MumbleLink).
#include "mumble_link.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace gct {
namespace {

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

struct MumbleContext {
    unsigned char serverAddress[28];
    uint32_t mapId;
    uint32_t mapType;
    uint32_t shardId;
    uint32_t instance;
    uint32_t buildId;
    uint32_t uiState;
    uint16_t compassWidth;
    uint16_t compassHeight;
    float compassRotation;
    float playerX;
    float playerY;
    float mapCenterX;
    float mapCenterY;
    float mapScale;
    uint32_t processId;
    uint8_t mountIndex;
};

static_assert(sizeof(wchar_t) == 2, "MumbleLink uses UTF-16");
static_assert(offsetof(MumbleContext, uiState) == 48, "context layout");
static_assert(offsetof(MumbleContext, processId) == 80, "context layout");

}  // namespace

MumbleLink::~MumbleLink() {
    if (view_) UnmapViewOfFile(view_);
    if (map_) CloseHandle(map_);
}

bool MumbleLink::Open() {
    if (view_) return true;
    // Only open, never create: if GW2 is not running there is nothing to read.
    map_ = OpenFileMappingW(FILE_MAP_READ, FALSE, name_.c_str());
    if (!map_) return false;
    view_ = MapViewOfFile(map_, FILE_MAP_READ, 0, 0, sizeof(LinkedMem));
    if (!view_) {
        CloseHandle(map_);
        map_ = nullptr;
        return false;
    }
    return true;
}

MumbleState MumbleLink::Read() {
    MumbleState st;
    if (!Open()) return st;

    LinkedMem mem;
    std::memcpy(&mem, view_, sizeof(mem));
    if (mem.uiVersion != 2) return st;

    // GW2 bumps uiTick every frame; a frozen tick means the game is gone
    // (our open handle keeps the old block alive) or sits in a loading screen.
    const ULONGLONG now = GetTickCount64();
    if (mem.uiTick != lastTick_) {
        lastTick_ = mem.uiTick;
        lastTickChange_ = now;
    }
    const bool fresh = now - lastTickChange_ < 3000;

    MumbleContext ctx;
    std::memcpy(&ctx, mem.context, sizeof(ctx));
    mem.identity[255] = L'\0';
    st.identity = ParseMumbleIdentity(mem.identity);
    st.uiState = ctx.uiState;
    st.processId = ctx.processId;

    if (!fresh && ctx.processId) {  // distinguish "loading screen" from "game closed"
        HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ctx.processId);
        DWORD code = 0;
        const bool running = p && GetExitCodeProcess(p, &code) && code == STILL_ACTIVE;
        if (p) CloseHandle(p);
        if (!running) {  // drop the stale block so a new GW2 start is picked up
            UnmapViewOfFile(view_);
            CloseHandle(map_);
            view_ = nullptr;
            map_ = nullptr;
            return MumbleState{};
        }
    }
    st.live = !st.identity.name.empty();
    return st;
}

}  // namespace gct
