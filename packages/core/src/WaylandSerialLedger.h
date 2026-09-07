#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace react_native_linux {

/**
 * Every Wayland request that needs a serial — `wl_data_device.set_selection`, `wl_data_device.start_drag`,
 * `xdg_toplevel.move`/`resize`/`show_window_menu`, `wl_surface.ack_configure`, `wl_pointer.set_cursor` — is
 * silently declined by the compositor when given the wrong one, and #330's zed issues are five different shapes
 * of that one mistake. This ledger is the single place a serial is written down, so every request reads the
 * answer from here instead of whichever listener argument happened to still be in scope.
 *
 * Two rules, both learned the hard way by GPUI (`crates/gpui_linux/src/linux/wayland/serial.rs`):
 *
 * - **Only a press updates its kind.** `wl_pointer.button` and `wl_keyboard.key` both fire on release too, and a
 *   popup grab or an interactive move given a release serial is declined — recording a release would make the
 *   very next legitimate request fail with the release serial still sitting in the ledger.
 * - **The merged kinds (`InteractiveMove`, `Selection`) are tracked in arrival order, not by numeric comparison.**
 *   `uint32_t` wraps, so "the larger serial" is not "the more recent" one once it does; every `record*` call here
 *   overwrites unconditionally, in the order the compositor's events actually arrived, which is arrival order by
 *   construction and needs no comparison to get right.
 *
 * `Configure` and the text-input kinds are deliberately not merged with anything: `xdg_surface.ack_configure`
 * needs exactly the serial its own `configure` carried, and `zwp_text_input_v3` tracks its own commit count
 * inside `TextInputV3State`, a wholly separate protocol counter this ledger has no business touching.
 *
 * This struct is intentionally libwayland-free — every member is a `uint32_t` and every argument comes already
 * unwrapped from a listener callback — so it sits in the 100% line-and-branch coverage gate the same way
 * `ToplevelState` does. See *Window host* in docs/cpp-toolchain.md.
 *
 * Threading contract: not synchronised. Every caller today runs on the frame thread that owns the Wayland
 * connection, the same contract `WaylandSeat` and `WaylandWindow` state.
 */
enum class WaylandSerialKind : uint8_t {
    PointerEnter,
    PointerButtonPress,
    KeyboardEnter,
    KeyboardKeyPress,
    TouchDown,
    Configure,
    /** `xdg_toplevel.move`, `.resize`, `.show_window_menu`: the latest pointer button or touch press. */
    InteractiveMove,
    /** `wl_data_device.set_selection`, `.start_drag`: the latest press of any kind, pointer, touch or keyboard. */
    Selection,
};

class WaylandSerialLedger final {
public:
    void recordPointerEnter(uint32_t serial) noexcept;
    /** `pressed` is `state == WL_POINTER_BUTTON_STATE_PRESSED`; a release is observed and otherwise ignored. */
    void recordPointerButton(uint32_t serial, bool pressed) noexcept;
    void recordKeyboardEnter(uint32_t serial) noexcept;
    /** `pressed` is `state == WL_KEYBOARD_KEY_STATE_PRESSED`; a release is observed and otherwise ignored. */
    void recordKeyboardKey(uint32_t serial, bool pressed) noexcept;
    void recordTouchDown(uint32_t serial) noexcept;
    void recordConfigure(uint32_t serial) noexcept;

    /** The most recently recorded serial for `kind`, or zero if nothing has recorded it yet. */
    uint32_t serial(WaylandSerialKind kind) const noexcept;

    /**
     * The serial a request for `kind` must send, or `std::nullopt` — logged with the reason a caller can grep
     * for — if nothing has recorded that kind yet. A request built from a zero serial fails exactly as silently
     * as one built from a stale one, so a caller that has not seen a qualifying event yet must not send the
     * request at all rather than send it with zero.
     */
    std::optional<uint32_t> requestSerial(WaylandSerialKind kind) const;

private:
    static constexpr size_t kKindCount = 8;

    std::array<uint32_t, kKindCount> serials_{};
    std::array<bool, kKindCount> recorded_{};
};

/** The name a log line and a test failure message refer to a kind by. */
std::string_view nameOfSerialKind(WaylandSerialKind kind) noexcept;

} // namespace react_native_linux
