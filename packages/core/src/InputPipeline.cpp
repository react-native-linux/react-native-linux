#include "InputPipeline.h"

#include <react/renderer/components/view/PointerEvent.h>
#include <react/renderer/graphics/Float.h>
#include <react/timing/primitives.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

constexpr int kMousePointerId = 1;
constexpr int kNoButton = -1;
constexpr int kPrimaryButton = 0;
constexpr int kAuxiliaryButton = 1;
constexpr int kSecondaryButton = 2;
constexpr int kPrimaryButtonsBit = 1;
constexpr int kSecondaryButtonsBit = 2;
constexpr int kAuxiliaryButtonsBit = 4;
constexpr int kNoButtonsBits = 0;
constexpr int kClickDetail = 1;
constexpr int kNoDetail = 0;
constexpr facebook::react::Float kActiveButtonPressure = 0.5F;
constexpr facebook::react::Float kNoPressure = 0.0F;
constexpr facebook::react::Float kMousePointerExtent = 1.0F;
constexpr facebook::react::Float kNoTangentialPressure = 0.0F;
constexpr int kNoTilt = 0;
constexpr int kNoTwist = 0;
constexpr char kMousePointerType[] = "mouse";

int buttonsBitOf(int button) {
    switch (button) {
        case kPrimaryButton:
            return kPrimaryButtonsBit;
        case kAuxiliaryButton:
            return kAuxiliaryButtonsBit;
        case kSecondaryButton:
            return kSecondaryButtonsBit;
        default:
            return kNoButtonsBits;
    }
}

facebook::react::PointerEvent makePointerEvent(const InputEvent& event, facebook::react::Point targetOrigin,
                                               int button, int detail, int buttons) {
    facebook::react::PointerEvent pointerEvent{};

    pointerEvent.pointerId = kMousePointerId;
    pointerEvent.pressure = buttons == kNoButtonsBits ? kNoPressure : kActiveButtonPressure;
    pointerEvent.pointerType = kMousePointerType;
    pointerEvent.clientPoint = event.surfacePoint;
    pointerEvent.screenPoint = event.surfacePoint;
    pointerEvent.offsetPoint = event.surfacePoint - targetOrigin;
    pointerEvent.width = kMousePointerExtent;
    pointerEvent.height = kMousePointerExtent;
    pointerEvent.tiltX = kNoTilt;
    pointerEvent.tiltY = kNoTilt;
    pointerEvent.detail = detail;
    pointerEvent.buttons = buttons;
    pointerEvent.tangentialPressure = kNoTangentialPressure;
    pointerEvent.twist = kNoTwist;
    pointerEvent.ctrlKey = event.modifiers.control;
    pointerEvent.shiftKey = event.modifiers.shift;
    pointerEvent.altKey = event.modifiers.alt;
    pointerEvent.metaKey = event.modifiers.meta;
    pointerEvent.isPrimary = true;
    pointerEvent.button = button;
    pointerEvent.timeStamp = facebook::react::HighResTimeStamp::now();

    return pointerEvent;
}

constexpr char kUnidentifiedKey[] = "Unidentified";
constexpr size_t kSingleCharacterNameLength = 1;
constexpr unsigned char kFirstPrintableByte = 0x20;
constexpr unsigned char kDeleteByte = 0x7F;

/**
 * One key whose DOM `key` value is a name rather than the text it produces. Everything printable is absent on
 * purpose: `domKeyName` derives those from the keysym name or the text, which is a rule instead of a list and
 * therefore cannot be missing an entry — which is what react-native-macos#437 turned out to be.
 */
struct NamedKey {
    std::string_view keysymName;
    std::string_view keyName;
};

