#include "LinuxMountingManager.h"
#include "RetainedScene.h"
#include "SceneTestSupport.h"

#include <gtest/gtest.h>

#include <atomic>
#include <latch>
#include <thread>

#include <folly/dynamic.h>
#include <react/renderer/graphics/Transform.h>
#include <react/renderer/graphics/TransformUtils.h>
#include <react/renderer/graphics/ValueUnit.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

// Issue #130: `AnimationBackend` splits every animation frame by whether its mutations touch layout, and the half
// that does not reaches this platform as `synchronouslyUpdateViewOnUIThread` — no Fabric commit, no Yoga relayout,
// straight into the retained scene. This file holds what that path promises: the props it applies land where a
// commit would have put them, the props it does not apply are counted rather than dropped in silence, and the
// frame after one repaints the node. See *Sync props fast path* in docs/cpp-toolchain.md.
//
// The transform cases are equality tests against the commit path on purpose. The payload is built with upstream's
// own `updateTransformProps`, which is what `animationbackend::packAnimatedProps` serializes a `Transform` with, so
// what the fast path parses is what the driver would have sent — and the matrix it produces is compared against the
// matrix the same `Transform` produces when it arrives on `ViewProps` through a mounting transaction.

namespace {

using facebook::react::TransformOperation;
using facebook::react::TransformOperationType;
using facebook::react::TransformOrigin;
using react_native_linux::MountDiagnostics;
using react_native_linux::SceneMatrix;

constexpr Tag kAnimatedTag = 2;
constexpr Tag kUnknownTag = 404;
constexpr double kHalfOpacity = 0.5;
constexpr uint32_t kHalfBlueArgb = 0x803366CCU;
constexpr uint32_t kHalfRedArgb = 0x80CC3333U;
constexpr float kRotationRadians = 0.5F;
constexpr float kPercentTranslation = 50.0F;
constexpr float kResolvedPercentTranslation = 60.0F;
constexpr size_t kMatrixValueCount = 6;

Rect animatedFrame() {
    return makeRect(24, 24, 120, 80);
}

folly::dynamic animatedProp(const std::string& propName, const folly::dynamic& value) {
    return folly::dynamic::object(propName, value);
}

ShadowView animatedChild(const std::shared_ptr<ViewProps>& viewProps) {
    return makeStyledView(kAnimatedTag, animatedFrame(), viewProps);
}

SceneSnapshot snapshotAfterAnimating(const std::shared_ptr<ViewProps>& viewProps, const folly::dynamic& props) {
    LinuxMountingManager mountingManager;

    mountChildAndTakeFrame(mountingManager, animatedChild(viewProps));
    mountingManager.synchronouslyUpdateViewOnUIThread(kAnimatedTag, props);

    return mountingManager.snapshotScene();
}

// An empty payload is how a test asks for the commit path: the node mounts with the props it is given and the
// synchronous update changes nothing.
SceneMatrix matrixAfterAnimating(const std::shared_ptr<ViewProps>& viewProps, const folly::dynamic& props) {
    return snapshotAfterAnimating(viewProps, props).at(0).matrix;
}

// `animationbackend::packAnimatedProps`'s transform case, spelled out: every operation through upstream's own
// serializer, under the `transform` key.
folly::dynamic packedTransform(const Transform& transform) {
    folly::dynamic operations = folly::dynamic::array();

    for (const TransformOperation& operation : transform.operations) {
        facebook::react::updateTransformProps(transform, operation, operations);
    }

    return animatedProp("transform", operations);
}

TransformOperation translateOperation(ValueUnit x, ValueUnit y) {
    return TransformOperation{.type = TransformOperationType::Translate,
                              .x = x,
                              .y = y,
                              .z = ValueUnit(0.0F, UnitType::Point)};
}

Transform translateThenRotate() {
    Transform transform;

    transform.operations.push_back(
        translateOperation(ValueUnit(20.0F, UnitType::Point), ValueUnit(10.0F, UnitType::Point)));
    transform.operations.push_back(TransformOperation{.type = TransformOperationType::Rotate,
                                                      .x = ValueUnit(0.0F, UnitType::Point),
                                                      .y = ValueUnit(0.0F, UnitType::Point),
                                                      .z = ValueUnit(kRotationRadians, UnitType::Point)});

    return transform;
}

// The one operation that cannot be resolved without the node's frame: half of the 120pt width is 60pt.
Transform percentTranslation() {
    Transform transform;

    transform.operations.push_back(
        translateOperation(ValueUnit(kPercentTranslation, UnitType::Percent), ValueUnit(0.0F, UnitType::Point)));

    return transform;
}

TransformOrigin topLeftOrigin() {
    return TransformOrigin{.xy = {ValueUnit(0.0F, UnitType::Point), ValueUnit(0.0F, UnitType::Point)}, .z = 0.0F};
}

std::array<float, kMatrixValueCount> matrixValues(const SceneMatrix& matrix) {
    return {matrix.scaleX, matrix.skewX, matrix.translateX, matrix.skewY, matrix.scaleY, matrix.translateY};
}

void expectSameMatrix(const SceneMatrix& actual, const SceneMatrix& expected) {
    const std::array<float, kMatrixValueCount> actualValues = matrixValues(actual);
    const std::array<float, kMatrixValueCount> expectedValues = matrixValues(expected);

    for (size_t index = 0; index < kMatrixValueCount; ++index) {
        EXPECT_FLOAT_EQ(actualValues.at(index), expectedValues.at(index));
    }
}

// The equality the fast path exists to satisfy, asserted once: the same `Transform` mounted on `ViewProps` through
// a transaction, and packed into an animated payload, resolve to the same matrix on a node carrying the same
// props. Hands back the matrix the fast path produced, for whatever else a case has to say about it.
SceneMatrix animatedMatrixMatchingTheCommitPath(const Transform& transform,
                                                const std::shared_ptr<ViewProps>& viewProps) {
    const std::shared_ptr<ViewProps> committedProps = propsWithBackground(blue());

    committedProps->transform = transform;
    committedProps->transformOrigin = viewProps->transformOrigin;

    const SceneMatrix committed = matrixAfterAnimating(committedProps, folly::dynamic::object());
    const SceneMatrix animated = matrixAfterAnimating(viewProps, packedTransform(transform));

    expectSameMatrix(animated, committed);

    return animated;
}

Rect damageBounds(const SceneDamage& damage) {
    Rect bounds = damage.front();

    for (const Rect& rect : damage) {
        bounds.unionInPlace(rect);
    }

    return bounds;
}

TEST(AnimatedPropsTest, AnOpacityUpdateFoldsIntoTheColoursTheNextFramePaints) {
    const SceneSnapshot snapshot =
        snapshotAfterAnimating(propsWithBackground(blue()), animatedProp("opacity", kHalfOpacity));

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot.at(0).backgroundColorArgb, kHalfBlueArgb);
}

