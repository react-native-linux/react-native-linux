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
    producer.join();

    const FrameClock::Tick wake = clock.onFallbackTimeout(start + std::chrono::milliseconds(16), hasPendingWork());

    EXPECT_TRUE(wake.shouldDraw);
    EXPECT_EQ(wake.source, FrameClock::Source::Timer);
}

TEST(IdleWakeTest, AJavaScriptCommitWakesAnIdleWindow) {
    react_native_linux::LinuxMountingManager mountingManager;

    expectProducerWakesAnIdleClock([&] { return mountingManager.hasPendingDamage(); },
                                   [&] {
                                       facebook::react::ShadowView view;
                                       view.tag = 2;
                                       facebook::react::ShadowViewMutation::List mutations;
                                       mutations.push_back(facebook::react::ShadowViewMutation::CreateMutation(view));
                                       mountingManager.executeMount(
                                           1, facebook::react::MountingTransaction{
                                                  1, 1, std::move(mutations), facebook::react::TransactionTelemetry{}});
                                   });
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

TEST(IdleWakeTest, ATimerWakesAnIdleWindow) {
    react_native_linux::HostTimerRegistry registry;

    expectProducerWakesAnIdleClock([&] { return registry.hasPendingTimers(); },
                                   [&] { registry.createTimer(1, 60'000.0); });

    registry.deleteTimer(1);
}

TEST(IdleWakeTest, TheAnimationBackendStartingWakesAnIdleWindow) {
    react_native_linux::LinuxAnimationChoreographer choreographer;

    expectProducerWakesAnIdleClock([&] { return choreographer.isActive(); }, [&] { choreographer.resume(); });
}

} // namespace
