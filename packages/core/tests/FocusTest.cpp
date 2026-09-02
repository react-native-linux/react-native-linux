#include "FocusModel.h"

#include <gtest/gtest.h>

#include <react/renderer/core/ReactPrimitives.h>

#include <optional>
#include <string>
#include <vector>

namespace {

using facebook::react::Tag;
using react_native_linux::FocusDirection;
using react_native_linux::FocusModel;
using react_native_linux::FocusOrigin;
using react_native_linux::FocusTransition;
using react_native_linux::focusDirectionForKey;
using react_native_linux::isActivationKey;
using react_native_linux::isTextInputComponent;

constexpr Tag kAlpha = 10;
constexpr Tag kBeta = 11;
constexpr Tag kGamma = 12;
constexpr Tag kUnmounted = 99;
constexpr Tag kNoTag = 0;

FocusModel modelWithThreeFocusables() {
    FocusModel model;

    model.setFocusableTags({kAlpha, kBeta, kGamma});

    return model;
}

void expectTransition(const FocusTransition& transition, Tag blurredTag, Tag focusedTag) {
    EXPECT_EQ(transition.blurredTag, blurredTag);
    EXPECT_EQ(transition.focusedTag, focusedTag);
    EXPECT_TRUE(transition.hasChanged);
}

void expectNoTransition(const FocusTransition& transition) {
    EXPECT_EQ(transition.blurredTag, kNoTag);
    EXPECT_EQ(transition.focusedTag, kNoTag);
    EXPECT_FALSE(transition.hasChanged);
}

TEST(FocusModelTest, TabTraversesInMountOrderAndWrapsAtTheEnd) {
    FocusModel model = modelWithThreeFocusables();

    expectTransition(model.move(FocusDirection::Forward), kNoTag, kAlpha);
    expectTransition(model.move(FocusDirection::Forward), kAlpha, kBeta);
    expectTransition(model.move(FocusDirection::Forward), kBeta, kGamma);
    expectTransition(model.move(FocusDirection::Forward), kGamma, kAlpha);
    EXPECT_EQ(model.focusedTag(), kAlpha);
}

TEST(FocusModelTest, ShiftTabTraversesBackwardsAndWrapsAtTheStart) {
    FocusModel model = modelWithThreeFocusables();

    expectTransition(model.move(FocusDirection::Backward), kNoTag, kGamma);
    expectTransition(model.move(FocusDirection::Backward), kGamma, kBeta);
    expectTransition(model.move(FocusDirection::Backward), kBeta, kAlpha);
    expectTransition(model.move(FocusDirection::Backward), kAlpha, kGamma);
}

TEST(FocusModelTest, TraversalWithoutAnyFocusableChangesNothing) {
    FocusModel model;

    expectNoTransition(model.move(FocusDirection::Forward));
    expectNoTransition(model.move(FocusDirection::Backward));
    EXPECT_EQ(model.focusedTag(), kNoTag);
    EXPECT_FALSE(model.isFocusVisible());
}

TEST(FocusModelTest, TabOnASingleFocusableStaysOnItAndEmitsNothingTwice) {
    FocusModel model;

    model.setFocusableTags({kAlpha});

    expectTransition(model.move(FocusDirection::Forward), kNoTag, kAlpha);
    expectNoTransition(model.move(FocusDirection::Forward));
    EXPECT_EQ(model.focusedTag(), kAlpha);
}

TEST(FocusModelTest, AClickOnAFocusableMovesFocusWithoutShowingTheRing) {
    FocusModel model = modelWithThreeFocusables();

    expectTransition(model.focusTag(kBeta, FocusOrigin::Pointer), kNoTag, kBeta);
    EXPECT_EQ(model.focusedTag(), kBeta);
    EXPECT_FALSE(model.isFocusVisible());
}

TEST(FocusModelTest, AKeyboardFocusShowsTheRingAndAPointerFocusHidesItAgain) {
    FocusModel model = modelWithThreeFocusables();

    model.move(FocusDirection::Forward);

    EXPECT_TRUE(model.isFocusVisible());

    expectNoTransition(model.setFocusableTags({kAlpha, kBeta, kGamma}));

    const FocusTransition clicked = model.focusTag(kAlpha, FocusOrigin::Pointer);

    EXPECT_EQ(clicked.blurredTag, kNoTag);
    EXPECT_EQ(clicked.focusedTag, kNoTag);
    EXPECT_TRUE(clicked.hasChanged);
    EXPECT_EQ(model.focusedTag(), kAlpha);
    EXPECT_FALSE(model.isFocusVisible());
}

TEST(FocusModelTest, TabAfterAClickOnTheSameNodeOnlyTurnsTheRingOn) {
    FocusModel model;

    model.setFocusableTags({kAlpha});
    model.focusTag(kAlpha, FocusOrigin::Pointer);

    const FocusTransition tabbed = model.move(FocusDirection::Forward);

    EXPECT_EQ(tabbed.blurredTag, kNoTag);
    EXPECT_EQ(tabbed.focusedTag, kNoTag);
    EXPECT_TRUE(tabbed.hasChanged);
    EXPECT_TRUE(model.isFocusVisible());
}

TEST(FocusModelTest, AClickOnAnythingUnfocusableBlursTheFocusedNode) {
    FocusModel model = modelWithThreeFocusables();

    model.move(FocusDirection::Forward);

    expectTransition(model.focusTag(kNoTag, FocusOrigin::Pointer), kAlpha, kNoTag);
    EXPECT_EQ(model.focusedTag(), kNoTag);
    EXPECT_FALSE(model.isFocusVisible());
}

TEST(FocusModelTest, AClickOnTheBackgroundWithNothingFocusedChangesNothing) {
    FocusModel model = modelWithThreeFocusables();

    expectNoTransition(model.focusTag(kNoTag, FocusOrigin::Pointer));
}

TEST(FocusModelTest, AClickOnANodeThatIsNotFocusableBlursRatherThanMoves) {
    FocusModel model = modelWithThreeFocusables();

    model.focusTag(kBeta, FocusOrigin::Keyboard);

    expectTransition(model.focusTag(kUnmounted, FocusOrigin::Pointer), kBeta, kNoTag);
}

TEST(FocusModelTest, UnmountingTheFocusedNodeBlursIt) {
    FocusModel model = modelWithThreeFocusables();

    model.move(FocusDirection::Forward);

    expectTransition(model.setFocusableTags({kBeta, kGamma}), kAlpha, kNoTag);
    EXPECT_EQ(model.focusedTag(), kNoTag);
    EXPECT_FALSE(model.isFocusVisible());
}

TEST(FocusModelTest, ACommitThatKeepsTheFocusedNodeKeepsTheFocus) {
    FocusModel model = modelWithThreeFocusables();

    model.move(FocusDirection::Forward);

    expectNoTransition(model.setFocusableTags({kAlpha, kBeta}));
    EXPECT_EQ(model.focusedTag(), kAlpha);
    EXPECT_TRUE(model.isFocusVisible());
}

TEST(FocusModelTest, ACommitWithNothingFocusedChangesNothing) {
    FocusModel model;

    expectNoTransition(model.setFocusableTags({kAlpha}));
}

TEST(FocusModelTest, FocusSurvivesReorderingIntoANewTraversalPosition) {
    FocusModel model = modelWithThreeFocusables();

    model.focusTag(kGamma, FocusOrigin::Keyboard);

    expectNoTransition(model.setFocusableTags({kGamma, kAlpha, kBeta}));
    expectTransition(model.move(FocusDirection::Forward), kGamma, kAlpha);
}

TEST(FocusKeyTest, TabAndShiftTabAreTheOnlyTraversalKeys) {
    const std::optional<FocusDirection> forward = focusDirectionForKey("Tab", false);
    const std::optional<FocusDirection> backward = focusDirectionForKey("Tab", true);

    ASSERT_TRUE(forward.has_value());
    EXPECT_TRUE(forward.value() == FocusDirection::Forward);
    ASSERT_TRUE(backward.has_value());
    EXPECT_TRUE(backward.value() == FocusDirection::Backward);
    EXPECT_FALSE(focusDirectionForKey("Enter", false).has_value());
    EXPECT_FALSE(focusDirectionForKey("ArrowRight", false).has_value());
}

TEST(FocusKeyTest, EnterAndSpaceActivateAndNothingElseDoes) {
    EXPECT_TRUE(isActivationKey("Enter"));
    EXPECT_TRUE(isActivationKey(" "));
    EXPECT_FALSE(isActivationKey("Tab"));
    EXPECT_FALSE(isActivationKey("a"));
}

TEST(FocusKeyTest, OnlyATextInputComponentAsksForTheCompositorsTextInput) {
    EXPECT_TRUE(isTextInputComponent("TextInput"));
    EXPECT_FALSE(isTextInputComponent("View"));
    EXPECT_FALSE(isTextInputComponent("Paragraph"));
}

} // namespace
