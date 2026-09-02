#include "InputPipeline.h"

#include <gtest/gtest.h>

#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/graphics/Point.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using facebook::react::Point;
using facebook::react::Tag;
using react_native_linux::InputEvent;
using react_native_linux::InputEventKind;
using react_native_linux::InputModifiers;
using react_native_linux::InputQueue;
using react_native_linux::kInputQueueCapacity;
using react_native_linux::domKeyCode;
using react_native_linux::domKeyName;
using react_native_linux::isTextKey;
using react_native_linux::parseKeySequence;
using react_native_linux::makeActivationDispatch;
using react_native_linux::PointerDispatch;
using react_native_linux::PointerDispatchType;
using react_native_linux::PointerRouter;

constexpr Tag kBoxTag = 4;
constexpr Tag kOtherTag = 5;
constexpr int kPrimaryButton = 0;
constexpr int kAuxiliaryButton = 1;
constexpr int kSecondaryButton = 2;
constexpr int kUnmappedButton = 9;
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

    const std::vector<PointerDispatch> dispatches =
        router.route(makeMotion(150, 120), kBoxTag, makePoint(100, 80));

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
TEST(KeyEventTest, TheShiftTabKeysymIsStillTheTabKey) {
    EXPECT_EQ(domKeyName("ISO_Left_Tab", ""), "Tab");
}

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

} // namespace