TEST(AnimatedPropsTest, AnOpacityUpdateDamagesTheNodeAndTheNextFrameCarriesIt) {
    LinuxMountingManager mountingManager;

    mountChildAndTakeFrame(mountingManager, animatedChild(propsWithBackground(blue())));
    mountingManager.synchronouslyUpdateViewOnUIThread(kAnimatedTag, animatedProp("opacity", kHalfOpacity));

    EXPECT_TRUE(mountingManager.hasPendingDamage());

    const SceneFrame frame = mountingManager.takeFrame();

    ASSERT_FALSE(frame.damage.empty());
    EXPECT_EQ(damageBounds(frame.damage), animatedFrame());
    EXPECT_FALSE(mountingManager.hasPendingDamage());
}

TEST(AnimatedPropsTest, APackedBackgroundColourBecomesTheColourTheNodePaints) {
    const SceneSnapshot snapshot = snapshotAfterAnimating(
        propsWithBackground(blue()), animatedProp("backgroundColor", static_cast<int32_t>(kRedArgb)));

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot.at(0).backgroundColorArgb, kRedArgb);
}

TEST(AnimatedPropsTest, AFullyTransparentBackgroundColourLeavesTheNodePaintingNothing) {
    const SceneSnapshot snapshot =
        snapshotAfterAnimating(propsWithBackground(blue()), animatedProp("backgroundColor", 0));

    EXPECT_TRUE(snapshot.empty());
}

