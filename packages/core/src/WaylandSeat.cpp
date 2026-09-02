#include "WaylandSeat.h"

#include "TextInputClient.h"

#include <sys/mman.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>
#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

namespace react_native_linux {

namespace {

constexpr int kPrimaryButton = 0;
constexpr int kAuxiliaryButton = 1;
constexpr int kSecondaryButton = 2;
constexpr int kUnmappedButton = -1;
constexpr size_t kKeysymNameCapacity = 64;
// The longest text one key can produce is one UTF-8 code point, which is four bytes, plus the terminator
// xkb_state_key_get_utf8 always writes.
constexpr size_t kKeyTextCapacity = 8;
// libxkbcommon reports a key as pressed with the evdev keycode the kernel used plus this offset, which is the X11
// keycode convention every keymap in the wild is written against.
constexpr uint32_t kEvdevToXkbKeycodeOffset = 8;

int toDomButton(uint32_t waylandButton) {
    switch (waylandButton) {
        case BTN_LEFT:
            return kPrimaryButton;
        case BTN_MIDDLE:
            return kAuxiliaryButton;
        case BTN_RIGHT:
            return kSecondaryButton;
        default:
            return kUnmappedButton;
    }
}

std::string keysymName(xkb_state* keyboardState, uint32_t keycode) {
    const xkb_keysym_t keysym = xkb_state_key_get_one_sym(keyboardState, keycode);
    std::array<char, kKeysymNameCapacity> name{};
    const int written = xkb_keysym_get_name(keysym, name.data(), name.size());

    if (written <= 0) {
        return {};
    }

    return std::string(name.data(), static_cast<size_t>(written));
}

/**
 * The text this key produces under the modifiers currently held, which is one half of the DOM `key` value.
 * A key that produces none — every named key, and every modifier — reports nothing rather than a control
 * character; `domKeyName` is what decides between the two.
 */
std::string keyText(xkb_state* keyboardState, uint32_t keycode) {
    std::array<char, kKeyTextCapacity> text{};
    const int written = xkb_state_key_get_utf8(keyboardState, keycode, text.data(), text.size());

    if (written <= 0 || static_cast<size_t>(written) >= text.size()) {
        return {};
    }

    return std::string(text.data(), static_cast<size_t>(written));
}

bool isModifierActive(xkb_state* keyboardState, const char* modifierName) {
    return xkb_state_mod_name_is_active(keyboardState, modifierName, XKB_STATE_MODS_EFFECTIVE) > 0;
}

} // namespace

wl_seat_listener WaylandSeat::makeSeatListener() {
    wl_seat_listener listener{};

    listener.capabilities = WaylandSeat::handleSeatCapabilities;
    listener.name = WaylandSeat::handleSeatName;

    return listener;
}

wl_pointer_listener WaylandSeat::makePointerListener() {
    // Value-initialised and then filled member by member rather than with a designated initialiser: the struct
    // grows a member with every wl_pointer version libwayland learns, and naming them all would tie this file to
    // one libwayland release. Everything wl_pointer version 5 can send is assigned here; the rest stay null and
    // are unreachable because the bound version is what decides which events a compositor may send.
    wl_pointer_listener listener{};

    listener.enter = WaylandSeat::handlePointerEnter;
    listener.leave = WaylandSeat::handlePointerLeave;
    listener.motion = WaylandSeat::handlePointerMotion;
    listener.button = WaylandSeat::handlePointerButton;
    listener.axis = WaylandSeat::handlePointerAxis;
    listener.frame = WaylandSeat::handlePointerFrame;
    listener.axis_source = WaylandSeat::handlePointerAxisSource;
    listener.axis_stop = WaylandSeat::handlePointerAxisStop;
    listener.axis_discrete = WaylandSeat::handlePointerAxisDiscrete;

    return listener;
}

wl_keyboard_listener WaylandSeat::makeKeyboardListener() {
    wl_keyboard_listener listener{};

    listener.keymap = WaylandSeat::handleKeyboardKeymap;
    listener.enter = WaylandSeat::handleKeyboardEnter;
    listener.leave = WaylandSeat::handleKeyboardLeave;
    listener.key = WaylandSeat::handleKeyboardKey;
    listener.modifiers = WaylandSeat::handleKeyboardModifiers;
    listener.repeat_info = WaylandSeat::handleKeyboardRepeatInfo;

    return listener;
}

const wl_seat_listener WaylandSeat::kSeatListener = WaylandSeat::makeSeatListener();

const wl_pointer_listener WaylandSeat::kPointerListener = WaylandSeat::makePointerListener();

const wl_keyboard_listener WaylandSeat::kKeyboardListener = WaylandSeat::makeKeyboardListener();

WaylandSeat::WaylandSeat(wl_seat* seat) : seat_(seat), xkbContext_(xkb_context_new(XKB_CONTEXT_NO_FLAGS)) {
    wl_seat_add_listener(seat_, &kSeatListener, this);
}

WaylandSeat::~WaylandSeat() noexcept {
    // Before the seat proxy goes away: destroying a zwp_text_input_v3 is a request that names it.
    textInput_.reset();
    releasePointer();
    releaseKeyboard();

    if (keyboardState_ != nullptr) {
        xkb_state_unref(keyboardState_);
    }

    if (keymap_ != nullptr) {
        xkb_keymap_unref(keymap_);
    }

    if (xkbContext_ != nullptr) {
        xkb_context_unref(xkbContext_);
    }

    if (seat_ != nullptr) {
        wl_seat_release(seat_);
    }
}

std::vector<InputEvent> WaylandSeat::takeEvents() { return queue_.drain(); }

size_t WaylandSeat::droppedEventCount() const noexcept { return queue_.droppedEventCount(); }

void WaylandSeat::updateCapabilities(uint32_t capabilities) {
    const bool hasPointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
    const bool hasKeyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;

    if (hasPointer && pointer_ == nullptr) {
        pointer_ = wl_seat_get_pointer(seat_);
        wl_pointer_add_listener(pointer_, &kPointerListener, this);
    } else if (!hasPointer) {
        releasePointer();
    }

    if (hasKeyboard && keyboard_ == nullptr) {
        keyboard_ = wl_seat_get_keyboard(seat_);
        wl_keyboard_add_listener(keyboard_, &kKeyboardListener, this);
    } else if (!hasKeyboard) {
        releaseKeyboard();
    }
}

void WaylandSeat::loadKeymap(uint32_t format, int32_t keymapDescriptor, uint32_t size) {
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || xkbContext_ == nullptr) {
        close(keymapDescriptor);

        return;
    }

