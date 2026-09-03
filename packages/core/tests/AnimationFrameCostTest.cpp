#include "AllocationProbe.h"

#include "LinuxAnimationChoreographer.h"
#include "LinuxMountingManager.h"
#include "RetainedScene.h"
#include "SceneTestSupport.h"

#include <gtest/gtest.h>

#include <folly/dynamic.h>
#include <react/renderer/uimanager/UIManagerAnimationBackend.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>

// Issue #124: the unit half of the native-driver frame-cost gate. The e2e half asserts the p95 frame time of a
// continuously animating view; this half asserts the thing that makes that number move — how many times a steady
// state animation frame reaches the allocator on the frame thread.
//
// A frame here is the two calls the window makes between delivering input and taking the scene:
// `LinuxAnimationChoreographer::tick`, which hands the backend the frame's timestamp, and
// `LinuxMountingManager::synchronouslyUpdateViewOnUIThread`, which is where the non-layout half of that frame's
// mutations lands. Every measurement is taken after warm-up frames, because the first frame through any of these
// paths also pays for whatever the runtime, the feature flags and the scene's own containers initialise once.
//
// The numbers are ceilings with reasons, not aspirations: `EXPECT_LE` against a stated constant, plus an
// `EXPECT_EQ` between two consecutive steady state frames, which is the assertion that actually catches a
// regression — a path that starts allocating per frame stops being equal to the frame before it long before it
// crosses any ceiling. Each test prints what it measured, so the ceilings are tracked rather than vibed. See
// *Animation frame cost (#124)* in docs/cpp-toolchain.md.

namespace {

using react_native_linux::AllocationScope;
using react_native_linux::allocationsDuringFrame;
using react_native_linux::LinuxAnimationChoreographer;

constexpr Tag kCostTag = 2;
constexpr size_t kWarmUpFrames = 2;
constexpr double kHalfOpacity = 0.5;
constexpr int64_t kFrameIntervalMilliseconds = 16;

/**
 * What `applyAnimatedProps` costs for `opacity` and `backgroundColor`, which write one field each and then damage
 * the node twice.
 *
 * Six allocations, all of them the damage bracket: `damageSubtree` runs twice, and each run builds one
 * `std::vector<const SceneNode*>` of ancestors and one `SceneSnapshot` of the subtree's primitives, then appends
 * one rectangle to a damage list that `takeDamage` moved the buffer out of. Nothing in the payload itself
 * allocates: the prop names are short enough for the small-string buffer, `SharedColor` is an `int32_t` wrapper on
 * this platform, and the rejected-prop list stays empty. The ceiling is set at twice the read count so an
 * incidental container in the walk is not a build break, and the frame-to-frame equality is what holds the line.
 */
constexpr size_t kPaintPropFrameAllocationCeiling = 12;

/**
 * What the same frame costs when the payload is a `transform` array, which is the paint-prop cost plus a
 * `folly::dynamic` parse that inherently allocates.
 *
 * `parseAnimatedTransform` hands the payload to upstream's `fromRawValue`, and `parseProcessedTransform` copies
 * it three times on the way to a `Transform`: into the `RawValue`'s own `folly::dynamic`, into a
 * `std::vector<RawValue>` of operations, and into a `std::unordered_map<std::string, RawValue>` per operation —
 * each copy carrying the operation object's own node and bucket array — before pushing the parsed operation onto
 * `Transform::operations` and copying that vector into the result. That is upstream's parser, reached through the
 * same overload `ViewProps` parsing uses on purpose, so this is a ceiling rather than a bug. The follow-up that
 * removes it is in the docs: pre-parse the transform into a POD in the animated payload, which is a change to what
 * `packAnimatedProps` sends rather than to anything on this side.
 */
constexpr size_t kTransformFrameAllocationCeiling = 64;

/**
 * The backend half of the choreographer seam, reduced to two scalars. It counts frames instead of recording them
 * because a `std::vector<double>` of timestamps would grow inside the measured scope and the measurement would be
 * of the test rather than of the path.
 */
class FrameCostAnimationBackend final : public facebook::react::UIManagerAnimationBackend {
public:
    void onAnimationFrame(facebook::react::AnimationTimestamp timestamp) override {
        lastTimestampMilliseconds = timestamp.count();
        deliveredFrames++;
    }

