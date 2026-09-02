#pragma once

#include "InputPipeline.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

struct wl_array;
struct wl_keyboard;
struct wl_keyboard_listener;
struct wl_pointer;
struct wl_pointer_listener;
struct wl_seat;
struct wl_seat_listener;
struct wl_surface;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;
struct zwp_text_input_manager_v3;

namespace react_native_linux {

class TextInputClient;

/**
 * The lowest `wl_seat` version this binds. `wl_pointer.frame` and the `release` requests that let a client drop a
 * pointer or keyboard without destroying the seat both arrive at 5, and a compositor that advertises less is not
 * one this platform targets, so the seat is simply not bound rather than special-cased.
 */
constexpr uint32_t kMinimumSeatVersion = 5;

/**
 * One `wl_seat`: a mouse pointer, a keyboard, and the queue of platform-neutral events they produce.
 *
 * Scope is ADR-0001's minimal surface for M1 and nothing else — pointer motion, buttons, and keys with modifiers.
 * Touch is not bound, axis events are queued raw for the scroll pipeline to interpret, and key repeat is not
 * synthesised from `wl_keyboard.repeat_info`.
 *
 * Text composition hangs off the same seat, because `zwp_text_input_v3` is created for one: `attachTextInput`
 * builds a `TextInputClient` when the compositor advertises the manager, and its events land on this queue in
 * the same order as everything else. See *IME* in docs/cpp-toolchain.md.
 *
 * Keysym translation is xkbcommon's, from the keymap the compositor sends over a file descriptor: the same library
 * every Wayland toolkit uses, and the one compose sequences and dead keys will need when they land here.
 * Coordinates arrive as `wl_fixed_t` in surface-local space, which is already the coordinate space Fabric lays
 * out in.
 *
 * Threading contract: every member runs on the thread that owns the Wayland connection — the frame thread. The
 * listeners here are invoked from inside that thread's `wl_display_dispatch_pending`, so the queue they fill and
 * `takeEvents` that empties it need no lock.
 */
class WaylandSeat final {
public:
    explicit WaylandSeat(wl_seat* seat);
    WaylandSeat(const WaylandSeat&) = delete;
    WaylandSeat(WaylandSeat&&) = delete;
    WaylandSeat& operator=(const WaylandSeat&) = delete;
    WaylandSeat& operator=(WaylandSeat&&) = delete;
    ~WaylandSeat() noexcept;

    std::vector<InputEvent> takeEvents();
    size_t droppedEventCount() const noexcept;
    void attachTextInput(zwp_text_input_manager_v3* manager);
    TextInputClient* textInput() const noexcept;

private:
    void updateCapabilities(uint32_t capabilities);
    void loadKeymap(uint32_t format, int32_t keymapDescriptor, uint32_t size);
    void updateModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
    void pushPointerPosition(InputEventKind kind, int32_t surfaceX, int32_t surfaceY);
    void pushPointerButton(uint32_t button, uint32_t state);
    void pushPointerLeave();
    void pushKey(uint32_t key, uint32_t state);
    void releasePointer() noexcept;
    void releaseKeyboard() noexcept;

    static wl_seat_listener makeSeatListener();
    static wl_pointer_listener makePointerListener();
    static wl_keyboard_listener makeKeyboardListener();

    static const wl_seat_listener kSeatListener;
    static const wl_pointer_listener kPointerListener;
    static const wl_keyboard_listener kKeyboardListener;

    static void handleSeatCapabilities(void* data, wl_seat* seat, uint32_t capabilities);
    static void handleSeatName(void* data, wl_seat* seat, const char* name);
    static void handlePointerEnter(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface,
                                   int32_t surfaceX, int32_t surfaceY);
    static void handlePointerLeave(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface);
    static void handlePointerMotion(void* data, wl_pointer* pointer, uint32_t time, int32_t surfaceX, int32_t surfaceY);
    static void handlePointerButton(void* data, wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button,
                                    uint32_t state);
    static void handlePointerAxis(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis, int32_t value);
    static void handlePointerFrame(void* data, wl_pointer* pointer);
    static void handlePointerAxisSource(void* data, wl_pointer* pointer, uint32_t axisSource);
    static void handlePointerAxisStop(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis);
    static void handlePointerAxisDiscrete(void* data, wl_pointer* pointer, uint32_t axis, int32_t discrete);
    static void handleKeyboardKeymap(void* data, wl_keyboard* keyboard, uint32_t format, int32_t keymapDescriptor,
                                     uint32_t size);
    static void handleKeyboardEnter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface,
                                    wl_array* keys);
    static void handleKeyboardLeave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface);
    static void handleKeyboardKey(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key,
                                  uint32_t state);
    static void handleKeyboardModifiers(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t depressed,
                                        uint32_t latched, uint32_t locked, uint32_t group);
    static void handleKeyboardRepeatInfo(void* data, wl_keyboard* keyboard, int32_t rate, int32_t delay);

    wl_seat* seat_{nullptr};
    wl_pointer* pointer_{nullptr};
    wl_keyboard* keyboard_{nullptr};
    xkb_context* xkbContext_{nullptr};
    xkb_keymap* keymap_{nullptr};
    xkb_state* keyboardState_{nullptr};
    InputQueue queue_;
    InputModifiers modifiers_;
    facebook::react::Point pointerPosition_{};
    std::unique_ptr<TextInputClient> textInput_;
};

} // namespace react_native_linux
