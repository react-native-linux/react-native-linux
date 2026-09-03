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

// The one translation unit that can watch the allocator: `AllocationProbe.h` replaces the global operators and
// permits exactly one includer, so the frame cost of #124 and the mounting cost of #106 are measured together.
//
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


// Issue #106. Upstream shipped a first mount that allocated about 6.9 GB of raster data before the bundle had
// finished evaluating (core#56980), and a mounting cost of about a millisecond per view (core#51869). Neither is
// visible without a counter, so this is the counter.
//
// Two different shapes of claim, because the two paths have two different costs:
//
//  - **The mounting transaction is per node** and always will be: each mutation writes a node into the scene. The
//    ceiling is therefore per node, and it is what catches a new container per mounted view.
//  - **The snapshot is not.** It walks the tree into one flat list, so its cost is the handful of reallocations
//    that list's growth costs and nothing else. Asserting that a four-times-larger tree does not cost four times
//    as much is what catches a per-primitive allocation appearing in the walk — a ceiling alone would not, because
//    a per-node cost hides under any ceiling at a small enough node count.
//
// The mount-and-unmount cycle is issue #106's second item, as a count rather than as RSS: a cycle that allocates
// more than the cycle before it is the leak, and it is deterministic here where a resident-set number is not.

constexpr size_t kLargeTreeNodeCount = 2000;
constexpr size_t kSmallTreeNodeCount = 500;
constexpr Tag kFirstCostTag = 100;

// 5.01 measured, on a tree whose nodes each carry a background, a border, a radius and a frame. The ceiling is
// six, which is headroom for one more container per node and not for a second one.
constexpr size_t kMountAllocationsPerNodeCeiling = 6;

// 13 measured for 2000 nodes: the growth of one `SceneSnapshot` vector from empty to 2048, plus the damage list.
// The ceiling is 32, which is what the same doubling costs if the vector ever starts at one element again.
constexpr size_t kSnapshotAllocationCeiling = 32;

// A four-times-larger tree may cost at most one more doubling than twice the smaller one. Linear-in-nodes fails
// this by two orders of magnitude; logarithmic passes it with room.
constexpr size_t kSnapshotGrowthFactorCeiling = 3;

std::shared_ptr<ViewProps> decoratedProps() {
    const std::shared_ptr<ViewProps> viewProps = propsWithBackground(blue());

    viewProps->borderRadii.all = ValueUnit{8.0F, UnitType::Point};
    viewProps->borderColors.all = red();
    viewProps->yogaStyle.setBorder(facebook::yoga::Edge::All, facebook::yoga::StyleLength::points(2));

    return viewProps;
}

ShadowView costView(size_t index) {
    return makeStyledView(static_cast<Tag>(kFirstCostTag + index),
                          makeRect(static_cast<float>(index % 40) * 20.0F, static_cast<float>(index / 40) * 20.0F,
                                   18, 18),
                          decoratedProps());
}

/**
 * A flat tree of `nodeCount` painted views under the surface root, each with the decorations core#56980
 * rasterized per view. Flat rather than deep, because a mounting transaction is a list of mutations and its cost
 * is per mutation rather than per level.
 */
ShadowViewMutationList mountMutations(size_t nodeCount) {
    ShadowViewMutationList mutations;

    for (size_t index = 0; index < nodeCount; index++) {
        const ShadowView child = costView(index);

        mutations.push_back(ShadowViewMutation::CreateMutation(child));
        mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceTag, child, static_cast<int>(index)));
    }

    return mutations;
}

/**
 * The other half of the cycle, in the order Fabric sends it: every child is removed from its parent and then
 * deleted, last one first.
 */
ShadowViewMutationList unmountMutations(size_t nodeCount) {
    ShadowViewMutationList mutations;

    for (size_t index = nodeCount; index > 0; index--) {
        const ShadowView child = costView(index - 1);

        mutations.push_back(ShadowViewMutation::RemoveMutation(kSurfaceTag, child, static_cast<int>(index - 1)));
        mutations.push_back(ShadowViewMutation::DeleteMutation(child));
    }

    return mutations;
}

