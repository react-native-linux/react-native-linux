#include "ActivityIndicatorContent.h"
#include "RetainedScene.h"
#include "SceneTestSupport.h"

#include <gtest/gtest.h>
#include <memory>
#include <stdexcept>

#include <react/renderer/components/FBReactNativeSpec/Props.h>
#include <react/renderer/graphics/Color.h>

// The `<ActivityIndicator>` of issue #261: where the arc is stroked, how far round it has turned, and what stops
// it turning. The picture is `activity-indicator.png`.

namespace {

using facebook::react::ActivityIndicatorViewProps;
using facebook::react::ActivityIndicatorViewSize;
using react_native_linux::ActivityIndicatorGeometry;
using react_native_linux::activityIndicatorGeometry;
using react_native_linux::activityIndicatorPhase;
using react_native_linux::isActivityIndicatorVisible;
using react_native_linux::kActivityIndicatorDefaultColorArgb;
using react_native_linux::kActivityIndicatorLargeStrokeWidth;
using react_native_linux::kActivityIndicatorRevolutionMilliseconds;
using react_native_linux::kActivityIndicatorSmallStrokeWidth;
using react_native_linux::kActivityIndicatorSweepDegrees;
using react_native_linux::SceneActivityIndicatorContent;

constexpr double kSixtyHertzMilliseconds = 1000.0 / 60.0;
const Rect kSmallFrame = makeRect(30, 40, 20, 20);

ShadowView makeActivityIndicator(Tag tag, Rect frame, bool animating, bool hidesWhenStopped,
                                 ActivityIndicatorViewSize size, SharedColor color) {
    const std::shared_ptr<ActivityIndicatorViewProps> indicatorProps = std::make_shared<ActivityIndicatorViewProps>();

    indicatorProps->animating = animating;
    indicatorProps->hidesWhenStopped = hidesWhenStopped;
    indicatorProps->size = size;
    indicatorProps->color = color;

    ShadowView shadowView;

    shadowView.tag = tag;
    shadowView.componentName = "ActivityIndicatorView";
    shadowView.layoutMetrics.frame = frame;
    shadowView.props = indicatorProps;

    return shadowView;
}

ShadowView makeSpinningIndicator(Tag tag, Rect frame) {
    return makeActivityIndicator(tag, frame, true, true, ActivityIndicatorViewSize::Small, SharedColor{});
}

RetainedScene sceneWithIndicator(const ShadowView& indicator) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 400, .height = 300});
    addChild(scene, kSurfaceTag, indicator);
    scene.takeDamage();

    return scene;
}

const SceneActivityIndicatorContent& indicatorOf(const SceneSnapshot& snapshot, Tag tag) {
    for (const ScenePrimitive& primitive : snapshot) {
        if (primitive.tag == tag) {
            return primitive.activityIndicator.value();
        }
    }

    throw std::runtime_error("no activity indicator primitive with that tag");
}

TEST(ActivityIndicatorPhaseTest, ThePhaseIsElapsedTimeThroughOneRevolutionAndWrapsAtTheEndOfIt) {
    EXPECT_FLOAT_EQ(activityIndicatorPhase(0.0), 0.0F);
    EXPECT_FLOAT_EQ(activityIndicatorPhase(kActivityIndicatorRevolutionMilliseconds / 4), 0.25F);
    EXPECT_FLOAT_EQ(activityIndicatorPhase(kActivityIndicatorRevolutionMilliseconds), 0.0F);
    EXPECT_FLOAT_EQ(activityIndicatorPhase(3.5 * kActivityIndicatorRevolutionMilliseconds), 0.5F);
}

// The same rule the GIF schedule follows: two 120 Hz frames turn the arc exactly as far as one 60 Hz frame.
TEST(ActivityIndicatorPhaseTest, TheAngleIsWallClockRatherThanACountOfFrames) {
    EXPECT_FLOAT_EQ(activityIndicatorPhase(kSixtyHertzMilliseconds),
                    activityIndicatorPhase(2 * (kSixtyHertzMilliseconds / 2)));
}

TEST(ActivityIndicatorGeometryTest, TheArcIsCentredInTheFrameAndInsetByHalfItsOwnStroke) {
    const ActivityIndicatorGeometry geometry = activityIndicatorGeometry(kSmallFrame, SceneActivityIndicatorContent{});

    EXPECT_FLOAT_EQ(geometry.strokeWidth, kActivityIndicatorSmallStrokeWidth);
    EXPECT_FLOAT_EQ(geometry.bounds.size.width, kSmallFrame.size.width - kActivityIndicatorSmallStrokeWidth);
    EXPECT_FLOAT_EQ(geometry.bounds.size.height, geometry.bounds.size.width);
    EXPECT_FLOAT_EQ(geometry.bounds.origin.x, kSmallFrame.origin.x + (kActivityIndicatorSmallStrokeWidth / 2));
    EXPECT_FLOAT_EQ(geometry.bounds.origin.y, kSmallFrame.origin.y + (kActivityIndicatorSmallStrokeWidth / 2));
    EXPECT_FLOAT_EQ(geometry.sweepAngleDegrees, kActivityIndicatorSweepDegrees);
}

