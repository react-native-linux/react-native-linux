#include "WindowControlSequence.h"

#include <gtest/gtest.h>
#include <string>

namespace {

using react_native_linux::describeWindowControlStep;
using react_native_linux::parseWindowControlSequence;
using react_native_linux::WindowControlSequence;
using react_native_linux::WindowControlStep;

WindowControlSequence parse(std::string_view specification) { return parseWindowControlSequence(specification); }

TEST(WindowControlSequenceTest, AnEmptySequenceIsRejected) {
    const WindowControlSequence parsed = parse("");

    EXPECT_EQ(parsed.error, "the window control sequence is empty");
    EXPECT_TRUE(parsed.steps.empty());
}

TEST(WindowControlSequenceTest, AnUnbracedSequenceIsRejected) {
    EXPECT_EQ(parse("800x600").error, "the window control sequence must be brace-delimited tokens");
}

TEST(WindowControlSequenceTest, AnUnterminatedTokenIsRejected) {
    EXPECT_EQ(parse("{800x600").error, "the window control sequence must be brace-delimited tokens");
}

TEST(WindowControlSequenceTest, AnUnknownTokenIsRejectedByName) {
    EXPECT_EQ(parse("{Shaded}").error, "unknown window control token {Shaded}");
}

TEST(WindowControlSequenceTest, ARejectedSequenceKeepsNoStepsFromBeforeTheFault) {
    const WindowControlSequence parsed = parse("{1024x768}{Shaded}");

    EXPECT_FALSE(parsed.error.empty());
    EXPECT_TRUE(parsed.steps.empty());
}

TEST(WindowControlSequenceTest, ARejectedSequenceKeepsNoStepsAfterAMalformedToken) {
    const WindowControlSequence parsed = parse("{1024x768}not-a-token");

    EXPECT_FALSE(parsed.error.empty());
    EXPECT_TRUE(parsed.steps.empty());
}

TEST(WindowControlSequenceTest, AnExtentTokenIsOneConfigureAtThatExtent) {
    const WindowControlSequence parsed = parse("{1024x768}");

    ASSERT_EQ(parsed.steps.size(), 1U);
    EXPECT_TRUE(parsed.error.empty());
    EXPECT_EQ(parsed.steps[0], (WindowControlStep{.width = 1024, .height = 768, .state = {}}));
}

TEST(WindowControlSequenceTest, AnExtentMissingItsSeparatorIsNotAnExtent) {
    EXPECT_EQ(parse("{1024}").error, "unknown window control token {1024}");
}

TEST(WindowControlSequenceTest, AnExtentWithAnUnreadableWidthIsRejected) { EXPECT_FALSE(parse("{axb}").error.empty()); }

TEST(WindowControlSequenceTest, AnExtentWithAnUnreadableHeightIsRejected) {
    EXPECT_FALSE(parse("{1024xb}").error.empty());
}

TEST(WindowControlSequenceTest, AnExtentWithTrailingTextIsRejected) {
    EXPECT_FALSE(parse("{1024x768px}").error.empty());
}

TEST(WindowControlSequenceTest, AZeroExtentIsRejected) { EXPECT_FALSE(parse("{1024x0}").error.empty()); }

TEST(WindowControlSequenceTest, AStateTokenBeforeAnyExtentLeavesTheExtentToTheWindow) {
    const WindowControlSequence parsed = parse("{Maximized}");

    ASSERT_EQ(parsed.steps.size(), 1U);
    EXPECT_EQ(parsed.steps[0].width, 0U);
    EXPECT_EQ(parsed.steps[0].height, 0U);
    EXPECT_TRUE(parsed.steps[0].state.maximized);
}

TEST(WindowControlSequenceTest, AStateTokenAfterAnExtentKeepsThatExtent) {
    const WindowControlSequence parsed = parse("{1024x768}{Maximized}");

    ASSERT_EQ(parsed.steps.size(), 2U);
    EXPECT_EQ(parsed.steps[1].width, 1024U);
    EXPECT_EQ(parsed.steps[1].height, 768U);
}

TEST(WindowControlSequenceTest, AStateTokenMayCarryTheExtentThatStateArrivesWith) {
    const WindowControlSequence parsed = parse("{Maximized:1280x800}");

    ASSERT_EQ(parsed.steps.size(), 1U);
    EXPECT_EQ(parsed.steps[0], (WindowControlStep{.width = 1280, .height = 800, .state = {.maximized = true}}));
}

TEST(WindowControlSequenceTest, AStateTokenWithAnUnreadableExtentIsRejected) {
    EXPECT_FALSE(parse("{Maximized:wide}").error.empty());
}

TEST(WindowControlSequenceTest, EveryStateNameSetsAndClearsItsOwnBit) {
    const WindowControlSequence parsed = parse("{Maximized}{Unmaximized}{Fullscreen}{Unfullscreen}{Tiled}{Untiled}");

    ASSERT_EQ(parsed.steps.size(), 6U);
    EXPECT_TRUE(parsed.steps[0].state.maximized);
    EXPECT_FALSE(parsed.steps[1].state.maximized);
    EXPECT_TRUE(parsed.steps[2].state.fullscreen);
    EXPECT_FALSE(parsed.steps[3].state.fullscreen);
    EXPECT_TRUE(parsed.steps[4].state.tiledLeft);
    EXPECT_TRUE(parsed.steps[4].state.tiledRight);
    EXPECT_TRUE(parsed.steps[4].state.tiledTop);
    EXPECT_TRUE(parsed.steps[4].state.tiledBottom);
    EXPECT_FALSE(parsed.steps[5].state.tiledLeft);
    EXPECT_FALSE(parsed.steps[5].state.tiledBottom);
}

TEST(WindowControlSequenceTest, StateAndExtentBothCarryForwardAcrossTokens) {
    const WindowControlSequence parsed = parse("{Fullscreen:1920x1080}{Maximized}");

    ASSERT_EQ(parsed.steps.size(), 2U);
    EXPECT_EQ(parsed.steps[1].width, 1920U);
    EXPECT_EQ(parsed.steps[1].height, 1080U);
    EXPECT_TRUE(parsed.steps[1].state.fullscreen);
    EXPECT_TRUE(parsed.steps[1].state.maximized);
}

TEST(WindowControlSequenceTest, ADragInterpolatesInclusivelyBetweenItsTwoExtents) {
    const WindowControlSequence parsed = parse("{Drag:800x600:1000x600:5}");

    ASSERT_EQ(parsed.steps.size(), 5U);
    EXPECT_EQ(parsed.steps[0].width, 800U);
    EXPECT_EQ(parsed.steps[1].width, 850U);
    EXPECT_EQ(parsed.steps[2].width, 900U);
    EXPECT_EQ(parsed.steps[3].width, 950U);
    EXPECT_EQ(parsed.steps[4].width, 1000U);
    EXPECT_EQ(parsed.steps[4].height, 600U);
}

TEST(WindowControlSequenceTest, ADragShrinksAsWellAsGrows) {
    const WindowControlSequence parsed = parse("{Drag:1000x800:800x600:3}");

    ASSERT_EQ(parsed.steps.size(), 3U);
    EXPECT_EQ(parsed.steps[1].width, 900U);
    EXPECT_EQ(parsed.steps[1].height, 700U);
    EXPECT_EQ(parsed.steps[2].width, 800U);
}

TEST(WindowControlSequenceTest, ADragIsResizingUntilItsLastConfigure) {
    const WindowControlSequence parsed = parse("{Drag:800x600:1000x600:3}");

    ASSERT_EQ(parsed.steps.size(), 3U);
    EXPECT_TRUE(parsed.steps[0].state.resizing);
    EXPECT_TRUE(parsed.steps[1].state.resizing);
    EXPECT_FALSE(parsed.steps[2].state.resizing);
}

TEST(WindowControlSequenceTest, ADragLeavesItsFinalExtentToTheTokensAfterIt) {
    const WindowControlSequence parsed = parse("{Drag:800x600:1000x600:2}{Maximized}");

    ASSERT_EQ(parsed.steps.size(), 3U);
    EXPECT_EQ(parsed.steps[2].width, 1000U);
    EXPECT_FALSE(parsed.steps[2].state.resizing);
}

TEST(WindowControlSequenceTest, ADragMissingItsSecondExtentIsRejected) {
    EXPECT_FALSE(parse("{Drag:800x600}").error.empty());
}

TEST(WindowControlSequenceTest, ADragMissingItsConfigureCountIsRejected) {
    EXPECT_FALSE(parse("{Drag:800x600:1000x600}").error.empty());
}

TEST(WindowControlSequenceTest, ADragWithAnUnreadableFirstExtentIsRejected) {
    EXPECT_FALSE(parse("{Drag:wide:1000x600:3}").error.empty());
}

TEST(WindowControlSequenceTest, ADragWithAnUnreadableSecondExtentIsRejected) {
    EXPECT_FALSE(parse("{Drag:800x600:wide:3}").error.empty());
}

TEST(WindowControlSequenceTest, ADragWithAnUnreadableCountIsRejected) {
    EXPECT_FALSE(parse("{Drag:800x600:1000x600:many}").error.empty());
}

TEST(WindowControlSequenceTest, ADragOfOneConfigureIsRejectedBecauseItInterpolatesNothing) {
    EXPECT_FALSE(parse("{Drag:800x600:1000x600:1}").error.empty());
}

TEST(WindowControlSequenceTest, AStepDescribesItselfWithATagThatIsNotAFault) {
    const WindowControlSequence parsed = parse("{Tiled:1024x768}");

    ASSERT_EQ(parsed.steps.size(), 1U);
    EXPECT_EQ(describeWindowControlStep(parsed.steps[0]),
              "[rnl-geometry] configure 1024x768 maximized=0 fullscreen=0 tiled=1 resizing=0");
}

TEST(WindowControlSequenceTest, AMaximizedStepIsNotReportedTiled) {
    const WindowControlSequence parsed = parse("{Tiled:800x600}{Maximized}");

    ASSERT_EQ(parsed.steps.size(), 2U);
    EXPECT_EQ(describeWindowControlStep(parsed.steps[1]),
              "[rnl-geometry] configure 800x600 maximized=1 fullscreen=0 tiled=0 resizing=0");
}

TEST(WindowControlSequenceTest, AFullscreenDragStepDescribesEveryBitItCarries) {
    const WindowControlSequence parsed = parse("{Fullscreen:800x600}{Drag:800x600:1000x600:2}");

    ASSERT_EQ(parsed.steps.size(), 3U);
    EXPECT_EQ(describeWindowControlStep(parsed.steps[1]),
              "[rnl-geometry] configure 800x600 maximized=0 fullscreen=1 tiled=0 resizing=1");
}

} // namespace
