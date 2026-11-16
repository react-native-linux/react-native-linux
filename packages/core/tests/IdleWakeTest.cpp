#include "AnimationFrameQueue.h"
#include "FrameClock.h"
#include "HostTimerRegistry.h"
#include "LinuxAnimationChoreographer.h"
#include "LinuxMountingManager.h"

#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <thread>

#include <react/renderer/mounting/MountingTransaction.h>

namespace {

using react_native_linux::FrameClock;

// Polling while the producer runs is what lets TSan see an unsynchronised signal: a read after `join` alone is
// ordered by the join itself, so a missing atomic or lock would pass unnoticed.
constexpr std::chrono::seconds kObserveDeadline{5};

/**
 * #703's wake contract, one producer at a time: an idle visible window skips the callback tick, which stops the
 * presents, and the first tick after a producer on another thread publishes work draws again. Each producer runs on
 * its own `std::thread`, the way the JavaScript, decode and timer threads reach the frame thread, so TSan sees the
 * real cross-thread read of every pending-work signal `WindowSession::hasPendingWork` composes. A producer missing
 * from that composition, or one whose signal is not published across threads, is a frozen UI rather than a slow one.
 */
void expectProducerWakesAnIdleClock(const std::function<bool()>& hasPendingWork, const std::function<void()>& produce) {
    FrameClock clock;
    const std::chrono::steady_clock::time_point start{};

    ASSERT_FALSE(clock.onFrameCallback(start, hasPendingWork()).shouldDraw);

    std::thread producer(produce);
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kObserveDeadline;
    bool isObservedWhileProducing = hasPendingWork();

    while (!isObservedWhileProducing && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
        isObservedWhileProducing = hasPendingWork();
    }

    producer.join();

    EXPECT_TRUE(isObservedWhileProducing);

    const FrameClock::Tick wake = clock.onFallbackTimeout(start + std::chrono::milliseconds(16), hasPendingWork());

    EXPECT_TRUE(wake.shouldDraw);
    EXPECT_EQ(wake.source, FrameClock::Source::Timer);
}

void commitOneView(react_native_linux::LinuxMountingManager& mountingManager) {
    facebook::react::ShadowView view;
    view.tag = 2;
    facebook::react::ShadowViewMutation::List mutations;
    mutations.push_back(facebook::react::ShadowViewMutation::CreateMutation(view));
    mountingManager.executeMount(
        1, facebook::react::MountingTransaction{1, 1, std::move(mutations), facebook::react::TransactionTelemetry{}});
}

TEST(IdleWakeTest, AJavaScriptCommitWakesAnIdleWindow) {
    react_native_linux::LinuxMountingManager mountingManager;

    expectProducerWakesAnIdleClock([&] { return mountingManager.hasPendingDamage(); },
                                   [&] { commitOneView(mountingManager); });
}

/**
 * The draw a commit wakes takes its damage before it knows whether it presents. When it presents nothing (a
 * recoverable image-acquire failure) and arms no callback, the next tick still draws, or the commit would stay
 * invisible on an idle window (#703).
 */
TEST(IdleWakeTest, ACommitWhoseDrawPresentedNothingIsDrawnAgainOnTheNextTick) {
    react_native_linux::LinuxMountingManager mountingManager;
    FrameClock clock;
    const auto drawsAt = [&](int milliseconds) {
        return clock
            .onFallbackTimeout(std::chrono::steady_clock::time_point{} + std::chrono::milliseconds(milliseconds),
                               mountingManager.hasPendingDamage())
            .shouldDraw;
    };

    std::thread([&] { commitOneView(mountingManager); }).join();

    ASSERT_TRUE(drawsAt(16));
    mountingManager.takeFrame();
    clock.recordDrawOutcome(false);

    ASSERT_FALSE(mountingManager.hasPendingDamage());
    EXPECT_TRUE(drawsAt(32));

    clock.recordDrawOutcome(true);

    EXPECT_FALSE(drawsAt(48));
}

TEST(IdleWakeTest, AnImageDecodeWakesAnIdleWindow) {
    react_native_linux::LinuxMountingManager mountingManager;

    expectProducerWakesAnIdleClock([&] { return mountingManager.hasPendingDamage(); },
                                   [&] { mountingManager.damageImageSource("https://example.test/a.png", nullptr); });
}

TEST(IdleWakeTest, AnAnimationFrameRequestWakesAnIdleWindow) {
    react_native_linux::AnimationFrameQueue queue;

    expectProducerWakesAnIdleClock([&] { return queue.hasPendingRequests(); },
                                   [&] { queue.request([](double /*frameTimestamp*/) {}); });
}

/**
 * A pending JS timer is not pending work (#703): an app holding a 60 s interval lets the window go idle, and the commit
 * its callback makes when it fires is what wakes it, exactly like any other commit.
 */
TEST(IdleWakeTest, APendingTimerLeavesAnIdleWindowAsleepUntilItCommits) {
    react_native_linux::HostTimerRegistry registry;
    react_native_linux::LinuxMountingManager mountingManager;
    FrameClock clock;
    const std::chrono::steady_clock::time_point start{};

    ASSERT_FALSE(clock.onFrameCallback(start, mountingManager.hasPendingDamage()).shouldDraw);

    std::thread([&] { registry.createTimer(1, 60'000.0); }).join();

    ASSERT_TRUE(registry.hasPendingTimers());
    EXPECT_FALSE(
        clock.onFallbackTimeout(start + std::chrono::milliseconds(16), mountingManager.hasPendingDamage()).shouldDraw);

    std::thread([&] { commitOneView(mountingManager); }).join();

    EXPECT_TRUE(
        clock.onFallbackTimeout(start + std::chrono::milliseconds(32), mountingManager.hasPendingDamage()).shouldDraw);

    registry.deleteTimer(1);
}

/**
 * `TextInputController` publishes every field's editor state on every frame, so a mounted field nobody focused must
 * not count as damage (#703), while a caret blink, which changes the state, still wakes the window.
 */
TEST(IdleWakeTest, AnUnchangedEditorStateLeavesAnIdleWindowAsleepAndACaretBlinkWakesIt) {
    react_native_linux::LinuxMountingManager mountingManager;
    FrameClock clock;
    const std::chrono::steady_clock::time_point start{};

    commitOneView(mountingManager);
    mountingManager.takeFrame();

    ASSERT_FALSE(clock.onFrameCallback(start, mountingManager.hasPendingDamage()).shouldDraw);

    std::thread([&] { mountingManager.setEditorState(2, react_native_linux::SceneEditorState{}); }).join();

    EXPECT_FALSE(
        clock.onFallbackTimeout(start + std::chrono::milliseconds(16), mountingManager.hasPendingDamage()).shouldDraw);

    expectProducerWakesAnIdleClock(
        [&] { return mountingManager.hasPendingDamage(); },
        [&] { mountingManager.setEditorState(2, react_native_linux::SceneEditorState{.isCaretVisible = true}); });
}

TEST(IdleWakeTest, TheAnimationBackendStartingWakesAnIdleWindow) {
    react_native_linux::LinuxAnimationChoreographer choreographer;

    expectProducerWakesAnIdleClock([&] { return choreographer.isActive(); }, [&] { choreographer.resume(); });
}

} // namespace