// `size` picks the stroke, not the box: the box is the style `ActivityIndicator.js` already produced from the
// same prop, so a frame-proportional stroke would be the prop applied twice.
TEST(ActivityIndicatorGeometryTest, TheLargeSizeStrokesThickerThanTheSmallOneAtTheSameFrame) {
    const ActivityIndicatorGeometry small =
        activityIndicatorGeometry(kSmallFrame, SceneActivityIndicatorContent{.isLarge = false});
    const ActivityIndicatorGeometry large =
        activityIndicatorGeometry(kSmallFrame, SceneActivityIndicatorContent{.isLarge = true});

    EXPECT_FLOAT_EQ(small.strokeWidth, kActivityIndicatorSmallStrokeWidth);
    EXPECT_FLOAT_EQ(large.strokeWidth, kActivityIndicatorLargeStrokeWidth);
    EXPECT_LT(large.bounds.size.width, small.bounds.size.width);
}

TEST(ActivityIndicatorGeometryTest, ANonSquareFrameHoldsTheLargestCircleItCanAndCentresIt) {
    const ActivityIndicatorGeometry geometry =
        activityIndicatorGeometry(makeRect(0, 0, 60, 20), SceneActivityIndicatorContent{});

    EXPECT_FLOAT_EQ(geometry.bounds.size.width, 20 - kActivityIndicatorSmallStrokeWidth);
    EXPECT_FLOAT_EQ(geometry.bounds.origin.x, (60 - geometry.bounds.size.width) / 2);
    EXPECT_FLOAT_EQ(geometry.bounds.origin.y, (20 - geometry.bounds.size.height) / 2);
}

TEST(ActivityIndicatorGeometryTest, AFrameWithNoRoomForAStrokedCircleCollapsesRatherThanInverting) {
    EXPECT_FLOAT_EQ(activityIndicatorGeometry(makeRect(0, 0, 20, 1), SceneActivityIndicatorContent{}).bounds.size.width,
                    0.0F);
}

TEST(ActivityIndicatorGeometryTest, TheArcTurnsOnceThroughTheRevolution) {
    const ActivityIndicatorGeometry start =
        activityIndicatorGeometry(kSmallFrame, SceneActivityIndicatorContent{.elapsedMilliseconds = 0.0});
    const ActivityIndicatorGeometry quarter = activityIndicatorGeometry(
        kSmallFrame,
        SceneActivityIndicatorContent{.elapsedMilliseconds = kActivityIndicatorRevolutionMilliseconds / 4});

    EXPECT_FLOAT_EQ(start.startAngleDegrees, 0.0F);
    EXPECT_FLOAT_EQ(quarter.startAngleDegrees, 90.0F);
}

TEST(ActivityIndicatorVisibilityTest, AStoppedIndicatorIsHiddenOnlyWhenItWasAskedToHide) {
    EXPECT_TRUE(
        isActivityIndicatorVisible(SceneActivityIndicatorContent{.isAnimating = true, .hidesWhenStopped = true}));
    EXPECT_FALSE(
        isActivityIndicatorVisible(SceneActivityIndicatorContent{.isAnimating = false, .hidesWhenStopped = true}));
    EXPECT_TRUE(
        isActivityIndicatorVisible(SceneActivityIndicatorContent{.isAnimating = false, .hidesWhenStopped = false}));
}

TEST(ActivityIndicatorSceneTest, TheSceneCarriesThePropsAndFallsBackToUpstreamsOwnDefaultColour) {
    const SceneActivityIndicatorContent defaulted =
        indicatorOf(sceneWithIndicator(makeSpinningIndicator(2, kSmallFrame)).snapshot(), 2);

    EXPECT_TRUE(defaulted.isAnimating);
    EXPECT_TRUE(defaulted.hidesWhenStopped);
    EXPECT_FALSE(defaulted.isLarge);
    EXPECT_EQ(defaulted.colorArgb, kActivityIndicatorDefaultColorArgb);

    const SceneActivityIndicatorContent authored =
        indicatorOf(sceneWithIndicator(
                        makeActivityIndicator(2, kSmallFrame, false, false, ActivityIndicatorViewSize::Large, blue()))
                        .snapshot(),
                    2);

    EXPECT_FALSE(authored.isAnimating);
    EXPECT_FALSE(authored.hidesWhenStopped);
    EXPECT_TRUE(authored.isLarge);
    EXPECT_EQ(authored.colorArgb, kBlueArgb);
}