constexpr std::array<NamedKey, 40> kNamedKeys{{{"Return", "Enter"},
                                               {"KP_Enter", "Enter"},
                                               {"Tab", "Tab"},
                                               {"ISO_Left_Tab", "Tab"},
                                               {"space", " "},
                                               {"Escape", "Escape"},
                                               {"BackSpace", "Backspace"},
                                               {"Delete", "Delete"},
                                               {"Insert", "Insert"},
                                               {"Home", "Home"},
                                               {"End", "End"},
                                               {"Prior", "PageUp"},
                                               {"Next", "PageDown"},
                                               {"Left", "ArrowLeft"},
                                               {"Right", "ArrowRight"},
                                               {"Up", "ArrowUp"},
                                               {"Down", "ArrowDown"},
                                               {"Shift_L", "Shift"},
                                               {"Shift_R", "Shift"},
                                               {"Control_L", "Control"},
                                               {"Control_R", "Control"},
                                               {"Alt_L", "Alt"},
                                               {"Alt_R", "Alt"},
                                               {"Super_L", "Meta"},
                                               {"Super_R", "Meta"},
                                               {"Caps_Lock", "CapsLock"},
                                               {"Num_Lock", "NumLock"},
                                               {"Menu", "ContextMenu"},
                                               {"F1", "F1"},
                                               {"F2", "F2"},
                                               {"F3", "F3"},
                                               {"F4", "F4"},
                                               {"F5", "F5"},
                                               {"F6", "F6"},
                                               {"F7", "F7"},
                                               {"F8", "F8"},
                                               {"F9", "F9"},
                                               {"F10", "F10"},
                                               {"F11", "F11"},
                                               {"F12", "F12"}}};

/**
 * One physical key, as the kernel numbers it and as the DOM names it. The evdev codes are `linux/input-event-codes.h`'s
 * own, so this table is a keyboard layout's worth of rows and no arithmetic: evdev numbers the letters in QWERTY
 * row order, which is a physical order that no alphabetical formula reproduces.
 */
struct PhysicalKey {
    uint32_t evdevKeycode;
    std::string_view codeName;
};

constexpr std::array<PhysicalKey, 87> kPhysicalKeys{{{KEY_A, "KeyA"},
                                                     {KEY_B, "KeyB"},
                                                     {KEY_C, "KeyC"},
                                                     {KEY_D, "KeyD"},
                                                     {KEY_E, "KeyE"},
                                                     {KEY_F, "KeyF"},
                                                     {KEY_G, "KeyG"},
                                                     {KEY_H, "KeyH"},
                                                     {KEY_I, "KeyI"},
                                                     {KEY_J, "KeyJ"},
                                                     {KEY_K, "KeyK"},
                                                     {KEY_L, "KeyL"},
                                                     {KEY_M, "KeyM"},
                                                     {KEY_N, "KeyN"},
                                                     {KEY_O, "KeyO"},
                                                     {KEY_P, "KeyP"},
                                                     {KEY_Q, "KeyQ"},
                                                     {KEY_R, "KeyR"},
                                                     {KEY_S, "KeyS"},
                                                     {KEY_T, "KeyT"},
                                                     {KEY_U, "KeyU"},
                                                     {KEY_V, "KeyV"},
                                                     {KEY_W, "KeyW"},
                                                     {KEY_X, "KeyX"},
                                                     {KEY_Y, "KeyY"},
                                                     {KEY_Z, "KeyZ"},
                                                     {KEY_1, "Digit1"},
                                                     {KEY_2, "Digit2"},
                                                     {KEY_3, "Digit3"},
                                                     {KEY_4, "Digit4"},
                                                     {KEY_5, "Digit5"},
                                                     {KEY_6, "Digit6"},
                                                     {KEY_7, "Digit7"},
                                                     {KEY_8, "Digit8"},
                                                     {KEY_9, "Digit9"},
                                                     {KEY_0, "Digit0"},
                                                     {KEY_ENTER, "Enter"},
                                                     {KEY_ESC, "Escape"},
                                                     {KEY_BACKSPACE, "Backspace"},
                                                     {KEY_TAB, "Tab"},
                                                     {KEY_SPACE, "Space"},
                                                     {KEY_MINUS, "Minus"},
                                                     {KEY_EQUAL, "Equal"},
                                                     {KEY_LEFTBRACE, "BracketLeft"},
                                                     {KEY_RIGHTBRACE, "BracketRight"},
                                                     {KEY_BACKSLASH, "Backslash"},
                                                     {KEY_SEMICOLON, "Semicolon"},
                                                     {KEY_APOSTROPHE, "Quote"},
                                                     {KEY_GRAVE, "Backquote"},
                                                     {KEY_COMMA, "Comma"},
                                                     {KEY_DOT, "Period"},
                                                     {KEY_SLASH, "Slash"},
                                                     {KEY_CAPSLOCK, "CapsLock"},
                                                     {KEY_F1, "F1"},
                                                     {KEY_F2, "F2"},
                                                     {KEY_F3, "F3"},
                                                     {KEY_F4, "F4"},
                                                     {KEY_F5, "F5"},
                                                     {KEY_F6, "F6"},
                                                     {KEY_F7, "F7"},
                                                     {KEY_F8, "F8"},
                                                     {KEY_F9, "F9"},
                                                     {KEY_F10, "F10"},
                                                     {KEY_F11, "F11"},
                                                     {KEY_F12, "F12"},
                                                     {KEY_LEFTSHIFT, "ShiftLeft"},
                                                     {KEY_RIGHTSHIFT, "ShiftRight"},
                                                     {KEY_LEFTCTRL, "ControlLeft"},
                                                     {KEY_RIGHTCTRL, "ControlRight"},
                                                     {KEY_LEFTALT, "AltLeft"},
                                                     {KEY_RIGHTALT, "AltRight"},
                                                     {KEY_LEFTMETA, "MetaLeft"},
                                                     {KEY_RIGHTMETA, "MetaRight"},
                                                     {KEY_HOME, "Home"},
                                                     {KEY_END, "End"},
                                                     {KEY_PAGEUP, "PageUp"},
                                                     {KEY_PAGEDOWN, "PageDown"},
                                                     {KEY_INSERT, "Insert"},
                                                     {KEY_DELETE, "Delete"},
                                                     {KEY_LEFT, "ArrowLeft"},
                                                     {KEY_RIGHT, "ArrowRight"},
                                                     {KEY_UP, "ArrowUp"},
                                                     {KEY_DOWN, "ArrowDown"},
                                                     {KEY_NUMLOCK, "NumLock"},
                                                     {KEY_KPENTER, "NumpadEnter"},
                                                     {KEY_COMPOSE, "ContextMenu"},
                                                     {KEY_102ND, "IntlBackslash"}}};

