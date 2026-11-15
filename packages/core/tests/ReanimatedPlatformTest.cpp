#include "ReanimatedPlatform.h"

#include "AnimationFrameQueue.h"
#include "LinuxMountingManager.h"
#include "SceneTestSupport.h"

#include <chrono>
#include <folly/dynamic.h>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Issue #139: Reanimated's platform contract, filled for Linux. Each case drives a member the way
// ReanimatedModuleProxy does and asserts on what it reaches: the frame queue worklets' UI thread dispatches, the
// retained scene behind the mounting manager, or the named error a stub throws.

namespace {

using react_native_linux::AnimationFrameQueue;
using react_native_linux::makePlatformDepMethodsHolder;

constexpr Tag kAnimatedTag = 2;
constexpr double kFirstFrameTimestamp = 16.0;
constexpr double kSecondFrameTimestamp = 24.0;

reanimated::PlatformDepMethodsHolder holderOver(AnimationFrameQueue& frames) {
    return makePlatformDepMethodsHolder(frames, std::make_shared<react_native_linux::LinuxMountingManager>());
}

void expectUnsupported(const std::function<void()>& callMember, const std::string& member) {
    try {
        callMember();
        ADD_FAILURE() << member << " did not throw";
    } catch (const std::runtime_error& error) {
        EXPECT_EQ(std::string(error.what()), "[Reanimated] " + member + " is not supported on Linux yet.");
    }
}

TEST(ReanimatedPlatformTest, RequestRenderDeliversEachTickItsOwnTimestamp) {
    AnimationFrameQueue frames;
    const auto holder = holderOver(frames);
    std::vector<double> timestamps;
    const std::function<void(double)> onRender = [&timestamps](double timestamp) { timestamps.push_back(timestamp); };

    holder.requestRender(onRender);
    frames.dispatchFrame(kFirstFrameTimestamp);
    holder.requestRender(onRender);
    frames.dispatchFrame(kSecondFrameTimestamp);
    frames.dispatchFrame(kSecondFrameTimestamp + kFirstFrameTimestamp);

    EXPECT_EQ(timestamps, (std::vector<double>{kFirstFrameTimestamp, kSecondFrameTimestamp}));
}

TEST(ReanimatedPlatformTest, AnimationTimestampIsTheFrameClocksSteadyClockInMilliseconds) {
    AnimationFrameQueue frames;
    const auto holder = holderOver(frames);
    const auto milliseconds = [](std::chrono::steady_clock::time_point time) {
        return std::chrono::duration<double, std::milli>(time.time_since_epoch()).count();
    };

    const double before = milliseconds(std::chrono::steady_clock::now());
    const double timestamp = holder.getAnimationTimestamp();
    const double after = milliseconds(std::chrono::steady_clock::now());

    EXPECT_LE(before, timestamp);
    EXPECT_LE(timestamp, after);
}

TEST(ReanimatedPlatformTest, SynchronousPropsReachTheMountingManagersScene) {
    AnimationFrameQueue frames;
    auto mountingManager = std::make_shared<react_native_linux::LinuxMountingManager>();
    mountChildAndTakeFrame(*mountingManager,
                           makeStyledView(kAnimatedTag, makeRect(0, 0, 40, 40), propsWithBackground(blue())));
    const auto holder = makePlatformDepMethodsHolder(frames, mountingManager);

    holder.synchronouslyUpdateUIPropsFunction(kAnimatedTag, folly::dynamic::object("opacity", 0.5));

    const auto snapshot = mountingManager->snapshotScene();
    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot.at(0).opensLayers, std::vector<float>{0.5F});
    EXPECT_TRUE(mountingManager->hasPendingDamage());
}

TEST(ReanimatedPlatformTest, MaybeFlushUIUpdatesQueueRequestsAFrame) {
    AnimationFrameQueue frames;
    const auto holder = holderOver(frames);

    holder.maybeFlushUIUpdatesQueueFunction();

    EXPECT_TRUE(frames.hasPendingRequests());
    EXPECT_EQ(frames.dispatchFrame(kFirstFrameTimestamp), 1U);
}

TEST(ReanimatedPlatformTest, UnimplementedMembersThrowAnErrorNamingThemselves) {
    AnimationFrameQueue frames;
    const auto holder = holderOver(frames);

    expectUnsupported([&holder] { holder.registerSensor(0, 0, 0, [](double[], int) {}); }, "registerSensor");
    expectUnsupported([&holder] { holder.unregisterSensor(0); }, "unregisterSensor");
    expectUnsupported([&holder] { holder.setGestureStateFunction(0, 0); }, "setGestureState");
    expectUnsupported([&holder] { holder.subscribeForKeyboardEvents([](int, int) {}, false, false); },
                      "subscribeForKeyboardEvents");
    expectUnsupported([&holder] { holder.unsubscribeFromKeyboardEvents(0); }, "unsubscribeFromKeyboardEvents");
    expectUnsupported(
        [&holder] { holder.attachPseudoSelector(kAnimatedTag, reanimated::PseudoSelector::Hover, [](bool) {}); },
        "attachPseudoSelector");
    expectUnsupported([&holder] { holder.detachPseudoSelector(kAnimatedTag, reanimated::PseudoSelector::Hover); },
                      "detachPseudoSelector");
}

TEST(ReanimatedPlatformTest, CssRoutingMembersAreEmptySoUpstreamRunsEveryAnimationOnItsLoop) {
    AnimationFrameQueue frames;
    const auto holder = holderOver(frames);

    EXPECT_FALSE(holder.cssCanRouteProperty);
    EXPECT_FALSE(holder.cssApplyTransition);
    EXPECT_FALSE(holder.cssRemoveTransition);
    EXPECT_FALSE(holder.cssGetPlatformValue);
    EXPECT_EQ(holder.platformAnimationFactory, nullptr);
}

} // namespace