TEST(ActivityIndicatorSceneTest, ASpinningIndicatorAdvancesAndDamagesItsOwnBoxAndNothingElse) {
    RetainedScene scene = sceneWithIndicator(makeSpinningIndicator(2, kSmallFrame));

    EXPECT_TRUE(scene.advanceControlAnimations(kSixtyHertzMilliseconds));
    EXPECT_DOUBLE_EQ(indicatorOf(scene.snapshot(), 2).elapsedMilliseconds, kSixtyHertzMilliseconds);

    const SceneDamage damage = scene.takeDamage();

    ASSERT_EQ(damage.size(), 1U);
    EXPECT_FLOAT_EQ(damage.front().origin.x, kSmallFrame.origin.x);
    EXPECT_FLOAT_EQ(damage.front().size.width, kSmallFrame.size.width);
}

TEST(ActivityIndicatorSceneTest, AStoppedIndicatorAsksForNoFrameAndKeepsTheAngleItStoppedAt) {
    RetainedScene scene = sceneWithIndicator(makeSpinningIndicator(2, kSmallFrame));

    EXPECT_TRUE(scene.advanceControlAnimations(kSixtyHertzMilliseconds));
    scene.takeDamage();
    scene.updateNode(
        makeActivityIndicator(2, kSmallFrame, false, true, ActivityIndicatorViewSize::Small, SharedColor{}));
    scene.takeDamage();

    EXPECT_FALSE(scene.advanceControlAnimations(kSixtyHertzMilliseconds));
    EXPECT_TRUE(scene.takeDamage().empty());
    EXPECT_DOUBLE_EQ(indicatorOf(scene.snapshot(), 2).elapsedMilliseconds, kSixtyHertzMilliseconds);

    scene.updateNode(makeSpinningIndicator(2, kSmallFrame));
    scene.takeDamage();

    EXPECT_TRUE(scene.advanceControlAnimations(kSixtyHertzMilliseconds));
    EXPECT_DOUBLE_EQ(indicatorOf(scene.snapshot(), 2).elapsedMilliseconds, 2 * kSixtyHertzMilliseconds);
}

TEST(ActivityIndicatorSceneTest, AClippedAwayIndicatorAccumulatesNoTimeAndResumesWhereItPaused) {
    const std::shared_ptr<ViewProps> hiddenOverflow = std::make_shared<ViewProps>();
    RetainedScene scene;

    hiddenOverflow->yogaStyle.setOverflow(facebook::yoga::Overflow::Hidden);
    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 400, .height = 300});
    addChild(scene, kSurfaceTag, makeStyledView(2, makeRect(0, 0, 50, 50), hiddenOverflow));
    addChild(scene, 2, makeSpinningIndicator(3, makeRect(200, 0, 20, 20)));
    scene.takeDamage();

    EXPECT_FALSE(scene.advanceControlAnimations(kSixtyHertzMilliseconds));
    EXPECT_TRUE(scene.takeDamage().empty());
    EXPECT_DOUBLE_EQ(indicatorOf(scene.snapshot(), 3).elapsedMilliseconds, 0.0);
}

// The elapsed time survives an ordinary re-render, so a spinner inside a component that re-renders every frame
// turns at its own pace rather than at React's.
TEST(ActivityIndicatorSceneTest, AnUpdateThatChangedNothingElseKeepsTheArcWhereItIs) {
    RetainedScene scene = sceneWithIndicator(makeSpinningIndicator(2, kSmallFrame));

    EXPECT_TRUE(scene.advanceControlAnimations(kSixtyHertzMilliseconds));
    scene.updateNode(makeSpinningIndicator(2, kSmallFrame));

    EXPECT_DOUBLE_EQ(indicatorOf(scene.snapshot(), 2).elapsedMilliseconds, kSixtyHertzMilliseconds);
}

TEST(ActivityIndicatorSceneTest, TheInheritedOpacityIsFoldedIntoTheArcsAlpha) {
    RetainedScene scene;
    const std::shared_ptr<ViewProps> fadedProps = std::make_shared<ViewProps>();

    fadedProps->opacity = 0.5;
    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 400, .height = 300});
    addChild(scene, kSurfaceTag, makeStyledView(2, makeRect(0, 0, 200, 200), fadedProps));
    addChild(scene, 2, makeSpinningIndicator(3, kSmallFrame));

    EXPECT_EQ(kActivityIndicatorDefaultColorArgb >> 24U, 0xFFU);
    EXPECT_EQ(indicatorOf(scene.snapshot(), 3).colorArgb >> 24U, 0x80U);
}

} // namespace
