#include "InputPipeline.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <linux/input-event-codes.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <react/renderer/components/view/PointerEvent.h>
#include <react/renderer/graphics/Float.h>
#include <react/timing/primitives.h>

namespace react_native_linux {

TextInputContentPurpose textInputContentPurpose(bool secureTextEntry, std::string_view keyboardType) {
    if (secureTextEntry) {
        return TextInputContentPurpose::Password;
    }

    if (keyboardType == "email-address") {
        return TextInputContentPurpose::Email;
    }

    if (keyboardType == "url") {
        return TextInputContentPurpose::Url;
    }

    if (keyboardType == "phone-pad" || keyboardType == "name-phone-pad") {
        return TextInputContentPurpose::Phone;
    }

    if (keyboardType == "number-pad") {
        return TextInputContentPurpose::Digits;
    }

    if (keyboardType == "numeric" || keyboardType == "decimal-pad") {
        return TextInputContentPurpose::Number;
    }

    return TextInputContentPurpose::Normal;
}

constexpr int kNoButton = -1;
constexpr facebook::react::Tag kNoPressTarget = 0;
constexpr int kNoButtonsBits = 0;
constexpr int kPrimaryButton = 0;
constexpr int kAuxiliaryButton = 1;
constexpr int kSecondaryButton = 2;
constexpr int kBackwardButton = 3;
constexpr int kForwardButton = 4;
constexpr int kPrimaryButtonsBit = 1;
constexpr int kSecondaryButtonsBit = 2;
constexpr int kAuxiliaryButtonsBit = 4;
constexpr int kBackwardButtonsBit = 8;
constexpr int kForwardButtonsBit = 16;

int domButtonOfEvdevCode(uint32_t evdevCode) {
    switch (evdevCode) {
    case BTN_LEFT:
        return kPrimaryButton;
    case BTN_MIDDLE:
        return kAuxiliaryButton;
    case BTN_RIGHT:
        return kSecondaryButton;
    case BTN_SIDE:
    case BTN_BACK:
        return kBackwardButton;
    case BTN_EXTRA:
    case BTN_FORWARD:
        return kForwardButton;
    default:
        return kNoButton;
    }
}

int buttonsMaskOfDomButton(int domButton) {
    switch (domButton) {
    case kPrimaryButton:
        return kPrimaryButtonsBit;
    case kSecondaryButton:
        return kSecondaryButtonsBit;
    case kAuxiliaryButton:
        return kAuxiliaryButtonsBit;
    case kBackwardButton:
        return kBackwardButtonsBit;
    case kForwardButton:
        return kForwardButtonsBit;
    default:
        return kNoButtonsBits;
    }
}

namespace {

constexpr double kMillisecondsPerSecond = 1000.0;
constexpr int kMousePointerId = 1;
constexpr int kClickDetail = 1;
constexpr int kNoDetail = 0;
constexpr facebook::react::Float kActiveButtonPressure = 0.5F;
constexpr facebook::react::Float kNoPressure = 0.0F;
constexpr facebook::react::Float kMousePointerExtent = 1.0F;
constexpr facebook::react::Float kNoTangentialPressure = 0.0F;
constexpr int kNoTilt = 0;
constexpr int kNoTwist = 0;
constexpr char kMousePointerType[] = "mouse";

int buttonsBitOf(int button) { return buttonsMaskOfDomButton(button); }

// A matrix whose determinant is under this maps every point onto a line or onto a point, so nothing is inside it
// to invert towards — the same floor `RetainedScene::toUntransformedPoint` uses for the identical reason.
constexpr float kSingularDeterminant = 1e-6F;

facebook::react::PointerEvent makePointerEvent(const InputEvent& event, facebook::react::Point targetOffset, int button,
                                               int detail, int buttons) {
    facebook::react::PointerEvent pointerEvent{};

    pointerEvent.pointerId = kMousePointerId;
    pointerEvent.pressure = buttons == kNoButtonsBits ? kNoPressure : kActiveButtonPressure;
    pointerEvent.pointerType = kMousePointerType;
    pointerEvent.clientPoint = event.surfacePoint;
    pointerEvent.screenPoint = event.surfacePoint;
    pointerEvent.offsetPoint = targetOffset;
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
    pointerEvent.timeStamp =
        event.eventTime.has_value() ? event.eventTime.value() : facebook::react::HighResTimeStamp::now();

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
    return event.kind == InputEventKind::PointerScrollContinuous || event.kind == InputEventKind::PointerScrollDiscrete;
}

constexpr char kTokenOpen = '{';
constexpr char kTokenClose = '}';
constexpr char kModifierSeparator = '+';
constexpr char kPayloadSeparator = ':';
constexpr std::string_view kControlModifier = "Ctrl";
constexpr std::string_view kShiftModifier = "Shift";
constexpr std::string_view kAltModifier = "Alt";
constexpr std::string_view kPreeditToken = "Preedit";
constexpr std::string_view kCommitToken = "Commit";
constexpr unsigned char kTwoByteLeadValue = 0xC0;
constexpr unsigned char kThreeByteLeadValue = 0xE0;
constexpr unsigned char kFourByteLeadValue = 0xF0;
constexpr size_t kOneByteLength = 1;
constexpr size_t kTwoByteLength = 2;
constexpr size_t kThreeByteLength = 3;
constexpr size_t kFourByteLength = 4;
constexpr char kAsciiCaseDistance = 'a' - 'A';

/**
 * The number of bytes the UTF-8 code point starting at `index` occupies, never running past the end of the
 * string. A lead byte is what says how long its sequence is, which is the property that makes UTF-8 scannable
 * from any position.
 */
size_t codePointLength(const std::string& text, size_t index) {
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    size_t length = kOneByteLength;

    if (lead >= kFourByteLeadValue) {
        length = kFourByteLength;
    } else if (lead >= kThreeByteLeadValue) {
        length = kThreeByteLength;
    } else if (lead >= kTwoByteLeadValue) {
        length = kTwoByteLength;
    }

    return std::min(length, text.size() - index);
}

/**
 * The DOM `key` value one token name stands for. A single-character name is the character it types, lowercased,
 * because that is what `domKeyName` produces for a key pressed with Ctrl held: the shortcut is `Ctrl` plus the
 * unmodified key, not `Ctrl` plus whatever the modifier turned it into.
 */
std::string tokenKeyName(const std::string& name) {
    constexpr std::array<NamedKey, 11> kTokenKeys{{{"Left", "ArrowLeft"},
                                                   {"Right", "ArrowRight"},
                                                   {"Up", "ArrowUp"},
                                                   {"Down", "ArrowDown"},
                                                   {"Home", "Home"},
                                                   {"End", "End"},
                                                   {"Backspace", "Backspace"},
                                                   {"Delete", "Delete"},
                                                   {"Enter", "Enter"},
                                                   {"Escape", "Escape"},
                                                   {"Tab", "Tab"}}};

    for (const NamedKey& tokenKey : kTokenKeys) {
        if (tokenKey.keysymName == name) {
            return std::string(tokenKey.keyName);
        }
    }

    if (name.size() != kSingleCharacterNameLength) {
        return {};
    }

    const char character = name.front();

    return std::string(1, character >= 'A' && character <= 'Z' ? static_cast<char>(character + kAsciiCaseDistance)
                                                               : character);
}

void appendKeyPress(std::vector<InputEvent>& events, const std::string& key, InputModifiers modifiers) {
    if (key.empty()) {
        return;
    }

    events.push_back(InputEvent{.kind = InputEventKind::KeyPress, .key = key, .modifiers = modifiers});
    events.push_back(InputEvent{.kind = InputEventKind::KeyRelease, .key = key, .modifiers = modifiers});
}

void appendToken(std::vector<InputEvent>& events, const std::string& token) {
    const size_t payloadSeparator = token.find(kPayloadSeparator);

    if (payloadSeparator != std::string::npos) {
        const std::string name = token.substr(0, payloadSeparator);
        const std::string payload = token.substr(payloadSeparator + 1);

        if (name == kPreeditToken) {
            events.push_back(InputEvent{.kind = InputEventKind::ImePreedit,
                                        .text = payload,
                                        .preeditCursorBegin = static_cast<int32_t>(payload.size()),
                                        .preeditCursorEnd = static_cast<int32_t>(payload.size())});
        } else if (name == kCommitToken) {
            events.push_back(InputEvent{.kind = InputEventKind::ImeCommit, .text = payload});
        }

        return;
    }

    InputModifiers modifiers;
    size_t start = 0;

    for (size_t separator = token.find(kModifierSeparator); separator != std::string::npos;
         separator = token.find(kModifierSeparator, start)) {
        const std::string_view modifier(token.data() + start, separator - start);

        modifiers.control |= modifier == kControlModifier;
        modifiers.shift |= modifier == kShiftModifier;
        modifiers.alt |= modifier == kAltModifier;
        start = separator + 1;
    }

    appendKeyPress(events, tokenKeyName(token.substr(start)), modifiers);
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
    // The event time advances with the run even though the deltas are summed: the compositor's last stamp is the
    // one the coalesced event is answering, not the first.
    previous.eventTimeMilliseconds = event.eventTimeMilliseconds;

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

// The `wl_pointer` axis this platform reads: the header cannot be included here because the Hermes-free test
// binary links no Wayland, and the seat passes the raw wire value through. 1 is horizontal scroll.
constexpr uint32_t kHorizontalScrollAxis = 1;

ScrollAxisKind scrollAxisForPointerAxis(uint32_t waylandAxis, const InputModifiers& modifiers) {
    if (waylandAxis == kHorizontalScrollAxis) {
        return ScrollAxisKind::Horizontal;
    }

    return modifiers.shift ? ScrollAxisKind::Horizontal : ScrollAxisKind::Vertical;
}

double notchesForValue120(int32_t value120) { return static_cast<double>(value120) / 120.0; }

double ScrollAxisLock::filter(const InputEvent& event) {
    if (event.kind == InputEventKind::PointerScrollDiscrete) {
        return event.scrollAmount;
    }

    const bool hasPreviousTime = lastEventTimeMilliseconds_.has_value();
    const bool isNewGesture =
        hasPreviousTime && event.eventTimeMilliseconds != 0 &&
        event.eventTimeMilliseconds > lastEventTimeMilliseconds_.value() &&
        event.eventTimeMilliseconds - lastEventTimeMilliseconds_.value() > kGestureSeparationMilliseconds;

    if (isNewGesture) {
        lockedAxis_.reset();
        lockedDistance_ = 0.0;
    }

    if (event.eventTimeMilliseconds != 0) {
        lastEventTimeMilliseconds_ = event.eventTimeMilliseconds;
    }

    const double magnitude = std::abs(event.scrollAmount);

    if (magnitude == 0.0) {
        return event.scrollAmount;
    }

    if (!lockedAxis_.has_value()) {
        lockedAxis_ = event.scrollAxis;
        lockedDistance_ = magnitude;

        return event.scrollAmount;
    }

    if (event.scrollAxis == lockedAxis_.value()) {
        lockedDistance_ += magnitude;

        return event.scrollAmount;
    }

    // A deliberate turn onto the other axis takes the lock and the delta passes; anything smaller is the drift
    // this filter exists to remove.
    if (magnitude >= kUnlockDistancePoints && magnitude >= kUnlockRatio * lockedDistance_) {
        lockedAxis_ = event.scrollAxis;
        lockedDistance_ = magnitude;

        return event.scrollAmount;
    }

    return 0.0;
}

void ScrollAxisLock::release() {
    lockedAxis_.reset();
    lockedDistance_ = 0.0;
}

std::vector<InputEvent> InputQueue::drain() { return std::exchange(events_, {}); }

size_t InputQueue::droppedEventCount() const noexcept { return droppedEventCount_; }

facebook::react::Point pointerOffsetWithinTarget(const PointerTargetTransform& transform,
                                                 facebook::react::Point surfacePoint) {
    const float determinant = (transform.scaleX * transform.scaleY) - (transform.skewX * transform.skewY);

    if (std::abs(determinant) < kSingularDeterminant) {
        return facebook::react::Point{};
    }

    const float relativeX = surfacePoint.x - transform.translateX;
    const float relativeY = surfacePoint.y - transform.translateY;

    const float localX = ((transform.scaleY * relativeX) - (transform.skewX * relativeY)) / determinant;
    const float localY = ((transform.scaleX * relativeY) - (transform.skewY * relativeX)) / determinant;

    return facebook::react::Point{.x = localX - transform.frameOrigin.x, .y = localY - transform.frameOrigin.y};
}

std::vector<PointerDispatch> PointerRouter::routeRelease(const InputEvent& event, facebook::react::Tag targetTag,
                                                         facebook::react::Point targetOffset) {
    pressedButtons_ &= ~buttonsBitOf(event.button);

    std::vector<PointerDispatch> dispatches{
        PointerDispatch{.type = PointerDispatchType::Up,
                        .event = makePointerEvent(event, targetOffset, event.button, kNoDetail, pressedButtons_)}};

    if (event.button == kPrimaryButton && targetTag == pressedTag_) {
        dispatches.push_back(PointerDispatch{
            .type = PointerDispatchType::Click,
            .event = makePointerEvent(event, targetOffset, event.button, kClickDetail, pressedButtons_)});
    }

    if (event.button == kPrimaryButton) {
        pressedTag_ = kNoPressTarget;
    }

    return dispatches;
}

void PointerRouter::cancelPressForScroll(const InputEvent& event) {
    if (event.scrollAmount == 0.0) {
        return;
    }

    pressedTag_ = kNoPressTarget;
}

std::vector<PointerDispatch> PointerRouter::route(const InputEvent& event, facebook::react::Tag targetTag,
                                                  facebook::react::Point targetOffset) {
    switch (
        event.kind) { // COV_EXCL: every InputEventKind value has a case, so the implicit no-match branch cannot execute
    case InputEventKind::PointerMotion:
        return {PointerDispatch{.type = PointerDispatchType::Move,
                                .event = makePointerEvent(event, targetOffset, kNoButton, kNoDetail, pressedButtons_)}};

    case InputEventKind::PointerButtonPress:
        if (event.button == kPrimaryButton) {
            pressedTag_ = targetTag;
        }

        pressedButtons_ |= buttonsBitOf(event.button);

        return {
            PointerDispatch{.type = PointerDispatchType::Down,
                            .event = makePointerEvent(event, targetOffset, event.button, kNoDetail, pressedButtons_)}};

    case InputEventKind::PointerButtonRelease:
        return routeRelease(event, targetTag, targetOffset);

    case InputEventKind::PointerLeave:
        pressedTag_ = kNoPressTarget;
        pressedButtons_ = kNoButtonsBits;

        return {PointerDispatch{.type = PointerDispatchType::Leave,
                                .event = makePointerEvent(event, targetOffset, kNoButton, kNoDetail, pressedButtons_)}};

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

void KeyRepeat::setRepeatInfo(int32_t ratePerSecond, int32_t delayMilliseconds) {
    ratePerSecond_ = ratePerSecond;
    delayMilliseconds_ = delayMilliseconds;
}

void KeyRepeat::press(uint64_t nowMilliseconds) {
    isHeld_ = true;
    pressedAtMilliseconds_ = nowMilliseconds;
    emittedRepeats_ = 0;
}

void KeyRepeat::release() {
    isHeld_ = false;
    emittedRepeats_ = 0;
}

bool KeyRepeat::isHeld() const noexcept { return isHeld_; }

uint32_t KeyRepeat::advance(uint64_t nowMilliseconds) {
    if (!isHeld_ || ratePerSecond_ <= 0) {
        return 0;
    }

    const double elapsedMilliseconds = static_cast<double>(nowMilliseconds - pressedAtMilliseconds_);

    if (elapsedMilliseconds < static_cast<double>(delayMilliseconds_)) {
        return 0;
    }

    const double intervalMilliseconds = kMillisecondsPerSecond / static_cast<double>(ratePerSecond_);
    const uint32_t dueRepeats =
        static_cast<uint32_t>(
            std::floor((elapsedMilliseconds - static_cast<double>(delayMilliseconds_)) / intervalMilliseconds)) +
        1;

    if (dueRepeats <= emittedRepeats_) {
        return 0;
    }

    const uint32_t repeats = dueRepeats - emittedRepeats_;
    emittedRepeats_ = dueRepeats;

    return repeats;
}

PointerDispatch makeActivationDispatch(const InputEvent& event, facebook::react::Point targetOrigin) {
    InputEvent activation = event;

    activation.surfacePoint = targetOrigin;

    // The offset is zero rather than derived from `targetOrigin`, because a keyboard activation has no press
    // point of its own to place inside the target's box — it is reported at the target's own origin, which is
    // zero offset by definition, in every coordinate space this platform's transforms can put the target in.
    return PointerDispatch{
        .type = PointerDispatchType::Click,
        .event = makePointerEvent(activation, facebook::react::Point{}, kPrimaryButton, kClickDetail, kNoButtonsBits)};
}

bool isScrollEvent(const InputEvent& event) {
    return event.kind == InputEventKind::PointerScrollContinuous ||
           event.kind == InputEventKind::PointerScrollDiscrete || event.kind == InputEventKind::PointerScrollStop;
}

facebook::react::HighResTimeStamp EventTimeMapper::map(uint32_t eventTimeMilliseconds,
                                                       facebook::react::HighResTimeStamp now) {
    if (eventTimeMilliseconds == 0) {
        return now;
    }

    const facebook::react::HighResTimeStamp compositorTime =
        facebook::react::HighResTimeStamp::fromChronoSteadyClockTimePoint(
            std::chrono::steady_clock::time_point(std::chrono::milliseconds(eventTimeMilliseconds)));

    if (!offset_.has_value()) {
        offset_ = now - compositorTime;

        return now;
    }

    return compositorTime + offset_.value();
}

std::optional<uint64_t> earliestEventTimeNanoseconds(const std::vector<InputEvent>& events) {
    std::optional<uint64_t> earliest;

    for (const InputEvent& event : events) {
        if (!event.eventTime.has_value()) {
            continue;
        }

        const uint64_t nanoseconds =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      event.eventTime.value().toChronoSteadyClockTimePoint().time_since_epoch())
                                      .count());

        if (!earliest.has_value() || nanoseconds < earliest.value()) {
            earliest = nanoseconds;
        }
    }

    return earliest;
}

bool isTextKey(const std::string& key) {
    if (key.empty()) {
        return false;
    }

    return codePointLength(key, 0) == key.size() && isPrintableText(key);
}

std::vector<std::string> keySequenceTokens(const std::string& sequence) {
    std::vector<std::string> tokens;

    for (size_t index = 0; index < sequence.size();) {
        if (sequence[index] != kTokenOpen) {
            const size_t width = codePointLength(sequence, index);

            tokens.push_back(sequence.substr(index, width));
            index += width;

            continue;
        }

        const size_t close = sequence.find(kTokenClose, index);

        if (close == std::string::npos) {
            tokens.push_back(sequence.substr(index));

            break;
        }

        tokens.push_back(sequence.substr(index, close - index + 1));
        index = close + 1;
    }

    return tokens;
}

std::vector<InputEvent> parseKeySequence(const std::string& sequence) {
    std::vector<InputEvent> events;

    for (const std::string& token : keySequenceTokens(sequence)) {
        if (token.front() == kTokenOpen) {
            // An unterminated group cannot be a token, and `parseKeySequence` has always stopped there rather
            // than guessing where it ended.
            if (token.back() != kTokenClose) {
                break;
            }

            appendToken(events, token.substr(1, token.size() - 2));

            continue;
        }

        appendKeyPress(events, token, {});
    }

    return events;
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