TEST(AnimatedPropsTest, ATransformArrayResolvesToTheMatrixACommitWouldHaveProduced) {
    const SceneMatrix animated =
        animatedMatrixMatchingTheCommitPath(translateThenRotate(), propsWithBackground(blue()));

    EXPECT_NE(animated.scaleX, 1.0F);
    EXPECT_NE(animated.translateX, 0.0F);
}

TEST(AnimatedPropsTest, APercentageTranslationResolvesAgainstTheNodesOwnFrame) {
    const SceneMatrix animated =
        animatedMatrixMatchingTheCommitPath(percentTranslation(), propsWithBackground(blue()));

    EXPECT_FLOAT_EQ(animated.translateX, kResolvedPercentTranslation);
}

TEST(AnimatedPropsTest, ATransformResolvesAboutTheOriginTheNodeMountedWith) {
    const std::shared_ptr<ViewProps> originProps = propsWithBackground(blue());

    originProps->transformOrigin = topLeftOrigin();

    const SceneMatrix animated = animatedMatrixMatchingTheCommitPath(translateThenRotate(), originProps);
    const SceneMatrix centred =
        matrixAfterAnimating(propsWithBackground(blue()), packedTransform(translateThenRotate()));

    EXPECT_NE(animated.translateX, centred.translateX);
}

TEST(AnimatedPropsTest, AnUpdateForAnUnknownTagIsADiagnosticAndDamagesNothing) {
    LinuxMountingManager mountingManager;

    mountChildAndTakeFrame(mountingManager, animatedChild(propsWithBackground(blue())));
    mountingManager.synchronouslyUpdateViewOnUIThread(kUnknownTag, animatedProp("opacity", kHalfOpacity));

    const MountDiagnostics diagnostics = mountingManager.mountDiagnostics();

    EXPECT_EQ(diagnostics.unknownTagOperations, 1U);
    EXPECT_EQ(diagnostics.firstUnknownOperation, "SyncUpdate");
    EXPECT_EQ(diagnostics.firstUnknownTag, kUnknownTag);
    EXPECT_FALSE(mountingManager.hasPendingDamage());
    EXPECT_TRUE(mountingManager.takeFrame().damage.empty());
}

// Issue #74, and react-native-windows#16309: "permanent UI thread hang: unbounded ProcessDelayedPropsNodes retry
// loop when a native-driver animation targets a view absent from the Fabric registry". The animated node graph
// and the mounting layer have independent lifetimes, so a driver can name a tag the scene does not hold — before
// the mount, after the unmount, or across a re-mount — and the thread that answers is the one that draws.
//
// There is no delayed-props queue here and therefore no retry: `synchronouslyUpdateViewOnUIThread` asks the scene
// once, counts a diagnostic if the tag is unknown, and returns. These three tests are that answer written down,
// because "it cannot happen here" is only true while nobody adds a queue.

constexpr size_t kAnimationFrameCount = 120;
constexpr size_t kConcurrentMountCycles = 200;

void unmountAnimatedChild(LinuxMountingManager& mountingManager) {
    const ShadowView child = animatedChild(propsWithBackground(blue()));
    ShadowViewMutationList mutations{ShadowViewMutation::RemoveMutation(kSurfaceTag, child, 0),
                                     ShadowViewMutation::DeleteMutation(child)};

    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));
    mountingManager.takeFrame();
}

