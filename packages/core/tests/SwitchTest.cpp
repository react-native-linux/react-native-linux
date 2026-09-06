#include "SceneTestSupport.h"

#include "RetainedScene.h"
#include "SwitchComponent.h"
#include "SwitchContent.h"

#include <gtest/gtest.h>

#include <react/renderer/components/FBReactNativeSpec/Props.h>
#include <react/renderer/graphics/Color.h>

#include <cstdint>
#include <memory>
#include <stdexcept>

// The `<Switch>` of issue #261, in the three layers that can be wrong arithmetically: where the two parts are
// drawn, how the thumb gets from one end to the other on the frame clock, and what the scene reads off the props
// React committed. The picture itself is `switch.png`; a press firing `onValueChange` once is the `switch-toggle`
// e2e scenario, because the emission needs a shadow tree and an event queue rather than a scene.

namespace {

using facebook::react::SwitchProps;
using react_native_linux::advanceSwitchThumbProgress;
using react_native_linux::kSwitchSize;
using react_native_linux::kSwitchThumbInset;
using react_native_linux::kSwitchToggleMilliseconds;
using react_native_linux::SceneSwitchContent;
using react_native_linux::SwitchGeometry;
using react_native_linux::switchGeometry;
using react_native_linux::switchTrackColorArgb;

constexpr double kSixtyHertzMilliseconds = 1000.0 / 60.0;
constexpr uint32_t kTrackOffArgb = 0xFF39393DU;
constexpr uint32_t kTrackOnArgb = 0xFF34C759U;
constexpr uint32_t kThumbArgb = 0xFFFFFFFFU;
const Rect kSwitchFrame = makeRect(40, 60, 51, 31);

ShadowView makeSwitch(Tag tag, Rect frame, bool value, bool disabled) {
    const std::shared_ptr<SwitchProps> switchProps = std::make_shared<SwitchProps>();

    switchProps->value = value;
    switchProps->disabled = disabled;

    ShadowView shadowView;

    shadowView.tag = tag;
    shadowView.componentName = react_native_linux::kSwitchComponentName;
    shadowView.layoutMetrics.frame = frame;
    shadowView.props = switchProps;

    return shadowView;
}

ShadowView makeColouredSwitch(Tag tag, Rect frame, bool value, SharedColor trackOff, SharedColor trackOn,
                              SharedColor thumb) {
    ShadowView shadowView = makeSwitch(tag, frame, value, false);
    const std::shared_ptr<SwitchProps> switchProps = std::make_shared<SwitchProps>();

    switchProps->value = value;
    switchProps->tintColor = trackOff;
    switchProps->onTintColor = trackOn;
    switchProps->thumbTintColor = thumb;
    shadowView.props = switchProps;

    return shadowView;
}

RetainedScene sceneWithSwitch(bool value, bool disabled) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 400, .height = 300});
    addChild(scene, kSurfaceTag, makeSwitch(2, kSwitchFrame, value, disabled));
    scene.takeDamage();

    return scene;
}

const SceneSwitchContent& switchOf(const SceneSnapshot& snapshot, Tag tag) {
    for (const ScenePrimitive& primitive : snapshot) {
        if (primitive.tag == tag) {
            return primitive.switchControl.value();
        }
    }

    throw std::runtime_error("no switch primitive with that tag");
}

TEST(SwitchGeometryTest, TheTrackIsThePillTheFrameDescribesAndTheThumbSitsOneInsetInside) {
    const SwitchGeometry geometry = switchGeometry(kSwitchFrame, 0.0F);

    EXPECT_FLOAT_EQ(geometry.track.bounds.size.width, kSwitchFrame.size.width);
    EXPECT_FLOAT_EQ(geometry.track.radii.topLeft.horizontal, kSwitchFrame.size.height / 2);
    EXPECT_FLOAT_EQ(geometry.track.radii.bottomRight.vertical, kSwitchFrame.size.height / 2);
    EXPECT_FLOAT_EQ(geometry.thumbRadius, (kSwitchFrame.size.height / 2) - kSwitchThumbInset);
    EXPECT_FLOAT_EQ(geometry.thumbCenter.y, kSwitchFrame.origin.y + (kSwitchFrame.size.height / 2));
    EXPECT_FLOAT_EQ(geometry.thumbCenter.x, kSwitchFrame.origin.x + kSwitchThumbInset + geometry.thumbRadius);
}