void startSurface(LinuxMountingManager& mountingManager) {
    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 1200});
}

size_t allocationsMounting(LinuxMountingManager& mountingManager, ShadowViewMutationList&& mutations) {
    return allocationsDuringFrame([&]() {
        mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));
    });
}

TEST(MountingCostTest, AMountingTransactionCostsABoundedNumberOfAllocationsPerNode) {
    LinuxMountingManager mountingManager;

    startSurface(mountingManager);

    const size_t allocations = allocationsMounting(mountingManager, mountMutations(kLargeTreeNodeCount));

    std::cout << "[cost] mount: " << allocations << " allocations for " << kLargeTreeNodeCount << " nodes"
              << std::endl;

    EXPECT_LE(allocations, kMountAllocationsPerNodeCeiling * kLargeTreeNodeCount);
}

TEST(MountingCostTest, ASnapshotCostsItsVectorGrowthRatherThanOneAllocationPerPrimitive) {
    LinuxMountingManager mountingManager;

    startSurface(mountingManager);
    mountingManager.executeMount(kSurfaceTag, transactionOf(mountMutations(kLargeTreeNodeCount)));

    const size_t allocations = allocationsDuringFrame([&]() { mountingManager.takeFrame(); });

    std::cout << "[cost] snapshot: " << allocations << " allocations for " << kLargeTreeNodeCount << " primitives"
              << std::endl;

    EXPECT_LE(allocations, kSnapshotAllocationCeiling);
}

TEST(MountingCostTest, AFourTimesLargerTreeDoesNotSnapshotForFourTimesTheAllocations) {
    LinuxMountingManager smallManager;
    LinuxMountingManager largeManager;

    startSurface(smallManager);
    startSurface(largeManager);
    smallManager.executeMount(kSurfaceTag, transactionOf(mountMutations(kSmallTreeNodeCount)));
    largeManager.executeMount(kSurfaceTag, transactionOf(mountMutations(kLargeTreeNodeCount)));

    const size_t small = allocationsDuringFrame([&]() { smallManager.takeFrame(); });
    const size_t large = allocationsDuringFrame([&]() { largeManager.takeFrame(); });

    std::cout << "[cost] snapshot growth: " << small << " for " << kSmallTreeNodeCount << ", " << large << " for "
              << kLargeTreeNodeCount << std::endl;

    EXPECT_LE(large, small * kSnapshotGrowthFactorCeiling);
}

TEST(MountingCostTest, MountingAndUnmountingTheSameScreenCostsTheSameEveryCycle) {
    LinuxMountingManager mountingManager;

    startSurface(mountingManager);

    // The first cycle also pays for whatever the scene's containers reserve once, so the assertion is between the
    // second and the third — the same shape as the animation frame tests above.
    for (size_t cycle = 0; cycle < 2; cycle++) {
        mountingManager.executeMount(kSurfaceTag, transactionOf(mountMutations(kSmallTreeNodeCount)));
        mountingManager.executeMount(kSurfaceTag, transactionOf(unmountMutations(kSmallTreeNodeCount)));
    }

    const size_t secondCycle = allocationsDuringFrame([&]() {
        mountingManager.executeMount(kSurfaceTag, transactionOf(mountMutations(kSmallTreeNodeCount)));
        mountingManager.executeMount(kSurfaceTag, transactionOf(unmountMutations(kSmallTreeNodeCount)));
    });
    const size_t thirdCycle = allocationsDuringFrame([&]() {
        mountingManager.executeMount(kSurfaceTag, transactionOf(mountMutations(kSmallTreeNodeCount)));
        mountingManager.executeMount(kSurfaceTag, transactionOf(unmountMutations(kSmallTreeNodeCount)));
    });

    std::cout << "[cost] cycle: " << secondCycle << " then " << thirdCycle << std::endl;

    EXPECT_EQ(secondCycle, thirdCycle);
}

} // namespace