    void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, keymapDescriptor, 0);

    if (mapped != MAP_FAILED) {
        xkb_keymap* keymap = xkb_keymap_new_from_string(xkbContext_, static_cast<const char*>(mapped),
                                                        XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);

        munmap(mapped, size);

        if (keymap != nullptr) {
            if (keyboardState_ != nullptr) {
                xkb_state_unref(keyboardState_);
            }

            if (keymap_ != nullptr) {
                xkb_keymap_unref(keymap_);
            }

            keymap_ = keymap;
            keyboardState_ = xkb_state_new(keymap_);
        }
    }

    close(keymapDescriptor);
}

void WaylandSeat::updateModifiers(uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    if (keyboardState_ == nullptr) {
        return;
    }

    xkb_state_update_mask(keyboardState_, depressed, latched, locked, 0, 0, group);

    modifiers_.control = isModifierActive(keyboardState_, XKB_MOD_NAME_CTRL);
    modifiers_.shift = isModifierActive(keyboardState_, XKB_MOD_NAME_SHIFT);
    modifiers_.alt = isModifierActive(keyboardState_, XKB_MOD_NAME_ALT);
    modifiers_.meta = isModifierActive(keyboardState_, XKB_MOD_NAME_LOGO);
}

void WaylandSeat::pushPointerPosition(InputEventKind kind, int32_t surfaceX, int32_t surfaceY) {
    pointerPosition_ = facebook::react::Point{.x = static_cast<facebook::react::Float>(wl_fixed_to_double(surfaceX)),
                                              .y = static_cast<facebook::react::Float>(wl_fixed_to_double(surfaceY))};

    queue_.push(InputEvent{.kind = kind, .surfacePoint = pointerPosition_, .modifiers = modifiers_});
}