TEST(SwitchGeometryTest, TheThumbTravelsToTheOtherEndAndIsHalfwayAtHalfProgress) {
    const SwitchGeometry off = switchGeometry(kSwitchFrame, 0.0F);
    const SwitchGeometry half = switchGeometry(kSwitchFrame, 0.5F);
    const SwitchGeometry on = switchGeometry(kSwitchFrame, 1.0F);

    EXPECT_FLOAT_EQ(on.thumbCenter.x,
                    kSwitchFrame.origin.x + kSwitchFrame.size.width - kSwitchThumbInset - on.thumbRadius);
    EXPECT_FLOAT_EQ(half.thumbCenter.x, (off.thumbCenter.x + on.thumbCenter.x) / 2);
    EXPECT_FLOAT_EQ(half.thumbCenter.y, off.thumbCenter.y);
}

// A progress outside the range cannot arrive from the advance, and a snapshot that carried one would put the
// thumb outside its own track — which is a switch drawn outside the rectangle its node damages.
TEST(SwitchGeometryTest, AProgressOutsideTheRangeIsClampedToTheEndsOfTheTrack) {
    EXPECT_FLOAT_EQ(switchGeometry(kSwitchFrame, -3.0F).thumbCenter.x,
                    switchGeometry(kSwitchFrame, 0.0F).thumbCenter.x);
    EXPECT_FLOAT_EQ(switchGeometry(kSwitchFrame, 4.0F).thumbCenter.x, switchGeometry(kSwitchFrame, 1.0F).thumbCenter.x);
}

// `scaleY: 0.01` on a switch, or a style that gave it no room: the thumb collapses rather than inverting, and
// the travel collapses with it instead of running backwards.
TEST(SwitchGeometryTest, AFrameTooSmallToHoldAnInsetThumbCollapsesRatherThanInverting) {
    const SwitchGeometry flat = switchGeometry(makeRect(0, 0, 51, 2), 1.0F);
    const SwitchGeometry narrow = switchGeometry(makeRect(0, 0, 6, 31), 1.0F);

    EXPECT_FLOAT_EQ(flat.thumbRadius, 0.0F);
    EXPECT_GE(narrow.thumbCenter.x, kSwitchThumbInset);
    EXPECT_FLOAT_EQ(narrow.thumbCenter.x, switchGeometry(makeRect(0, 0, 6, 31), 0.0F).thumbCenter.x);
}

// A switch narrower than it is tall — a `width` an app set, or a `scaleX` mid-animation — sizes its thumb off
// the shorter side, so the circle stays inside the pill instead of spilling out of both ends of the box the node
// damages.
TEST(SwitchGeometryTest, ANarrowFrameSizesTheThumbOffItsWidthRatherThanItsHeight) {
    const SwitchGeometry narrow = switchGeometry(makeRect(0, 0, 12, 31), 1.0F);

    EXPECT_FLOAT_EQ(narrow.thumbRadius, (12.0F / 2) - kSwitchThumbInset);
    EXPECT_GE(narrow.thumbCenter.x - narrow.thumbRadius, kSwitchThumbInset);
    EXPECT_LE(narrow.thumbCenter.x + narrow.thumbRadius, 12.0F - kSwitchThumbInset);
}