    void trigger() override {}

    void stop(facebook::react::CallbackId /*callbackId*/) override {}

    facebook::react::CallbackId start(const Callback& /*callback*/) override { return 0; }

    void registerJSInvoker(std::shared_ptr<facebook::react::CallInvoker> /*jsInvoker*/) override {}

    void pushAnimationMutations(const Callback& /*callback*/) override {}

    void clearRegistryOnSurfaceStop(facebook::react::SurfaceId /*surfaceId*/) override {}

    void clearRegistry(facebook::react::SurfaceId /*surfaceId*/) override {}

    double lastTimestampMilliseconds{0.0};
    size_t deliveredFrames{0};
};

/**
 * What two consecutive steady state frames cost. Equality between them is the regression assertion; the ceiling is
 * the budget.
 */
struct SteadyStateCost {
    size_t earlierFrameAllocations{0};
    size_t laterFrameAllocations{0};
};

void reportFrameCost(const char* pathName, size_t allocations) {
    std::cout << "[ frame cost ] " << pathName << ": " << allocations << " allocations\n";
}

std::chrono::steady_clock::time_point frameTimeAt(size_t frameNumber) {
    return std::chrono::steady_clock::time_point(
        std::chrono::milliseconds(kFrameIntervalMilliseconds * static_cast<int64_t>(frameNumber + 1)));
}

Rect costFrame() {
    return makeRect(24, 24, 120, 80);
}

folly::dynamic payloadOf(const std::string& propName, const folly::dynamic& value) {
    return folly::dynamic::object(propName, value);
}

// `{"translateX": 20}`, which is what upstream's `serializeTransformAxis` produces for a single-axis translation
// and therefore the smallest payload the transform parse can be measured on.
folly::dynamic translationPayload() {
    return payloadOf("transform", folly::dynamic::array(folly::dynamic::object("translateX", 20.0)));
}

void mountCostNode(LinuxMountingManager& mountingManager) {
    mountChildAndTakeFrame(mountingManager, makePaintedView(kCostTag, costFrame(), blue()));
}

/**
 * Drives the sync-props fast path with the same payload every frame — which is what a native driver does, since
 * the value changes and the shape does not — and hands back what the last two frames cost. `takeFrame` runs
 * outside every measured scope: it is the frame's snapshot and its damage, and both belong to the paint half.
 */
SteadyStateCost measureSyncPropFrames(const folly::dynamic& props) {
    LinuxMountingManager mountingManager;

    mountCostNode(mountingManager);

    for (size_t frame = 0; frame < kWarmUpFrames; ++frame) {
        mountingManager.synchronouslyUpdateViewOnUIThread(kCostTag, props);
        mountingManager.takeFrame();
    }

    SteadyStateCost cost;
    const auto animationFrame = [&mountingManager, &props]() {
        mountingManager.synchronouslyUpdateViewOnUIThread(kCostTag, props);
    };

    cost.earlierFrameAllocations = allocationsDuringFrame(animationFrame);
    mountingManager.takeFrame();
    cost.laterFrameAllocations = allocationsDuringFrame(animationFrame);
    mountingManager.takeFrame();

    return cost;
}

TEST(AnimationFrameCostTest, ATickThroughTheChoreographerReachesTheAllocatorNeitherWayRound) {
    const std::shared_ptr<FrameCostAnimationBackend> backend = std::make_shared<FrameCostAnimationBackend>();
    LinuxAnimationChoreographer choreographer;

    choreographer.setAnimationBackend(backend);
    choreographer.resume();

    for (size_t frame = 0; frame < kWarmUpFrames; ++frame) {
        choreographer.tick(frameTimeAt(frame));
    }

    size_t allocations = 0;
    size_t deallocations = 0;

    {
        const AllocationScope scope;

        choreographer.tick(frameTimeAt(kWarmUpFrames));
        allocations = scope.allocations();
        deallocations = scope.deallocations();
    }

    reportFrameCost("choreographer.tick", allocations);

    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(deallocations, 0U);
    EXPECT_EQ(backend->deliveredFrames, kWarmUpFrames + 1);
    EXPECT_DOUBLE_EQ(backend->lastTimestampMilliseconds,
                     static_cast<double>(kFrameIntervalMilliseconds) * static_cast<double>(kWarmUpFrames + 1));
}

TEST(AnimationFrameCostTest, AnOpacityFrameStaysUnderThePaintPropCeilingAndCostsWhatTheFrameBeforeItCost) {
    const SteadyStateCost cost = measureSyncPropFrames(payloadOf("opacity", kHalfOpacity));

    reportFrameCost("syncProps.opacity", cost.laterFrameAllocations);

    EXPECT_EQ(cost.laterFrameAllocations, cost.earlierFrameAllocations);
    EXPECT_LE(cost.laterFrameAllocations, kPaintPropFrameAllocationCeiling);
}

TEST(AnimationFrameCostTest, ABackgroundColourFrameStaysUnderThePaintPropCeilingAndCostsWhatTheFrameBeforeItCost) {
    const SteadyStateCost cost = measureSyncPropFrames(payloadOf("backgroundColor", static_cast<int32_t>(kRedArgb)));

    reportFrameCost("syncProps.backgroundColor", cost.laterFrameAllocations);

    EXPECT_EQ(cost.laterFrameAllocations, cost.earlierFrameAllocations);
    EXPECT_LE(cost.laterFrameAllocations, kPaintPropFrameAllocationCeiling);
}

TEST(AnimationFrameCostTest, ATransformFrameStaysUnderTheParseCeilingAndCostsWhatTheFrameBeforeItCost) {
    const SteadyStateCost cost = measureSyncPropFrames(translationPayload());

    reportFrameCost("syncProps.transform", cost.laterFrameAllocations);

    EXPECT_EQ(cost.laterFrameAllocations, cost.earlierFrameAllocations);
    EXPECT_LE(cost.laterFrameAllocations, kTransformFrameAllocationCeiling);
}

TEST(AnimationFrameCostTest, TakingTheDamageListAllocatesNothingAndTheNextFrameRebuildsItAtTheSameCost) {
    RetainedScene scene;
    const folly::dynamic props = payloadOf("opacity", kHalfOpacity);

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makePaintedView(kCostTag, costFrame(), blue()));
    scene.takeDamage();

    for (size_t frame = 0; frame < kWarmUpFrames; ++frame) {
        scene.applyAnimatedProps(kCostTag, props);
        scene.takeDamage();
    }

    const auto damageFrame = [&scene, &props]() { scene.applyAnimatedProps(kCostTag, props); };
    SteadyStateCost cost;

    cost.earlierFrameAllocations = allocationsDuringFrame(damageFrame);

    SceneDamage damage;
    size_t takeAllocations = 0;

    {
        const AllocationScope scope;

        damage = scene.takeDamage();
        takeAllocations = scope.allocations();
    }

    cost.laterFrameAllocations = allocationsDuringFrame(damageFrame);
    scene.takeDamage();

    reportFrameCost("retainedScene.animatedDamage", cost.laterFrameAllocations);

    EXPECT_EQ(takeAllocations, 0U);
    EXPECT_EQ(damage.size(), 2U);
    EXPECT_EQ(cost.laterFrameAllocations, cost.earlierFrameAllocations);
    EXPECT_LE(cost.laterFrameAllocations, kPaintPropFrameAllocationCeiling);
}

} // namespace