void WaylandSeat::pushPointerButton(uint32_t button, uint32_t state) {
    const int domButton = toDomButton(button);

    if (domButton == kUnmappedButton) {
        return;
    }

    const InputEventKind kind = state == WL_POINTER_BUTTON_STATE_PRESSED ? InputEventKind::PointerButtonPress
                                                                         : InputEventKind::PointerButtonRelease;

    queue_.push(
        InputEvent{.kind = kind, .surfacePoint = pointerPosition_, .button = domButton, .modifiers = modifiers_});
}

void WaylandSeat::pushPointerLeave() {
    queue_.push(
        InputEvent{.kind = InputEventKind::PointerLeave, .surfacePoint = pointerPosition_, .modifiers = modifiers_});
}

void WaylandSeat::pushKey(uint32_t key, uint32_t state) {
    if (keyboardState_ == nullptr) {
        return;
    }

    const InputEventKind kind =
        state == WL_KEYBOARD_KEY_STATE_PRESSED ? InputEventKind::KeyPress : InputEventKind::KeyRelease;
    const uint32_t xkbKeycode = key + kEvdevToXkbKeycodeOffset;

    // The DOM names are computed here rather than downstream because this is the only place that has an
    // xkb_state; the naming rules themselves are in InputPipeline, where the coverage gate scores them.
    queue_.push(InputEvent{.kind = kind,
                           .surfacePoint = pointerPosition_,
                           .key = domKeyName(keysymName(keyboardState_, xkbKeycode),
                                             keyText(keyboardState_, xkbKeycode)),
                           .code = domKeyCode(key),
                           .modifiers = modifiers_});
}

void WaylandSeat::releasePointer() noexcept {
    if (pointer_ != nullptr) {
        wl_pointer_release(pointer_);
        pointer_ = nullptr;
    }
}

void WaylandSeat::releaseKeyboard() noexcept {
    if (keyboard_ != nullptr) {
        wl_keyboard_release(keyboard_);
        keyboard_ = nullptr;
    }
}

void WaylandSeat::handleSeatCapabilities(void* data, wl_seat* /*seat*/, uint32_t capabilities) {
    static_cast<WaylandSeat*>(data)->updateCapabilities(capabilities);
}

void WaylandSeat::handleSeatName(void* /*data*/, wl_seat* /*seat*/, const char* /*name*/) {}

void WaylandSeat::handlePointerEnter(void* data, wl_pointer* /*pointer*/, uint32_t /*serial*/, wl_surface* /*surface*/,
                                     int32_t surfaceX, int32_t surfaceY) {
    static_cast<WaylandSeat*>(data)->pushPointerPosition(InputEventKind::PointerMotion, surfaceX, surfaceY);
}

void WaylandSeat::handlePointerLeave(void* data, wl_pointer* /*pointer*/, uint32_t /*serial*/,
                                     wl_surface* /*surface*/) {
    static_cast<WaylandSeat*>(data)->pushPointerLeave();
}

void WaylandSeat::handlePointerMotion(void* data, wl_pointer* /*pointer*/, uint32_t /*time*/, int32_t surfaceX,
                                      int32_t surfaceY) {
    static_cast<WaylandSeat*>(data)->pushPointerPosition(InputEventKind::PointerMotion, surfaceX, surfaceY);
}

void WaylandSeat::handlePointerButton(void* data, wl_pointer* /*pointer*/, uint32_t /*serial*/, uint32_t /*time*/,
                                      uint32_t button, uint32_t state) {
    static_cast<WaylandSeat*>(data)->pushPointerButton(button, state);
}