TEST(SwitchThumbProgressTest, TheThumbCrossesTheTrackInTheToggleDurationWhicheverWayItIsGoing) {
    const int frameCount = static_cast<int>(kSwitchToggleMilliseconds / kSixtyHertzMilliseconds);
    float progress = 0.0F;

    for (int frame = 0; frame < frameCount - 1; frame++) {
        progress = advanceSwitchThumbProgress(progress, true, kSixtyHertzMilliseconds);
        EXPECT_LT(progress, 1.0F);
    }

    for (int frame = 0; frame < 2; frame++) {
        progress = advanceSwitchThumbProgress(progress, true, kSixtyHertzMilliseconds);
    }

    EXPECT_FLOAT_EQ(progress, 1.0F);

    for (int frame = 0; frame < frameCount - 1; frame++) {
        progress = advanceSwitchThumbProgress(progress, false, kSixtyHertzMilliseconds);
        EXPECT_GT(progress, 0.0F);
    }

    for (int frame = 0; frame < 2; frame++) {
        progress = advanceSwitchThumbProgress(progress, false, kSixtyHertzMilliseconds);
    }

    EXPECT_FLOAT_EQ(progress, 0.0F);
}

// The schedule is elapsed time, not a step per frame: two 120 Hz frames move the thumb exactly as far as one
// 60 Hz frame, which is the rule `animatedImageFrameIndex` already sets for a GIF.
TEST(SwitchThumbProgressTest, TheTravelIsWallClockRatherThanACountOfFrames) {
    const float atSixtyHertz = advanceSwitchThumbProgress(0.0F, true, kSixtyHertzMilliseconds);
    const float atOneHundredAndTwentyHertz = advanceSwitchThumbProgress(
        advanceSwitchThumbProgress(0.0F, true, kSixtyHertzMilliseconds / 2), true, kSixtyHertzMilliseconds / 2);

    EXPECT_FLOAT_EQ(atSixtyHertz, atOneHundredAndTwentyHertz);
}

TEST(SwitchThumbProgressTest, AThumbAlreadyAtTheEndItIsHeadingForDoesNotMove) {
    EXPECT_FLOAT_EQ(advanceSwitchThumbProgress(1.0F, true, kSixtyHertzMilliseconds), 1.0F);
    EXPECT_FLOAT_EQ(advanceSwitchThumbProgress(0.0F, false, kSixtyHertzMilliseconds), 0.0F);
}

TEST(SwitchTrackColorTest, TheTrackIsMixedInTheProportionTheThumbHasTravelled) {
    const SceneSwitchContent content{.trackOffColorArgb = 0xFF000000U, .trackOnColorArgb = 0xFFFFFFFFU};

    EXPECT_EQ(switchTrackColorArgb(content), 0xFF000000U);
    EXPECT_EQ(switchTrackColorArgb(SceneSwitchContent{.thumbProgress = 1.0F,
                                                      .trackOffColorArgb = content.trackOffColorArgb,
                                                      .trackOnColorArgb = content.trackOnColorArgb}),
              0xFFFFFFFFU);
    EXPECT_EQ(switchTrackColorArgb(SceneSwitchContent{.thumbProgress = 0.5F,
                                                      .trackOffColorArgb = content.trackOffColorArgb,
                                                      .trackOnColorArgb = content.trackOnColorArgb}),
              0xFF808080U);
}

TEST(SwitchTrackColorTest, AProgressOutsideTheRangeMixesNoFurtherThanTheTwoColoursItWasGiven) {
    const SceneSwitchContent overshot{
        .thumbProgress = 2.0F, .trackOffColorArgb = 0xFF000000U, .trackOnColorArgb = 0xFF102030U};

    EXPECT_EQ(switchTrackColorArgb(overshot), 0xFF102030U);
}

TEST(SwitchMeasurementTest, ASwitchMeasuresAtTheControlsOwnSizeSoYogaDoesNotCollapseIt) {
    // react-native-macos#1699: a Fabric `<Switch>` that measures at nothing renders as nothing, because
    // `Switch.js` gives it `alignSelf: flex-start` and no width of its own.
    EXPECT_GT(kSwitchSize.width, 0);
    EXPECT_GT(kSwitchSize.height, 0);
    EXPECT_FLOAT_EQ(kSwitchSize.width, 51.0F);
    EXPECT_FLOAT_EQ(kSwitchSize.height, 31.0F);
}