TEST(AnimatedPropsLifetimeTest, AnAnimationThatOutlivesItsViewCostsOneDiagnosticPerFrameAndNothingElse) {
    LinuxMountingManager mountingManager;

    mountChildAndTakeFrame(mountingManager, animatedChild(propsWithBackground(blue())));
    unmountAnimatedChild(mountingManager);

    // The driver does not know the view is gone: it keeps producing frames, as it would for the whole duration of
    // whatever animation was running.
    for (size_t frame = 0; frame < kAnimationFrameCount; frame++) {
        mountingManager.synchronouslyUpdateViewOnUIThread(kAnimatedTag, animatedProp("opacity", kHalfOpacity));
    }

    const MountDiagnostics diagnostics = mountingManager.mountDiagnostics();

    // One count per frame and not one more: bounded work, no queue, nothing retried. The first one is kept whole,
    // which is what a report of this needs and what the log line already said.
    EXPECT_EQ(diagnostics.unknownTagOperations, kAnimationFrameCount);
    EXPECT_EQ(diagnostics.firstUnknownOperation, "SyncUpdate");
    EXPECT_EQ(diagnostics.firstUnknownTag, kAnimatedTag);
    EXPECT_FALSE(mountingManager.hasPendingDamage());
    EXPECT_TRUE(mountingManager.takeFrame().damage.empty());
}

TEST(AnimatedPropsLifetimeTest, AnAnimationAttachedBeforeItsViewMountsAppliesFromTheMountOnwards) {
    LinuxMountingManager mountingManager;

    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});

    // Attached first, mounted second — the order react-native-windows#16309 is about.
    for (size_t frame = 0; frame < kAnimationFrameCount; frame++) {
        mountingManager.synchronouslyUpdateViewOnUIThread(kAnimatedTag, animatedProp("opacity", kHalfOpacity));
    }

    EXPECT_EQ(mountingManager.mountDiagnostics().unknownTagOperations, kAnimationFrameCount);

    ShadowViewMutationList mutations{
        ShadowViewMutation::CreateMutation(animatedChild(propsWithBackground(blue()))),
        ShadowViewMutation::InsertMutation(kSurfaceTag, animatedChild(propsWithBackground(blue())), 0)};

    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));
    mountingManager.takeFrame();
    mountingManager.synchronouslyUpdateViewOnUIThread(kAnimatedTag, animatedProp("opacity", kHalfOpacity));

    const SceneSnapshot snapshot = mountingManager.snapshotScene();

    // The tag is not poisoned by having been unknown: the update that arrives after the mount applies.
    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot[0].backgroundColorArgb, kHalfBlueArgb);
    EXPECT_EQ(mountingManager.mountDiagnostics().unknownTagOperations, kAnimationFrameCount);
}

TEST(AnimatedPropsLifetimeTest, ATagMountedAgainAfterItsUnmountAnimatesAsTheNewNode) {
    LinuxMountingManager mountingManager;

    mountChildAndTakeFrame(mountingManager, animatedChild(propsWithBackground(blue())));
    mountingManager.synchronouslyUpdateViewOnUIThread(kAnimatedTag, animatedProp("opacity", kHalfOpacity));
    unmountAnimatedChild(mountingManager);
    mountChildAndTakeFrame(mountingManager, animatedChild(propsWithBackground(red())));

    // The second node mounted red and is animated to half opacity: the answer is the *new* node's colour, so
    // nothing survived the unmount that should not have.
    mountingManager.synchronouslyUpdateViewOnUIThread(kAnimatedTag, animatedProp("opacity", kHalfOpacity));

    const SceneSnapshot snapshot = mountingManager.snapshotScene();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot[0].backgroundColorArgb, kHalfRedArgb);
}