bool isPrintableText(const std::string& keyText) {
    const unsigned char first = static_cast<unsigned char>(keyText.front());

    return first >= kFirstPrintableByte && first != kDeleteByte;
}

bool isScrollDelta(const InputEvent& event) {
    return event.kind == InputEventKind::PointerScrollContinuous ||
           event.kind == InputEventKind::PointerScrollDiscrete;
}

/**
 * Folds `event` into the event already at the back of the queue when the two describe the same thing, and reports
 * whether it did. Motion collapses to the latest position because the intermediate ones are not information;
 * scroll deltas sum because every one of them is.
 */
bool coalesceIntoPrevious(InputEvent& previous, const InputEvent& event) {
    if (event.kind == InputEventKind::PointerMotion && previous.kind == InputEventKind::PointerMotion) {
        previous = event;

        return true;
    }

    if (!isScrollDelta(event) || event.scrollAxis != previous.scrollAxis) {
        return false;
    }

    // wl_pointer sends axis_discrete before the axis event carrying the same notch, so a continuous delta directly
    // behind a discrete one on the same axis is that notch measured a second time in units nobody defines.
    if (event.kind == InputEventKind::PointerScrollContinuous &&
        previous.kind == InputEventKind::PointerScrollDiscrete) {
        return true;
    }

    if (event.kind != previous.kind) {
        return false;
    }

    previous.scrollAmount += event.scrollAmount;

    return true;
}

} // namespace

void InputQueue::push(const InputEvent& event) {
    if (!events_.empty() && coalesceIntoPrevious(events_.back(), event)) {
        return;
    }

    if (events_.size() >= kInputQueueCapacity) {
        ++droppedEventCount_;

        return;
    }

    events_.push_back(event);
}

std::vector<InputEvent> InputQueue::drain() { return std::exchange(events_, {}); }

size_t InputQueue::droppedEventCount() const noexcept { return droppedEventCount_; }

