#include "LinuxAnimationChoreographer.h"

#include <gtest/gtest.h>

#include <react/renderer/animationbackend/AnimationBackend.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

using react_native_linux::LinuxAnimationChoreographer;

/**
 * The backend half of the seam, reduced to what the choreographer can observe: every timestamp `onAnimationFrame`
 * was handed, in order. Everything else `UIManagerAnimationBackend` declares belongs to `AnimationBackend`'s own
 * registry and is never reached from this side.
 */
class RecordingAnimationBackend final : public facebook::react::UIManagerAnimationBackend {
public:
    void onAnimationFrame(facebook::react::AnimationTimestamp timestamp) override {
        deliveredMilliseconds.push_back(timestamp.count());
    }

    facebook::react::CallbackId start(const Callback& /*callback*/) override { return 0; }

    void stop(facebook::react::CallbackId /*callbackId*/) override {}

    void clearRegistry(facebook::react::SurfaceId /*surfaceId*/) override {}

    void clearRegistryOnSurfaceStop(facebook::react::SurfaceId /*surfaceId*/) override {}

    void trigger() override {}

    void pushAnimationMutations(const Callback& /*callback*/) override {}

    void registerJSInvoker(std::shared_ptr<facebook::react::CallInvoker> /*jsInvoker*/) override {}

    std::vector<double> deliveredMilliseconds;
};

std::chrono::steady_clock::time_point timeAt(int64_t milliseconds) {
    return std::chrono::steady_clock::time_point(std::chrono::milliseconds(milliseconds));
}

TEST(AnimationChoreographerTest, ResumeReportsTheChoreographerAsActive) {
    LinuxAnimationChoreographer choreographer;

    EXPECT_FALSE(choreographer.isActive());

    choreographer.resume();

    EXPECT_TRUE(choreographer.isActive());
}

TEST(AnimationChoreographerTest, PauseReportsTheChoreographerAsInactive) {
    LinuxAnimationChoreographer choreographer;

    choreographer.resume();
    choreographer.pause();

    EXPECT_FALSE(choreographer.isActive());
}

TEST(AnimationChoreographerTest, ATickWhileInactiveDeliversNothing) {
    const std::shared_ptr<RecordingAnimationBackend> backend = std::make_shared<RecordingAnimationBackend>();
    LinuxAnimationChoreographer choreographer;

    choreographer.setAnimationBackend(backend);
    choreographer.tick(timeAt(16));

    EXPECT_TRUE(backend->deliveredMilliseconds.empty());
}

TEST(AnimationChoreographerTest, ATickWhileActiveDeliversTheTicksOwnTimestampExactlyOnce) {
    const std::shared_ptr<RecordingAnimationBackend> backend = std::make_shared<RecordingAnimationBackend>();
    LinuxAnimationChoreographer choreographer;

    choreographer.setAnimationBackend(backend);
    choreographer.resume();
    choreographer.tick(timeAt(16));

    ASSERT_EQ(backend->deliveredMilliseconds.size(), 1U);
    EXPECT_DOUBLE_EQ(backend->deliveredMilliseconds.front(), 16.0);
}

TEST(AnimationChoreographerTest, ResumingTwiceDeliversOneFramePerTick) {
    const std::shared_ptr<RecordingAnimationBackend> backend = std::make_shared<RecordingAnimationBackend>();
    LinuxAnimationChoreographer choreographer;

    choreographer.setAnimationBackend(backend);
    choreographer.resume();
    choreographer.resume();
    choreographer.tick(timeAt(16));

    EXPECT_TRUE(choreographer.isActive());
    EXPECT_EQ(backend->deliveredMilliseconds.size(), 1U);
}

TEST(AnimationChoreographerTest, PausingWithoutResumingIsHarmless) {
    const std::shared_ptr<RecordingAnimationBackend> backend = std::make_shared<RecordingAnimationBackend>();
    LinuxAnimationChoreographer choreographer;

    choreographer.setAnimationBackend(backend);
    choreographer.pause();
    choreographer.tick(timeAt(16));

    EXPECT_FALSE(choreographer.isActive());
    EXPECT_TRUE(backend->deliveredMilliseconds.empty());
}

TEST(AnimationChoreographerTest, EveryTickBetweenResumeAndPauseIsDeliveredInOrder) {
    const std::shared_ptr<RecordingAnimationBackend> backend = std::make_shared<RecordingAnimationBackend>();
    LinuxAnimationChoreographer choreographer;

    choreographer.setAnimationBackend(backend);
    choreographer.resume();

    for (int64_t millisecond = 16; millisecond <= 64; millisecond += 16) {
        choreographer.tick(timeAt(millisecond));
    }

    choreographer.pause();
    choreographer.tick(timeAt(80));

    EXPECT_EQ(backend->deliveredMilliseconds, (std::vector<double>{16.0, 32.0, 48.0, 64.0}));
}

TEST(AnimationChoreographerTest, ADestroyedBackendMakesATickANoOpRatherThanACrash) {
    LinuxAnimationChoreographer choreographer;

    {
        const std::shared_ptr<RecordingAnimationBackend> backend = std::make_shared<RecordingAnimationBackend>();

        choreographer.setAnimationBackend(backend);
    }

    choreographer.resume();
    choreographer.tick(timeAt(16));

    EXPECT_TRUE(choreographer.isActive());
}

TEST(AnimationChoreographerTest, TheDefaultNowIsSteadyClockMillisecondsSinceEpoch) {
    const LinuxAnimationChoreographer choreographer;
    const double before =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
    const double reported = choreographer.now().count();
    const double after =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();

    EXPECT_GE(reported, before);
    EXPECT_LE(reported, after);
}

} // namespace
