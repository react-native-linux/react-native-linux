#include "LinuxMountingManager.h"
#include "RetainedScene.h"
#include "SceneTestSupport.h"

#include <gtest/gtest.h>

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