std::vector<PointerDispatch> PointerRouter::routeRelease(const InputEvent& event, facebook::react::Tag targetTag,
                                                         facebook::react::Point targetOrigin) {
    pressedButtons_ &= ~buttonsBitOf(event.button);

    std::vector<PointerDispatch> dispatches{PointerDispatch{
        .type = PointerDispatchType::Up,
        .event = makePointerEvent(event, targetOrigin, event.button, kNoDetail, pressedButtons_)}};

    if (targetTag == pressedTag_) {
        dispatches.push_back(PointerDispatch{
            .type = PointerDispatchType::Click,
            .event = makePointerEvent(event, targetOrigin, event.button, kClickDetail, pressedButtons_)});
    }

    pressedTag_ = 0;

    return dispatches;
}

std::vector<PointerDispatch> PointerRouter::route(const InputEvent& event, facebook::react::Tag targetTag,
                                                  facebook::react::Point targetOrigin) {
    switch (event.kind) { // COV_EXCL: every InputEventKind value has a case, so the implicit no-match branch cannot execute
        case InputEventKind::PointerMotion:
            return {PointerDispatch{
                .type = PointerDispatchType::Move,
                .event = makePointerEvent(event, targetOrigin, kNoButton, kNoDetail, pressedButtons_)}};

        case InputEventKind::PointerButtonPress:
            pressedTag_ = targetTag;
            pressedButtons_ |= buttonsBitOf(event.button);

            return {PointerDispatch{
                .type = PointerDispatchType::Down,
                .event = makePointerEvent(event, targetOrigin, event.button, kNoDetail, pressedButtons_)}};

        case InputEventKind::PointerButtonRelease:
            return routeRelease(event, targetTag, targetOrigin);

        case InputEventKind::PointerLeave:
            pressedTag_ = 0;
            pressedButtons_ = kNoButtonsBits;

            return {PointerDispatch{
                .type = PointerDispatchType::Leave,
                .event = makePointerEvent(event, targetOrigin, kNoButton, kNoDetail, pressedButtons_)}};

        case InputEventKind::KeyPress:
        case InputEventKind::KeyRelease:
        case InputEventKind::ImePreedit:
        case InputEventKind::ImeCommit:
        case InputEventKind::ImeDeleteSurrounding:
        case InputEventKind::PointerScrollContinuous:
        case InputEventKind::PointerScrollDiscrete:
        case InputEventKind::PointerScrollStop:
            break;
    }

    return {};
}

std::string domKeyName(const std::string& keysymName, const std::string& keyText) {
    for (const NamedKey& namedKey : kNamedKeys) {
        if (namedKey.keysymName == keysymName) {
            return std::string(namedKey.keyName);
        }
    }

    if (keysymName.size() == kSingleCharacterNameLength) {
        return keysymName;
    }

    if (!keyText.empty() && isPrintableText(keyText)) {
        return keyText;
    }

    return std::string(kUnidentifiedKey);
}

std::string domKeyCode(uint32_t evdevKeycode) {
    for (const PhysicalKey& physicalKey : kPhysicalKeys) {
        if (physicalKey.evdevKeycode == evdevKeycode) {
            return std::string(physicalKey.codeName);
        }
    }

    return std::string(kUnidentifiedKey);
}

PointerDispatch makeActivationDispatch(const InputEvent& event, facebook::react::Point targetOrigin) {
    InputEvent activation = event;

    activation.surfacePoint = targetOrigin;

    return PointerDispatch{
        .type = PointerDispatchType::Click,
        .event = makePointerEvent(activation, targetOrigin, kPrimaryButton, kClickDetail, kNoButtonsBits)};
}

bool isScrollEvent(const InputEvent& event) {
    return event.kind == InputEventKind::PointerScrollContinuous ||
           event.kind == InputEventKind::PointerScrollDiscrete || event.kind == InputEventKind::PointerScrollStop;
}

void deliverImeEvent(const InputEvent& event, ImeSink& sink) {
    switch (event.kind) {
        case InputEventKind::ImePreedit:
            sink.onImePreedit(event.text, event.preeditCursorBegin, event.preeditCursorEnd);
            break;

        case InputEventKind::ImeCommit:
            sink.onImeCommit(event.text);
            break;

        case InputEventKind::ImeDeleteSurrounding:
            sink.onImeDeleteSurrounding(event.deleteBeforeLength, event.deleteAfterLength);
            break;

        default:
            break;
    }
}

} // namespace react_native_linux
