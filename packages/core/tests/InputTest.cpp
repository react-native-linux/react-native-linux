#include "InputPipeline.h"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <linux/input-event-codes.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/graphics/Point.h>
#include <string>
#include <vector>

namespace {

using facebook::react::Point;
using facebook::react::Tag;
using react_native_linux::buttonsMaskOfDomButton;
using react_native_linux::domButtonOfEvdevCode;
using react_native_linux::domKeyCode;
using react_native_linux::domKeyName;
using react_native_linux::InputEvent;
using react_native_linux::InputEventKind;
using react_native_linux::InputModifiers;
using react_native_linux::InputQueue;
using react_native_linux::isTextKey;
using react_native_linux::kInputQueueCapacity;
using react_native_linux::makeActivationDispatch;
using react_native_linux::notchesForValue120;
using react_native_linux::parseKeySequence;
using react_native_linux::PointerDispatch;
using react_native_linux::PointerDispatchType;
using react_native_linux::pointerOffsetWithinTarget;
using react_native_linux::PointerRouter;
using react_native_linux::PointerTargetTransform;
using react_native_linux::scrollAxisForPointerAxis;
using react_native_linux::ScrollAxisKind;

constexpr Tag kBoxTag = 4;
constexpr Tag kOtherTag = 5;
constexpr int kPrimaryButton = 0;
constexpr int kAuxiliaryButton = 1;
constexpr int kSecondaryButton = 2;
constexpr int kUnmappedButton = -1;
constexpr int kPrimaryButtonsBit = 1;
constexpr int kSecondaryButtonsBit = 2;
constexpr int kAuxiliaryButtonsBit = 4;
// One frame of a 1000 Hz mouse at 60 Hz, rounded up. The acceptance criterion for issue #18 is that this many raw
// motions cost React exactly one event.
constexpr size_t kHighRateMotionCount = 17;

Point makePoint(float x, float y) { return Point{.x = x, .y = y}; }

InputEvent makeMotion(float x, float y) {
    return InputEvent{.kind = InputEventKind::PointerMotion, .surfacePoint = makePoint(x, y)};
}

InputEvent makeButton(InputEventKind kind, int button) {
    return InputEvent{.kind = kind, .surfacePoint = makePoint(0, 0), .button = button};
}

TEST(InputQueueTest, CoalescesAHighRateMotionBurstIntoOneEvent) {
    InputQueue queue;

    for (size_t step = 0; step < kHighRateMotionCount; ++step) {
        queue.push(makeMotion(static_cast<float>(step), static_cast<float>(step * 2)));
    }

    const std::vector<InputEvent> drained = queue.drain();

    ASSERT_EQ(drained.size(), 1U);
    EXPECT_EQ(drained[0].kind, InputEventKind::PointerMotion);
    EXPECT_FLOAT_EQ(drained[0].surfacePoint.x, static_cast<float>(kHighRateMotionCount - 1));
    EXPECT_FLOAT_EQ(drained[0].surfacePoint.y, static_cast<float>((kHighRateMotionCount - 1) * 2));
    EXPECT_EQ(queue.droppedEventCount(), 0U);
}

TEST(InputQueueTest, KeepsTheMotionOnEachSideOfAButtonPress) {
    InputQueue queue;

    queue.push(makeMotion(10, 10));
    queue.push(makeMotion(20, 20));
    queue.push(makeButton(InputEventKind::PointerButtonPress, kPrimaryButton));
    queue.push(makeMotion(30, 30));
    queue.push(makeMotion(40, 40));

    const std::vector<InputEvent> drained = queue.drain();

    ASSERT_EQ(drained.size(), 3U);
    EXPECT_EQ(drained[0].kind, InputEventKind::PointerMotion);
    EXPECT_FLOAT_EQ(drained[0].surfacePoint.x, 20);
    EXPECT_EQ(drained[1].kind, InputEventKind::PointerButtonPress);
    EXPECT_EQ(drained[2].kind, InputEventKind::PointerMotion);
    EXPECT_FLOAT_EQ(drained[2].surfacePoint.x, 40);
}

TEST(InputQueueTest, DrainEmptiesTheQueue) {
    InputQueue queue;

    queue.push(makeMotion(10, 10));

    EXPECT_EQ(queue.drain().size(), 1U);
    EXPECT_TRUE(queue.drain().empty());
}

TEST(InputQueueTest, CountsWhatItDropsPastCapacity) {
    constexpr size_t kOverflowCount = 3;

    InputQueue queue;

    for (size_t step = 0; step < kInputQueueCapacity + kOverflowCount; ++step) {
        queue.push(makeButton(InputEventKind::PointerButtonPress, kPrimaryButton));
    }

    EXPECT_EQ(queue.droppedEventCount(), kOverflowCount);
    EXPECT_EQ(queue.drain().size(), kInputQueueCapacity);
}

TEST(PointerRouterTest, MotionBecomesOneMoveWithNoButton) {
    PointerRouter router;

    // The third argument is already the target-local offset `pointerOffsetWithinTarget` would have produced for
    // this press — the router only carries it through to the payload, it never derives it — so it need not equal
    // any subtraction of the surface point the caller resolved the target against.
    const std::vector<PointerDispatch> dispatches = router.route(makeMotion(150, 120), kBoxTag, makePoint(50, 40));

    ASSERT_EQ(dispatches.size(), 1U);
    EXPECT_EQ(dispatches[0].type, PointerDispatchType::Move);
    EXPECT_EQ(dispatches[0].event.button, -1);
    EXPECT_EQ(dispatches[0].event.buttons, 0);
    EXPECT_FLOAT_EQ(dispatches[0].event.pressure, 0.0F);
    EXPECT_FLOAT_EQ(dispatches[0].event.clientPoint.x, 150);
    EXPECT_FLOAT_EQ(dispatches[0].event.offsetPoint.x, 50);
    EXPECT_FLOAT_EQ(dispatches[0].event.offsetPoint.y, 40);
    EXPECT_TRUE(dispatches[0].event.isPrimary);
    EXPECT_EQ(dispatches[0].event.pointerType, "mouse");
}

// Issue #246. `locationX/Y` — this platform's `offsetX/Y`, since nothing here ever builds a legacy `Touch`
// payload — has to be the press point undone through every transform the target composes, not the press point
// minus the target's forward-mapped surface origin: the two agree for a pure translation (an identity ancestor
// chain, or a scrolled one, which is a translation too) and disagree everywhere else, which is exactly the gap
// upstream's own `PointerEventsProcessor::retargetPointerEvent` names as a known, unfixed "HACK" for the same
// reason. The table below is built from the same inverse `RetainedScene::coversPrimitive` applies, worked by hand
// against concrete matrices rather than copied from the implementation, so a mistake in one is unlikely to be the
// same mistake in the other.
struct PointerOffsetCase {
    const char* name;
    PointerTargetTransform transform;
    Point surfacePoint;
    Point expectedOffset;
};

TEST(PointerOffsetWithinTargetTest, InvertsEveryComposedTransform) {
    const std::vector<PointerOffsetCase> cases{
        // Translated only (also stands in for a scrolled ancestor, which composes a translation and nothing
        // else): the surface origin is the frame's own origin plus the translation, so subtracting it and
        // inverting agree.
        {"a translated target", PointerTargetTransform{.translateX = 100, .translateY = 80}, makePoint(150, 120),
         makePoint(50, 40)},
        // Scaled 2x about its own centre: a box whose local frame is (100,100)-(150,150) painted twice as large
        // covers surface (75,75)-(175,175). Pressing surface (100,100) is a quarter of the way across the painted
        // box on both axes, which is a local offset of 12.5 — not the 25 a plain subtraction from the (still
        // untransformed-looking) forward-mapped origin (75,75) would report.
        {"a target scaled 2x about its own centre",
         PointerTargetTransform{.scaleX = 2, .translateX = -125, .scaleY = 2, .translateY = -125,
                                .frameOrigin = makePoint(100, 100)},
         makePoint(100, 100), makePoint(12.5, 12.5)},
        // Scaled 2x on X only, 1x on Y, about its own centre (50, 25) of a 100x50 box at the local origin: the
        // surface centre does not move (a point at the centre of a scale is invariant), and the local point under
        // it is the box's own centre.
        {"a target scaled non-uniformly", PointerTargetTransform{.scaleX = 2, .translateX = -50, .scaleY = 1},
         makePoint(50, 25), makePoint(50, 25)},
        // Rotated 90 degrees about its own centre (125, 125): the centre of the rotation does not move, and it
        // reports the box's own centre — (25, 25) inside a 50x50 box — not the (-25, 25) a plain subtraction from
        // the forward-mapped (now rotated) corner would report.
        {"a target rotated 90 degrees, pressed at its centre",
         PointerTargetTransform{.scaleX = 0, .skewX = -1, .translateX = 250, .skewY = 1, .scaleY = 0,
                                .frameOrigin = makePoint(100, 100)},
         makePoint(125, 125), makePoint(25, 25)},
        // The same rotated target, pressed where its own local origin corner now sits on the surface — (150, 100)
        // is `mapPoint` of the frame's own (100, 100) origin through the same matrix — reports local (0, 0), the
        // frame's own corner.
        {"a target rotated 90 degrees, pressed at its own rotated corner",
         PointerTargetTransform{.scaleX = 0, .skewX = -1, .translateX = 250, .skewY = 1, .scaleY = 0,
                                .frameOrigin = makePoint(100, 100)},
         makePoint(150, 100), makePoint(0, 0)},
        // `scale: 0` maps every surface point onto the target's own origin, so hit-testing never lands on it —
        // the same floor `RetainedScene::toUntransformedPoint` applies. There is nothing to invert towards, so the
        // offset is zero rather than a divide by zero.
        {"a target scaled to nothing",
         PointerTargetTransform{.scaleX = 0, .translateX = 10, .scaleY = 0, .translateY = 20,
                                .frameOrigin = makePoint(5, 5)},
         makePoint(400, 400), makePoint(0, 0)},
    };

    for (const PointerOffsetCase& offsetCase : cases) {
        const Point offset = pointerOffsetWithinTarget(offsetCase.transform, offsetCase.surfacePoint);

        EXPECT_FLOAT_EQ(offset.x, offsetCase.expectedOffset.x) << offsetCase.name;
        EXPECT_FLOAT_EQ(offset.y, offsetCase.expectedOffset.y) << offsetCase.name;
    }
}

TEST(PointerRouterTest, PressAndReleaseOnTheSameTargetProduceAClick) {
    PointerRouter router;

    const std::vector<PointerDispatch> pressed =
        router.route(makeButton(InputEventKind::PointerButtonPress, kPrimaryButton), kBoxTag, makePoint(0, 0));

    ASSERT_EQ(pressed.size(), 1U);
    EXPECT_EQ(pressed[0].type, PointerDispatchType::Down);
    EXPECT_EQ(pressed[0].event.buttons, kPrimaryButtonsBit);
    EXPECT_EQ(pressed[0].event.button, kPrimaryButton);
    EXPECT_FLOAT_EQ(pressed[0].event.pressure, 0.5F);

    const std::vector<PointerDispatch> released =
        router.route(makeButton(InputEventKind::PointerButtonRelease, kPrimaryButton), kBoxTag, makePoint(0, 0));

    ASSERT_EQ(released.size(), 2U);
    EXPECT_EQ(released[0].type, PointerDispatchType::Up);
    EXPECT_EQ(released[0].event.buttons, 0);
    EXPECT_EQ(released[0].event.detail, 0);
    EXPECT_EQ(released[1].type, PointerDispatchType::Click);
    EXPECT_EQ(released[1].event.detail, 1);
}

TEST(PointerRouterTest, ReleaseOnADifferentTargetIsNotAClick) {
    PointerRouter router;

    router.route(makeButton(InputEventKind::PointerButtonPress, kPrimaryButton), kBoxTag, makePoint(0, 0));

    const std::vector<PointerDispatch> released =
        router.route(makeButton(InputEventKind::PointerButtonRelease, kPrimaryButton), kOtherTag, makePoint(0, 0));

    ASSERT_EQ(released.size(), 1U);
    EXPECT_EQ(released[0].type, PointerDispatchType::Up);
}

TEST(PointerRouterTest, TracksEveryMappedButtonInTheButtonsBitmask) {
    PointerRouter router;

    router.route(makeButton(InputEventKind::PointerButtonPress, kPrimaryButton), kBoxTag, makePoint(0, 0));
    router.route(makeButton(InputEventKind::PointerButtonPress, kAuxiliaryButton), kBoxTag, makePoint(0, 0));

    const std::vector<PointerDispatch> secondary =
        router.route(makeButton(InputEventKind::PointerButtonPress, kSecondaryButton), kBoxTag, makePoint(0, 0));

    ASSERT_EQ(secondary.size(), 1U);
    EXPECT_EQ(secondary[0].event.buttons, kPrimaryButtonsBit | kAuxiliaryButtonsBit | kSecondaryButtonsBit);

    const std::vector<PointerDispatch> unmapped =
        router.route(makeButton(InputEventKind::PointerButtonPress, kUnmappedButton), kBoxTag, makePoint(0, 0));

    ASSERT_EQ(unmapped.size(), 1U);
    EXPECT_EQ(unmapped[0].event.buttons, kPrimaryButtonsBit | kAuxiliaryButtonsBit | kSecondaryButtonsBit);
}

TEST(PointerRouterTest, LeavingTheSurfaceClearsTheButtonsAndThePressTarget) {
    PointerRouter router;

    router.route(makeButton(InputEventKind::PointerButtonPress, kPrimaryButton), kBoxTag, makePoint(0, 0));

    const std::vector<PointerDispatch> left =
        router.route(InputEvent{.kind = InputEventKind::PointerLeave}, kBoxTag, makePoint(0, 0));

    ASSERT_EQ(left.size(), 1U);
    EXPECT_EQ(left[0].type, PointerDispatchType::Leave);
    EXPECT_EQ(left[0].event.buttons, 0);

    const std::vector<PointerDispatch> released =
        router.route(makeButton(InputEventKind::PointerButtonRelease, kPrimaryButton), kBoxTag, makePoint(0, 0));

    ASSERT_EQ(released.size(), 1U);
    EXPECT_EQ(released[0].type, PointerDispatchType::Up);
}

TEST(PointerRouterTest, KeyEventsProduceNoPointerDispatches) {
    PointerRouter router;

    const InputEvent pressed{.kind = InputEventKind::KeyPress, .key = std::string("a")};
    const InputEvent released{.kind = InputEventKind::KeyRelease, .key = std::string("a")};

    EXPECT_TRUE(router.route(pressed, kBoxTag, makePoint(0, 0)).empty());
    EXPECT_TRUE(router.route(released, kBoxTag, makePoint(0, 0)).empty());
}

TEST(PointerRouterTest, CarriesTheModifierStateOntoThePointerEvent) {
    PointerRouter router;

    InputEvent event = makeMotion(10, 10);

    event.modifiers = InputModifiers{.control = true, .shift = false, .alt = true, .meta = true};

    const std::vector<PointerDispatch> dispatches = router.route(event, kBoxTag, makePoint(0, 0));

    ASSERT_EQ(dispatches.size(), 1U);
    EXPECT_TRUE(dispatches[0].event.ctrlKey);
    EXPECT_FALSE(dispatches[0].event.shiftKey);
    EXPECT_TRUE(dispatches[0].event.altKey);
    EXPECT_TRUE(dispatches[0].event.metaKey);
}

TEST(KeyEventTest, NamedKeysBecomeTheirDomNamesRatherThanTheirControlCharacters) {
    EXPECT_EQ(domKeyName("Return", "\r"), "Enter");
    EXPECT_EQ(domKeyName("KP_Enter", "\r"), "Enter");
    EXPECT_EQ(domKeyName("Tab", "\t"), "Tab");
    EXPECT_EQ(domKeyName("BackSpace", "\b"), "Backspace");
    EXPECT_EQ(domKeyName("Escape", "\x1B"), "Escape");
    EXPECT_EQ(domKeyName("space", " "), " ");
    EXPECT_EQ(domKeyName("Prior", ""), "PageUp");
    EXPECT_EQ(domKeyName("Left", ""), "ArrowLeft");
    EXPECT_EQ(domKeyName("Super_L", ""), "Meta");
    EXPECT_EQ(domKeyName("F5", ""), "F5");
}

// Shift+Tab is a different keysym rather than Tab with a modifier, and every desktop platform has shipped a bug
// where the two stopped being the same key. See react-native-macos#823.
TEST(KeyEventTest, TheShiftTabKeysymIsStillTheTabKey) { EXPECT_EQ(domKeyName("ISO_Left_Tab", ""), "Tab"); }

TEST(KeyEventTest, ASingleCharacterKeysymWinsOverTheTextTheModifiersProduced) {
    EXPECT_EQ(domKeyName("a", "a"), "a");
    EXPECT_EQ(domKeyName("A", "A"), "A");
    EXPECT_EQ(domKeyName("a", "\x01"), "a");
    EXPECT_EQ(domKeyName("1", "1"), "1");
}

TEST(KeyEventTest, PunctuationComesFromTheTextRatherThanFromATableEntryEach) {
    EXPECT_EQ(domKeyName("slash", "/"), "/");
    EXPECT_EQ(domKeyName("exclam", "!"), "!");
    EXPECT_EQ(domKeyName("eacute", "\xC3\xA9"), "\xC3\xA9");
}

TEST(KeyEventTest, AKeyWithNoNameAndNoPrintableTextIsUnidentified) {
    EXPECT_EQ(domKeyName("XF86AudioPlay", ""), "Unidentified");
    EXPECT_EQ(domKeyName("XF86AudioPlay", "\x01"), "Unidentified");
    EXPECT_EQ(domKeyName("XF86AudioPlay", "\x7F"), "Unidentified");
    EXPECT_EQ(domKeyName("", ""), "Unidentified");
}

TEST(KeyEventTest, CodeIsThePhysicalKeyRatherThanTheKeymapOnTopOfIt) {
    constexpr uint32_t kEvdevA = 30;
    constexpr uint32_t kEvdevTab = 15;
    constexpr uint32_t kEvdevSpace = 57;
    constexpr uint32_t kEvdevLeftShift = 42;
    constexpr uint32_t kEvdevUnassigned = 0xFFFF;

    EXPECT_EQ(domKeyCode(kEvdevA), "KeyA");
    EXPECT_EQ(domKeyCode(kEvdevTab), "Tab");
    EXPECT_EQ(domKeyCode(kEvdevSpace), "Space");
    EXPECT_EQ(domKeyCode(kEvdevLeftShift), "ShiftLeft");
    EXPECT_EQ(domKeyCode(kEvdevUnassigned), "Unidentified");
}

TEST(KeyEventTest, ActivationIsTheSameClickThePointerPathProduces) {
    const InputEvent activationKey{
        .kind = InputEventKind::KeyPress, .surfacePoint = makePoint(700, 500), .key = std::string(" ")};
    const PointerDispatch dispatch = makeActivationDispatch(activationKey, makePoint(100, 80));

    EXPECT_EQ(dispatch.type, PointerDispatchType::Click);
    EXPECT_EQ(dispatch.event.detail, 1);
    EXPECT_EQ(dispatch.event.button, 0);
    EXPECT_EQ(dispatch.event.buttons, 0);
    EXPECT_FLOAT_EQ(dispatch.event.clientPoint.x, 100);
    EXPECT_FLOAT_EQ(dispatch.event.clientPoint.y, 80);
    EXPECT_FLOAT_EQ(dispatch.event.offsetPoint.x, 0);
    EXPECT_FLOAT_EQ(dispatch.event.offsetPoint.y, 0);
}

TEST(KeyEventTest, ActivationCarriesTheModifiersTheKeyWasPressedWith) {
    InputEvent activationKey{.kind = InputEventKind::KeyPress, .key = std::string("Enter")};

    activationKey.modifiers = InputModifiers{.control = true, .shift = false, .alt = false, .meta = true};

    const PointerDispatch dispatch = makeActivationDispatch(activationKey, makePoint(0, 0));

    EXPECT_TRUE(dispatch.event.ctrlKey);
    EXPECT_FALSE(dispatch.event.shiftKey);
    EXPECT_TRUE(dispatch.event.metaKey);
}

TEST(PointerRouterTest, BatchesAFullFrameInQueueOrder) {
    InputQueue queue;
    PointerRouter router;

    for (size_t step = 0; step < kHighRateMotionCount; ++step) {
        queue.push(makeMotion(static_cast<float>(step), 0));
    }

    queue.push(makeButton(InputEventKind::PointerButtonPress, kPrimaryButton));
    queue.push(makeButton(InputEventKind::PointerButtonRelease, kPrimaryButton));

    std::vector<PointerDispatchType> types;

    for (const InputEvent& event : queue.drain()) {
        for (const PointerDispatch& dispatch : router.route(event, kBoxTag, makePoint(0, 0))) {
            types.push_back(dispatch.type);
        }
    }

    const std::vector<PointerDispatchType> expected{PointerDispatchType::Move, PointerDispatchType::Down,
                                                    PointerDispatchType::Up, PointerDispatchType::Click};

    EXPECT_EQ(types, expected);
}

// Issue #244. A wheel or a touchpad scroll that starts while a button is down cancels the press, because React's
// responder system cancels one on touch and has no wheel path to do it with.

struct ScrollDuringPressCase {
    const char* name;
    InputEventKind kind;
    ScrollAxisKind scrollAxis;
    double scrollAmount;
    bool cancelsThePress;
};

InputEvent makeScrollEvent(const ScrollDuringPressCase& scrollCase) {
    return InputEvent{
        .kind = scrollCase.kind, .scrollAxis = scrollCase.scrollAxis, .scrollAmount = scrollCase.scrollAmount};
}

std::vector<PointerDispatch> routePrimaryButton(PointerRouter& router, InputEventKind kind) {
    return router.route(makeButton(kind, kPrimaryButton), kBoxTag, makePoint(0, 0));
}

TEST(PointerRouterTest, AScrollThatMovedCancelsThePressItStartedUnder) {
    const std::vector<ScrollDuringPressCase> cases{
        {"a wheel notch down", InputEventKind::PointerScrollDiscrete, ScrollAxisKind::Vertical, 1.0, true},
        {"a wheel notch up", InputEventKind::PointerScrollDiscrete, ScrollAxisKind::Vertical, -1.0, true},
        {"a touchpad delta", InputEventKind::PointerScrollContinuous, ScrollAxisKind::Vertical, -12.5, true},
        {"a horizontal wheel notch", InputEventKind::PointerScrollDiscrete, ScrollAxisKind::Horizontal, 0.25, true},
        {"the fingers leaving the touchpad", InputEventKind::PointerScrollStop, ScrollAxisKind::Vertical, 0.0,
         false},
        {"a delta that coalesced to nothing", InputEventKind::PointerScrollContinuous, ScrollAxisKind::Vertical,
         0.0, false},
    };

    for (const ScrollDuringPressCase& scrollCase : cases) {
        PointerRouter router;

        routePrimaryButton(router, InputEventKind::PointerButtonPress);
        router.cancelPressForScroll(makeScrollEvent(scrollCase));

        const std::vector<PointerDispatch> released =
            routePrimaryButton(router, InputEventKind::PointerButtonRelease);

        // The release is a pointerUp whatever the scroll did, so Pressability reports onPressOut either way; the
        // click is what the cancel removes, and the click is what becomes onPress.
        ASSERT_FALSE(released.empty()) << scrollCase.name;
        EXPECT_EQ(released[0].type, PointerDispatchType::Up) << scrollCase.name;
        EXPECT_EQ(released[0].event.buttons, 0) << scrollCase.name;
        EXPECT_EQ(released.size(), scrollCase.cancelsThePress ? 1U : 2U) << scrollCase.name;
    }
}

TEST(PointerRouterTest, APressThatBeginsAfterTheScrollSettledStillClicks) {
    PointerRouter router;

    router.cancelPressForScroll(InputEvent{.kind = InputEventKind::PointerScrollDiscrete, .scrollAmount = 1.0});
    routePrimaryButton(router, InputEventKind::PointerButtonPress);
    router.cancelPressForScroll(InputEvent{.kind = InputEventKind::PointerScrollStop, .scrollAmount = 0.0});

    const std::vector<PointerDispatch> released = routePrimaryButton(router, InputEventKind::PointerButtonRelease);

    ASSERT_EQ(released.size(), 2U);
    EXPECT_EQ(released[1].type, PointerDispatchType::Click);
}

TEST(PointerRouterTest, ACancelledPressLeavesNothingBehindForTheNextPress) {
    PointerRouter router;

    routePrimaryButton(router, InputEventKind::PointerButtonPress);
    router.cancelPressForScroll(InputEvent{.kind = InputEventKind::PointerScrollContinuous, .scrollAmount = 8.0});
    routePrimaryButton(router, InputEventKind::PointerButtonRelease);
    routePrimaryButton(router, InputEventKind::PointerButtonPress);

    const std::vector<PointerDispatch> clicked = routePrimaryButton(router, InputEventKind::PointerButtonRelease);

    ASSERT_EQ(clicked.size(), 2U);
    EXPECT_EQ(clicked[1].type, PointerDispatchType::Click);
}

TEST(IsTextKeyTest, TreatsASingleCodePointAsTextAndANameAsAKey) {
    EXPECT_TRUE(isTextKey("a"));
    EXPECT_TRUE(isTextKey(" "));
    EXPECT_TRUE(isTextKey("\xC3\xA9"));
    EXPECT_TRUE(isTextKey("\xE4\xBD\xA0"));
    EXPECT_TRUE(isTextKey("\xF0\x9F\x98\x80"));

    EXPECT_FALSE(isTextKey({}));
    EXPECT_FALSE(isTextKey("Enter"));
    EXPECT_FALSE(isTextKey("ArrowLeft"));
    EXPECT_FALSE(isTextKey("\x1B"));
}

TEST(ParseKeySequenceTest, TurnsEveryCharacterIntoAPressAndARelease) {
    const std::vector<InputEvent> events = parseKeySequence("h\xC3\xA9");

    ASSERT_EQ(events.size(), 4U);
    EXPECT_EQ(events[0].kind, InputEventKind::KeyPress);
    EXPECT_EQ(events[0].key, "h");
    EXPECT_EQ(events[1].kind, InputEventKind::KeyRelease);
    EXPECT_EQ(events[2].key, "\xC3\xA9");
    EXPECT_EQ(events[3].kind, InputEventKind::KeyRelease);
}

TEST(ParseKeySequenceTest, NamesAKeyAndItsModifiers) {
    const std::vector<InputEvent> arrow = parseKeySequence("{Left}");

    ASSERT_EQ(arrow.size(), 2U);
    EXPECT_EQ(arrow[0].key, "ArrowLeft");
    EXPECT_FALSE(arrow[0].modifiers.shift);

    const std::vector<InputEvent> shifted = parseKeySequence("{Shift+Left}");

    ASSERT_EQ(shifted.size(), 2U);
    EXPECT_EQ(shifted[0].key, "ArrowLeft");
    EXPECT_TRUE(shifted[0].modifiers.shift);

    const std::vector<InputEvent> selectAll = parseKeySequence("{Ctrl+A}");

    ASSERT_EQ(selectAll.size(), 2U);
    EXPECT_EQ(selectAll[0].key, "a");
    EXPECT_TRUE(selectAll[0].modifiers.control);

    const std::vector<InputEvent> copy = parseKeySequence("{Ctrl+c}");

    ASSERT_EQ(copy.size(), 2U);
    EXPECT_EQ(copy[0].key, "c");

    const std::vector<InputEvent> digit = parseKeySequence("{Ctrl+1}");

    ASSERT_EQ(digit.size(), 2U);
    EXPECT_EQ(digit[0].key, "1");

    const std::vector<InputEvent> combined = parseKeySequence("{Ctrl+Shift+Alt+Left}");

    ASSERT_EQ(combined.size(), 2U);
    EXPECT_TRUE(combined[0].modifiers.control);
    EXPECT_TRUE(combined[0].modifiers.shift);
    EXPECT_TRUE(combined[0].modifiers.alt);
}

TEST(ParseKeySequenceTest, InjectsCompositionEventsFromTheirTokens) {
    const std::vector<InputEvent> preedit = parseKeySequence("{Preedit:ni}");

    ASSERT_EQ(preedit.size(), 1U);
    EXPECT_EQ(preedit[0].kind, InputEventKind::ImePreedit);
    EXPECT_EQ(preedit[0].text, "ni");
    EXPECT_EQ(preedit[0].preeditCursorBegin, 2);
    EXPECT_EQ(preedit[0].preeditCursorEnd, 2);

    const std::vector<InputEvent> commit = parseKeySequence("{Commit:\xE4\xBD\xA0}");

    ASSERT_EQ(commit.size(), 1U);
    EXPECT_EQ(commit[0].kind, InputEventKind::ImeCommit);
    EXPECT_EQ(commit[0].text, "\xE4\xBD\xA0");
}

TEST(ParseKeySequenceTest, IgnoresTokensItDoesNotRecogniseAndStopsAtAnUnclosedOne) {
    EXPECT_TRUE(parseKeySequence("{Nonsense}").empty());
    EXPECT_TRUE(parseKeySequence("{Unknown:payload}").empty());
    EXPECT_TRUE(parseKeySequence("{Ctrl+}").empty());

    const std::vector<InputEvent> truncated = parseKeySequence("a{Left");

    ASSERT_EQ(truncated.size(), 2U);
    EXPECT_EQ(truncated[0].key, "a");
}

#pragma mark - the mouse payload contract (#66)

/**
 * The mapping table of #66: every evdev button code the platform accepts maps to the W3C `button` number React
 * Native's PointerEvent declares, and the codes a mouse nobody sells does not send map to -1, which the seat
 * drops.
 */
TEST(MousePayloadTest, DomButtonOfEvdevCodeMapsTheW3CButtonNumbers) {
    const std::vector<std::pair<uint32_t, int>> table = {
        {BTN_LEFT, 0}, {BTN_MIDDLE, 1}, {BTN_RIGHT, 2},   {BTN_SIDE, 3},
        {BTN_BACK, 3}, {BTN_EXTRA, 4},  {BTN_FORWARD, 4}, {BTN_TOUCH, -1},
    };

    for (const auto& [evdevCode, domButton] : table) {
        EXPECT_EQ(domButtonOfEvdevCode(evdevCode), domButton) << "evdev code " << evdevCode;
    }
}

/**
 * The W3C `buttons` bitmask, pinned per button: 1 primary, 2 secondary, 4 auxiliary, 8 backward, 16 forward.
 * Anything else is a pointer with no buttons held.
 */
TEST(MousePayloadTest, ButtonsMaskOfDomButtonMatchesTheW3CBitmask) {
    const std::vector<std::pair<int, int>> table = {
        {0, 1}, {1, 4}, {2, 2}, {3, 8}, {4, 16}, {kUnmappedButton, 0},
    };

    for (const auto& [domButton, mask] : table) {
        EXPECT_EQ(buttonsMaskOfDomButton(domButton), mask) << "dom button " << domButton;
    }
}

/**
 * Chording: press the primary button, press and release the secondary while it is held. The `buttons` bitmask
 * grows to 3 while both are held, shrinks back to 1 on the secondary release, and the secondary never produces a
 * click even though it went down and up over the same target.
 */
TEST(MousePayloadTest, AChordedPressCarriesTheCombinedBitmaskAndNeverClicksForTheSecondary) {
    PointerRouter router;

    const std::vector<PointerDispatch> primaryDown =
        router.route(makeButton(InputEventKind::PointerButtonPress, kPrimaryButton), kBoxTag, makePoint(0, 0));

    ASSERT_EQ(primaryDown.size(), 1U);
    EXPECT_EQ(primaryDown[0].type, PointerDispatchType::Down);
    EXPECT_EQ(primaryDown[0].event.button, kPrimaryButton);
    EXPECT_EQ(primaryDown[0].event.buttons, kPrimaryButtonsBit);

    const std::vector<PointerDispatch> secondaryDown =
        router.route(makeButton(InputEventKind::PointerButtonPress, kSecondaryButton), kBoxTag, makePoint(0, 0));

    ASSERT_EQ(secondaryDown.size(), 1U);
    EXPECT_EQ(secondaryDown[0].event.button, kSecondaryButton);
    EXPECT_EQ(secondaryDown[0].event.buttons, kPrimaryButtonsBit | kSecondaryButtonsBit);

    const std::vector<PointerDispatch> secondaryUp =
        router.route(makeButton(InputEventKind::PointerButtonRelease, kSecondaryButton), kBoxTag, makePoint(0, 0));

    ASSERT_EQ(secondaryUp.size(), 1U);
    EXPECT_EQ(secondaryUp[0].type, PointerDispatchType::Up);
    EXPECT_EQ(secondaryUp[0].event.button, kSecondaryButton);
    EXPECT_EQ(secondaryUp[0].event.buttons, kPrimaryButtonsBit);

    const std::vector<PointerDispatch> primaryUp =
        router.route(makeButton(InputEventKind::PointerButtonRelease, kPrimaryButton), kBoxTag, makePoint(0, 0));

    ASSERT_EQ(primaryUp.size(), 2U);
    EXPECT_EQ(primaryUp[0].type, PointerDispatchType::Up);
    EXPECT_EQ(primaryUp[0].event.buttons, 0);
    EXPECT_EQ(primaryUp[1].type, PointerDispatchType::Click);
    EXPECT_EQ(primaryUp[1].event.button, kPrimaryButton);
}

/**
 * The behavioural half of the macOS plan, owned by the payload: a secondary or middle press over a target is two
 * dispatches, Down and Up, and no Click — a context menu must be buildable without a native module.
 */
TEST(MousePayloadTest, SecondaryAndMiddlePressesNeverProduceAClick) {
    for (const int button : {kSecondaryButton, kAuxiliaryButton}) {
        PointerRouter router;

        router.route(makeButton(InputEventKind::PointerButtonPress, button), kBoxTag, makePoint(0, 0));
        const std::vector<PointerDispatch> dispatches =
            router.route(makeButton(InputEventKind::PointerButtonRelease, button), kBoxTag, makePoint(0, 0));

        ASSERT_EQ(dispatches.size(), 1U) << "button " << button;
        EXPECT_EQ(dispatches[0].type, PointerDispatchType::Up) << "button " << button;
        EXPECT_EQ(dispatches[0].event.button, button) << "button " << button;
        EXPECT_EQ(dispatches[0].event.buttons, 0) << "button " << button;
    }
}

#pragma mark - the wheel and touchpad source contract (#48)

/**
 * The axis-source routing table of #48, per source value. The axis is what routes: a horizontal axis is
 * horizontal, and shift maps a vertical wheel to the horizontal axis (react-native-macos#1922 - horizontal
 * scrolling from a mouse without a horizontal wheel). The axis_source value itself (wheel, wheel_tilt, finger,
 * continuous, and the two this platform does not distinguish - wheel_button, finger_count) never changes the
 * routing: the event kind that arrives is the path.
 */
TEST(ScrollSourceTest, TheAxisRoutingTableMapsSourcesAndShift) {
    constexpr uint32_t kVerticalAxis = 0;   // WL_POINTER_AXIS_VERTICAL_SCROLL
    constexpr uint32_t kHorizontalAxis = 1; // WL_POINTER_AXIS_HORIZONTAL_SCROLL

    const std::vector<std::tuple<uint32_t, bool, ScrollAxisKind>> table = {
        {kVerticalAxis, false, ScrollAxisKind::Vertical},     // wheel, no shift
        {kVerticalAxis, true, ScrollAxisKind::Horizontal},    // wheel + shift: the #1922 row
        {kHorizontalAxis, false, ScrollAxisKind::Horizontal}, // wheel_tilt / true horizontal
        {kHorizontalAxis, true, ScrollAxisKind::Horizontal},  // shift on horizontal changes nothing
    };

    for (const auto& [waylandAxis, shift, expectedAxis] : table) {
        InputModifiers modifiers;

        modifiers.shift = shift;

        EXPECT_EQ(scrollAxisForPointerAxis(waylandAxis, modifiers), expectedAxis)
            << "axis " << waylandAxis << " shift " << shift;
    }
}

/**
 * The value120 conversion of #48's gap: 120 units are one detent, fractions are kept for the smooth
 * high-resolution wheel, and the sign carries the direction.
 */
TEST(ScrollSourceTest, Value120ConvertsToFractionalNotches) {
    const std::vector<std::pair<int32_t, double>> table = {
        {120, 1.0}, {240, 2.0}, {-120, -1.0}, {-240, -2.0}, {30, 0.25}, {60, 0.5},
    };

    for (const auto& [value120, notches] : table) {
        EXPECT_DOUBLE_EQ(notchesForValue120(value120), notches) << "value120 " << value120;
    }
}

} // namespace