TEST(SwitchSceneTest, ASwitchMountsAtTheEndItsValueNamesRatherThanSlidingThere) {
    EXPECT_FLOAT_EQ(switchOf(sceneWithSwitch(false, false).snapshot(), 2).thumbProgress, 0.0F);
    EXPECT_FLOAT_EQ(switchOf(sceneWithSwitch(true, false).snapshot(), 2).thumbProgress, 1.0F);
}

TEST(SwitchSceneTest, TheSceneCarriesTheAuthoredColoursAndFallsBackToThePlatformsOwn) {
    RetainedScene scene = sceneWithSwitch(false, false);
    const SceneSwitchContent defaulted = switchOf(scene.snapshot(), 2);

    EXPECT_EQ(defaulted.trackOffColorArgb, kTrackOffArgb);
    EXPECT_EQ(defaulted.trackOnColorArgb, kTrackOnArgb);
    EXPECT_EQ(defaulted.thumbColorArgb, kThumbArgb);

    scene.updateNode(makeColouredSwitch(2, kSwitchFrame, false, red(), blue(), red()));

    const SceneSwitchContent authored = switchOf(scene.snapshot(), 2);

    EXPECT_EQ(authored.trackOffColorArgb, kRedArgb);
    EXPECT_EQ(authored.trackOnColorArgb, kBlueArgb);
    EXPECT_EQ(authored.thumbColorArgb, kRedArgb);
}

// A disabled control is dimmed rather than recoloured, and the inherited opacity multiplies that dimming instead
// of replacing it.
TEST(SwitchSceneTest, ADisabledSwitchIsDrawnAtHalfStrengthUnderTheInheritedOpacity) {
    const SceneSwitchContent enabled = switchOf(sceneWithSwitch(false, false).snapshot(), 2);
    const SceneSwitchContent disabled = switchOf(sceneWithSwitch(false, true).snapshot(), 2);

    EXPECT_TRUE(disabled.isDisabled);
    EXPECT_EQ(disabled.thumbColorArgb >> 24U, 0x80U);
    EXPECT_EQ(enabled.thumbColorArgb >> 24U, 0xFFU);
    EXPECT_EQ(disabled.trackOffColorArgb & 0xFFFFFFU, enabled.trackOffColorArgb & 0xFFFFFFU);
}

TEST(SwitchSceneTest, AnUpdateThatFlipsTheValueLeavesTheThumbWhereItIsSoTheFrameClockMovesIt) {
    RetainedScene scene = sceneWithSwitch(false, false);

    scene.updateNode(makeSwitch(2, kSwitchFrame, true, false));
    scene.takeDamage();

    const SceneSwitchContent committed = switchOf(scene.snapshot(), 2);

    EXPECT_TRUE(committed.isOn);
    EXPECT_FLOAT_EQ(committed.thumbProgress, 0.0F);

    EXPECT_TRUE(scene.advanceControlAnimations(kSixtyHertzMilliseconds));
    EXPECT_GT(switchOf(scene.snapshot(), 2).thumbProgress, 0.0F);
    EXPECT_LT(switchOf(scene.snapshot(), 2).thumbProgress, 1.0F);
}

TEST(SwitchSceneTest, AThumbThatMovedDamagesTheSwitchesOwnBoxAndNothingElse) {
    RetainedScene scene = sceneWithSwitch(false, false);

    scene.updateNode(makeSwitch(2, kSwitchFrame, true, false));
    scene.takeDamage();

    EXPECT_TRUE(scene.advanceControlAnimations(kSixtyHertzMilliseconds));

    const SceneDamage damage = scene.takeDamage();

    ASSERT_EQ(damage.size(), 1U);
    EXPECT_FLOAT_EQ(damage.front().origin.x, kSwitchFrame.origin.x);
    EXPECT_FLOAT_EQ(damage.front().size.width, kSwitchFrame.size.width);
}

TEST(SwitchSceneTest, ASwitchThatIsAlreadyWhereItsValueNamesAsksForNoFrameAndDamagesNothing) {
    RetainedScene scene = sceneWithSwitch(true, false);

    EXPECT_FALSE(scene.advanceControlAnimations(kSixtyHertzMilliseconds));
    EXPECT_TRUE(scene.takeDamage().empty());
}

