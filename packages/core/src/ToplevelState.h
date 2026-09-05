#pragma once

#include <cstddef>
#include <cstdint>

namespace react_native_linux {

/**
 * The desktop lifecycle bits `xdg_toplevel.configure` reports, decoded from the raw `wl_array` of
 * `enum xdg_toplevel_state` values the protocol sends.
 *
 * Only the states this platform's contract names (#218) are kept: `activated`, `maximized` and `fullscreen`.
 * `resizing` is tracked alongside them because it shares the same array and costs nothing extra to decode; the
 * tiled and suspended states xdg-shell also defines are not, because nothing here has a use for them yet and this
 * struct is not a general mirror of the protocol.
 *
 * A default-constructed `ToplevelState` is every flag false, which is also what the compositor's very first
 * `configure` — before any state has ever been reported — decodes as.
 */
struct ToplevelState {
    bool activated{false};
    bool maximized{false};
    bool fullscreen{false};
    bool resizing{false};

    bool operator==(const ToplevelState&) const = default;
};

/**
 * Decodes a `configure` event's state array. `states` points at `count` raw `uint32_t` values, each one an
 * `enum xdg_toplevel_state` member — the caller has already done the `wl_array` unwrapping, because that type,
 * and the generated header its values come from, only exist in the window build; this function has neither
 * dependency, which is what keeps it in the unit-test coverage gate. See *Window host* in
 * docs/cpp-toolchain.md.
 *
 * The four numeric values below are xdg-shell's own wire values (`xdg-shell.xml`, `xdg_toplevel::state`):
 * MAXIMIZED = 1, FULLSCREEN = 2, RESIZING = 3, ACTIVATED = 4. They are wire constants, frozen by the protocol's
 * own stability guarantee, the same way *Input*'s evdev-keycode offset of 8 is a wire constant rather than
 * something to look up through a generated enum.
 *
 * An unrecognised value — a tiled or suspended state, or anything a future protocol version adds — is silently
 * ignored rather than rejected, because `xdg_toplevel.configure` is allowed to report states this platform does
 * not act on and ignoring them is what forward compatibility means here.
 */
ToplevelState decodeToplevelStates(const uint32_t* states, size_t count) noexcept;

} // namespace react_native_linux
