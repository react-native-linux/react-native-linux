#include "ToplevelState.h"

namespace react_native_linux {

namespace {

constexpr uint32_t kXdgToplevelStateMaximized = 1;
constexpr uint32_t kXdgToplevelStateFullscreen = 2;
constexpr uint32_t kXdgToplevelStateResizing = 3;
constexpr uint32_t kXdgToplevelStateActivated = 4;

} // namespace

ToplevelState decodeToplevelStates(const uint32_t* states, size_t count) noexcept {
    ToplevelState decoded;

    for (size_t index = 0; index < count; ++index) {
        switch (states[index]) {
        case kXdgToplevelStateActivated:
            decoded.activated = true;
            break;
        case kXdgToplevelStateMaximized:
            decoded.maximized = true;
            break;
        case kXdgToplevelStateFullscreen:
            decoded.fullscreen = true;
            break;
        case kXdgToplevelStateResizing:
            decoded.resizing = true;
            break;
        default:
            break;
        }
    }

    return decoded;
}

} // namespace react_native_linux
