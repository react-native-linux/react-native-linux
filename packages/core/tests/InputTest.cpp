#include "InputPipeline.h"

#include <gtest/gtest.h>

#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/graphics/Point.h>

#include <cstddef>
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

} // namespace
