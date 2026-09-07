#include "ToplevelState.h"

namespace react_native_linux {

namespace {

constexpr uint32_t kXdgToplevelStateMaximized = 1;
constexpr uint32_t kXdgToplevelStateFullscreen = 2;
constexpr uint32_t kXdgToplevelStateResizing = 3;
constexpr uint32_t kXdgToplevelStateActivated = 4;
// xdg-shell.xml, xdg_toplevel::state, added in the protocol's version 2: TILED_LEFT = 5, TILED_RIGHT = 6,
// TILED_TOP = 7, TILED_BOTTOM = 8. SUSPENDED = 9 exists too but nothing here decodes it; see the docblock.
constexpr uint32_t kXdgToplevelStateTiledLeft = 5;
constexpr uint32_t kXdgToplevelStateTiledRight = 6;
constexpr uint32_t kXdgToplevelStateTiledTop = 7;
constexpr uint32_t kXdgToplevelStateTiledBottom = 8;

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
        case kXdgToplevelStateTiledLeft:
            decoded.tiledLeft = true;
            break;
        case kXdgToplevelStateTiledRight:
            decoded.tiledRight = true;
            break;
        case kXdgToplevelStateTiledTop:
            decoded.tiledTop = true;
            break;
        case kXdgToplevelStateTiledBottom:
            decoded.tiledBottom = true;
            break;
        default:
            break;
        }
    }

    return decoded;
}

} // namespace react_native_linux
