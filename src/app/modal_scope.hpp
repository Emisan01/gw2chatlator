// modal_scope.hpp — while one of our own dialogs, menus or message boxes is
// open (and shortly after), the reader's pictures are not used: they may show
// our own window (invariant 5 – never read ourselves, never send our own
// settings to a translator). Dialogs are also excluded from capture; message
// boxes and menus cannot be, so this guard covers them. UI thread only.
#pragma once

#include <windows.h>

namespace gct {

class ModalScope {
public:
    ModalScope() {
        ++depth_;
        lastChange_ = GetTickCount64();
    }
    ~ModalScope() {
        --depth_;
        lastChange_ = GetTickCount64();
    }
    ModalScope(const ModalScope&) = delete;
    ModalScope& operator=(const ModalScope&) = delete;

    static bool Active() { return depth_ > 0; }
    // A picture taken at `tick` may show one of our windows: one is open, or it closed less than 400 ms before.
    static bool MayShowOurWindow(ULONGLONG tick) { return depth_ > 0 || tick < lastChange_ + 400; }

private:
    static inline int depth_ = 0;
    static inline ULONGLONG lastChange_ = 0;
};

}  // namespace gct
