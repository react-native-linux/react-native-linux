#include "InputPipeline.h"

#include <react/renderer/components/view/PointerEvent.h>
#include <react/renderer/graphics/Float.h>
#include <react/timing/primitives.h>

#include <cstddef>
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

} // namespace

void InputQueue::push(const InputEvent& event) {
    const bool isCoalescible = event.kind == InputEventKind::PointerMotion && !events_.empty() &&
                               events_.back().kind == InputEventKind::PointerMotion;

    if (isCoalescible) {
        events_.back() = event;

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
            break;
    }

    return {};
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