namespace {

/**
 * One scroll event, with the seat's last pointer position and modifier state attached because `wl_pointer` sends
 * neither with an axis event.
 *
 * Wayland's axis value grows as the surface scrolls down or right, which is the direction React Native's
 * `contentOffset` grows in, so the sign passes through untouched. Nothing here decides what a value means: the
 * queue pairs a discrete notch with the `axis` event that duplicates it and the `ScrollController` turns whatever
 * survives into motion, because both of those are arithmetic a unit test can see and this file is not.
 */
InputEvent makeScrollEvent(InputEventKind kind, uint32_t waylandAxis, double amount,
                           facebook::react::Point surfacePoint, InputModifiers modifiers) {
    return InputEvent{.kind = kind,
                      .surfacePoint = surfacePoint,
                      .modifiers = modifiers,
                      .scrollAxis = waylandAxis == WL_POINTER_AXIS_HORIZONTAL_SCROLL ? ScrollAxisKind::Horizontal
                                                                                     : ScrollAxisKind::Vertical,
                      .scrollAmount = amount};
}

} // namespace

void WaylandSeat::handlePointerAxis(void* data, wl_pointer* /*pointer*/, uint32_t /*time*/, uint32_t axis,
                                    int32_t value) {
    WaylandSeat* seat = static_cast<WaylandSeat*>(data);

    seat->queue_.push(makeScrollEvent(InputEventKind::PointerScrollContinuous, axis, wl_fixed_to_double(value),
                                      seat->pointerPosition_, seat->modifiers_));
}

void WaylandSeat::handlePointerFrame(void* /*data*/, wl_pointer* /*pointer*/) {}

// wl_pointer.axis_source distinguishes a wheel from a finger, and nothing here needs it: axis_discrete is what a
// wheel sends and axis_stop is what a finger sends, and those two are already the whole distinction.
void WaylandSeat::handlePointerAxisSource(void* /*data*/, wl_pointer* /*pointer*/, uint32_t /*axisSource*/) {}

void WaylandSeat::handlePointerAxisStop(void* data, wl_pointer* /*pointer*/, uint32_t /*time*/, uint32_t axis) {
    WaylandSeat* seat = static_cast<WaylandSeat*>(data);

    seat->queue_.push(makeScrollEvent(InputEventKind::PointerScrollStop, axis, 0.0, seat->pointerPosition_,
                                      seat->modifiers_));
}

void WaylandSeat::handlePointerAxisDiscrete(void* data, wl_pointer* /*pointer*/, uint32_t axis, int32_t discrete) {
    WaylandSeat* seat = static_cast<WaylandSeat*>(data);

    seat->queue_.push(makeScrollEvent(InputEventKind::PointerScrollDiscrete, axis, discrete,
                                      seat->pointerPosition_, seat->modifiers_));
}

void WaylandSeat::handleKeyboardKeymap(void* data, wl_keyboard* /*keyboard*/, uint32_t format, int32_t keymapDescriptor,
                                       uint32_t size) {
    static_cast<WaylandSeat*>(data)->loadKeymap(format, keymapDescriptor, size);
}

void WaylandSeat::handleKeyboardEnter(void* /*data*/, wl_keyboard* /*keyboard*/, uint32_t /*serial*/,
                                      wl_surface* /*surface*/, wl_array* /*keys*/) {}

void WaylandSeat::handleKeyboardLeave(void* /*data*/, wl_keyboard* /*keyboard*/, uint32_t /*serial*/,
                                      wl_surface* /*surface*/) {}

void WaylandSeat::handleKeyboardKey(void* data, wl_keyboard* /*keyboard*/, uint32_t /*serial*/, uint32_t /*time*/,
                                    uint32_t key, uint32_t state) {
    static_cast<WaylandSeat*>(data)->pushKey(key, state);
}

void WaylandSeat::handleKeyboardModifiers(void* data, wl_keyboard* /*keyboard*/, uint32_t /*serial*/,
                                          uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    static_cast<WaylandSeat*>(data)->updateModifiers(depressed, latched, locked, group);
}

void WaylandSeat::handleKeyboardRepeatInfo(void* /*data*/, wl_keyboard* /*keyboard*/, int32_t /*rate*/,
                                           int32_t /*delay*/) {}

void WaylandSeat::attachTextInput(zwp_text_input_manager_v3* manager) {
    textInput_ = std::make_unique<TextInputClient>(manager, seat_, queue_);
}

TextInputClient* WaylandSeat::textInput() const noexcept { return textInput_.get(); }

} // namespace react_native_linux