// The TSan half of #74: the frame thread animating a tag while the JavaScript thread mounts and unmounts it, so
// the interleaving that would dereference a removed node is the one this runs. Both sides take `sceneMutex_`,
// which is the whole guarantee; ThreadSanitizer is what checks the claim rather than the assertions below, which
// only say the run finished and stayed consistent. Run under CTest and under the TSan preset.
TEST(AnimatedPropsLifetimeTest, AnimatingWhileTheTagIsMountedAndUnmountedOnAnotherThreadIsSerialized) {
    LinuxMountingManager mountingManager;
    std::latch startLatch{2};
    std::atomic<bool> isMounting{true};

    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});

    std::thread mountingThread([&] {
        startLatch.arrive_and_wait();

        for (size_t cycle = 0; cycle < kConcurrentMountCycles; cycle++) {
            ShadowViewMutationList mounting{
                ShadowViewMutation::CreateMutation(animatedChild(propsWithBackground(blue()))),
                ShadowViewMutation::InsertMutation(kSurfaceTag, animatedChild(propsWithBackground(blue())), 0)};

            mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mounting)));
            unmountAnimatedChild(mountingManager);
        }

        isMounting.store(false);
    });

    startLatch.arrive_and_wait();

    // The frame thread's two calls, in the order the window makes them, for as long as the other thread is
    // mounting: an update whose tag has just been deleted is the case, and it must cost a diagnostic rather than
    // a dereference.
    while (isMounting.load()) {
        mountingManager.synchronouslyUpdateViewOnUIThread(kAnimatedTag, animatedProp("opacity", kHalfOpacity));
        mountingManager.takeFrame();
    }

    mountingThread.join();

    // Whatever the interleaving was, the scene holds either the node or nothing, and never a dangling half of one.
    EXPECT_LE(mountingManager.snapshotScene().size(), 1U);
}

TEST(AnimatedPropsTest, ANonAllowlistedPropIsCountedAndTheRestOfThePayloadStillApplies) {
    LinuxMountingManager mountingManager;
    folly::dynamic props = animatedProp("opacity", kHalfOpacity);

    props["shadowRadius"] = 4.0;

    mountChildAndTakeFrame(mountingManager, animatedChild(propsWithBackground(blue())));
    mountingManager.synchronouslyUpdateViewOnUIThread(kAnimatedTag, props);

    const MountDiagnostics diagnostics = mountingManager.mountDiagnostics();

    EXPECT_EQ(diagnostics.rejectedAnimatedProps, 1U);
    EXPECT_EQ(diagnostics.firstRejectedAnimatedProp, "shadowRadius");
    EXPECT_EQ(diagnostics.unknownTagOperations, 0U);
    EXPECT_EQ(mountingManager.snapshotScene().at(0).backgroundColorArgb, kHalfBlueArgb);
}

TEST(AnimatedPropsTest, EveryRejectedPropIsCountedAndOnlyTheFirstIsKept) {
    LinuxMountingManager mountingManager;

    mountChildAndTakeFrame(mountingManager, animatedChild(propsWithBackground(blue())));
    mountingManager.synchronouslyUpdateViewOnUIThread(kAnimatedTag, animatedProp("shadowRadius", 4.0));
    mountingManager.synchronouslyUpdateViewOnUIThread(kAnimatedTag, animatedProp("borderTopColor", 0));

    const MountDiagnostics diagnostics = mountingManager.mountDiagnostics();

    EXPECT_EQ(diagnostics.rejectedAnimatedProps, 2U);
    EXPECT_EQ(diagnostics.firstRejectedAnimatedProp, "shadowRadius");
}

TEST(AnimatedPropsSceneTest, AnUnknownTagAndAPayloadThatIsNotAnObjectAreBothNoOps) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makePaintedView(kAnimatedTag, animatedFrame(), blue()));
    scene.takeDamage();

    EXPECT_TRUE(scene.applyAnimatedProps(kUnknownTag, animatedProp("opacity", kHalfOpacity)).empty());
    EXPECT_TRUE(scene.applyAnimatedProps(kAnimatedTag, folly::dynamic::array("opacity")).empty());
    EXPECT_TRUE(scene.takeDamage().empty());
}

} // namespace