RetainedScene sceneWithClippedSwitch() {
    RetainedScene scene;
    const std::shared_ptr<ViewProps> clipping = std::make_shared<ViewProps>();

    clipping->yogaStyle.setOverflow(facebook::yoga::Overflow::Hidden);
    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 400, .height = 300});
    addChild(scene, kSurfaceTag, makeStyledView(2, makeRect(0, 0, 50, 50), clipping));
    addChild(scene, 2, makeSwitch(3, makeRect(200, 0, 51, 31), false, false));

    return scene;
}

// A press on a switch inside a list that has scrolled away still commits, and the thumb still has to arrive:
// what it must not do is damage a rectangle nothing paints.
TEST(SwitchSceneTest, AClippedAwaySwitchKeepsTravellingAndDamagesNothing) {
    RetainedScene scene = sceneWithClippedSwitch();

    scene.updateNode(makeSwitch(3, makeRect(200, 0, 51, 31), true, false));
    scene.takeDamage();

    EXPECT_FALSE(scene.advanceControlAnimations(kSixtyHertzMilliseconds));
    EXPECT_TRUE(scene.takeDamage().empty());
    EXPECT_GT(switchOf(scene.snapshot(), 3).thumbProgress, 0.0F);
}

// The advance walks past everything that is not a control, so a scene of plain views is a scene it never
// damages.
TEST(SwitchSceneTest, ANodeThatIsNotAControlIsWalkedPastWithoutASwitchContent) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 400, .height = 300});
    addChild(scene, kSurfaceTag, makePaintedView(2, kSwitchFrame, blue()));
    scene.takeDamage();

    for (const ScenePrimitive& primitive : scene.snapshot()) {
        EXPECT_FALSE(primitive.switchControl.has_value());
    }

    EXPECT_FALSE(scene.advanceControlAnimations(kSixtyHertzMilliseconds));
}

// The mounting manager is what the frame clock actually asks, and what it has to learn from an advance is that
// there is something to paint: a toggle that moved and did not raise the flag is a thumb that jumps on the next
// frame something else happens to damage.
TEST(SwitchSceneTest, AControlAnimationFrameFlagsPendingDamageAndAStillOneDoesNot) {
    LinuxMountingManager mountingManager;
    const ShadowView off = makeSwitch(2, kSwitchFrame, false, false);
    const ShadowView on = makeSwitch(2, kSwitchFrame, true, false);
    ShadowViewMutationList update;

    mountChildAndTakeFrame(mountingManager, off);
    update.push_back(ShadowViewMutation::UpdateMutation(off, on, kSurfaceTag));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(update)));

    // The mount's own damage is still pending, so this is the advance whose flag is already raised.
    EXPECT_TRUE(mountingManager.advanceControlAnimations(kSixtyHertzMilliseconds));
    EXPECT_TRUE(mountingManager.hasPendingDamage());

    mountingManager.takeFrame();

    EXPECT_TRUE(mountingManager.advanceControlAnimations(kSixtyHertzMilliseconds));
    EXPECT_TRUE(mountingManager.hasPendingDamage());

    mountingManager.takeFrame();

    // Past the end of the travel in one step, so the next frame has nothing left to move.
    EXPECT_TRUE(mountingManager.advanceControlAnimations(kSwitchToggleMilliseconds));
    mountingManager.takeFrame();

    EXPECT_FALSE(mountingManager.advanceControlAnimations(kSixtyHertzMilliseconds));
    EXPECT_FALSE(mountingManager.hasPendingDamage());
}

// A `<Switch>` paints even though it has no background colour, no border and no text; without this it would be
// dropped from the snapshot exactly as an empty `<View>` is.
TEST(SwitchSceneTest, ASwitchWithNoOtherPaintedPropIsStillInTheSnapshot) {
    const SceneSnapshot snapshot = sceneWithSwitch(false, false).snapshot();
    bool hasSwitch = false;

    for (const ScenePrimitive& primitive : snapshot) {
        hasSwitch = hasSwitch || primitive.switchControl.has_value();
    }

    EXPECT_TRUE(hasSwitch);
}

} // namespace
