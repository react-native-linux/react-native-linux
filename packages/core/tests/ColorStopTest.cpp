#include "ColorStops.h"

#include <gtest/gtest.h>
#include <optional>
#include <vector>

#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/ValueUnit.h>

/**
 * React Native's Android `ColorStopTest` (packages/react-native/ReactAndroid/src/test/java/com/facebook/react/
 * uimanager/style/ColorStopTest.kt at v0.87.1), ported case for case onto `fixedColorStops` (#418). Where upstream
 * asserts a literal, the port asserts the same literal; `Color.RED` and friends are Android's ARGB constants. The
 * cases after the port are this file's own, for the hint branches the upstream file does not reach.
 *
 * | Upstream case                                              | Here                                           |
 * | ---------------------------------------------------------- | ---------------------------------------------- |
 * | testBasicColorStops                                        | BasicColorStops                                |
 * | testColorStopsWithFirstAndLastPositionsMissing             | ColorStopsWithFirstAndLastPositionsMissing     |
 * | testColorStopsWithLessPositionValueThanPreviousPosition    | ColorStopsWithLessPositionValueThanPrevious... |
 * | testColorStopsWithMissingMiddlePositions                   | ColorStopsWithMissingMiddlePositions           |
 * | testColorStopsWithMixedUnits                               | ColorStopsWithMixedUnits                       |
 * | testColorStopsWithMultipleTransitionHints                  | ColorStopsWithMultipleTransitionHints          |
 * | testColorStopsWithPositionedStopAdjacentToUnpositionedStop | ColorStopsWithPositionedStopAdjacentTo...      |
 */
namespace {

using facebook::react::ColorStop;
using facebook::react::SharedColor;
using facebook::react::UnitType;
using facebook::react::ValueUnit;
using react_native_linux::fixedColorStops;

SharedColor argb(uint32_t value) {
    return facebook::react::colorFromRGBA(static_cast<uint8_t>(value >> 16U), static_cast<uint8_t>(value >> 8U),
                                          static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 24U));
}

const SharedColor kRed = argb(0xFFFF0000U);
const SharedColor kGreen = argb(0xFF00FF00U);
const SharedColor kBlue = argb(0xFF0000FFU);
const SharedColor kGray = argb(0xFF888888U);
const SharedColor kCyan = argb(0xFF00FFFFU);
const SharedColor kTransparent = argb(0x00000000U);
const SharedColor kYellow = argb(0xFFFFFF00U);
const SharedColor kMagenta = argb(0xFFFF00FFU);

ColorStop stop(SharedColor color, float percent) {
    return ColorStop{.color = color, .position = ValueUnit{percent, UnitType::Percent}};
}

ColorStop unpositioned(SharedColor color) { return ColorStop{.color = color, .position = {}}; }

ColorStop hint(float percent) { return ColorStop{.color = {}, .position = ValueUnit{percent, UnitType::Percent}}; }

TEST(ColorStopTest, BasicColorStops) {
    const auto processed = fixedColorStops({stop(kRed, 0), stop(kGreen, 42)}, 60);

    ASSERT_EQ(processed.size(), 2U);
    EXPECT_EQ(processed[0].color, kRed);
    EXPECT_FLOAT_EQ(processed[0].position.value(), 0.0F);
    EXPECT_EQ(processed[1].color, kGreen);
    EXPECT_FLOAT_EQ(processed[1].position.value(), 0.42F);
}

TEST(ColorStopTest, ColorStopsWithFirstAndLastPositionsMissing) {
    const auto processed = fixedColorStops({unpositioned(kRed), stop(kGreen, 30), unpositioned(kBlue)}, 80);

    ASSERT_EQ(processed.size(), 3U);
    EXPECT_FLOAT_EQ(processed[0].position.value(), 0.0F);
    EXPECT_FLOAT_EQ(processed[1].position.value(), 0.3F);
    EXPECT_FLOAT_EQ(processed[2].position.value(), 1.0F);
    EXPECT_EQ(processed[2].color, kBlue);
}

TEST(ColorStopTest, ColorStopsWithLessPositionValueThanPreviousPosition) {
    const auto processed =
        fixedColorStops({unpositioned(kRed), stop(kGreen, 30), stop(kBlue, 20), stop(kGray, 60), stop(kCyan, 50)}, 80);

    ASSERT_EQ(processed.size(), 5U);
    EXPECT_FLOAT_EQ(processed[0].position.value(), 0.0F);
    EXPECT_FLOAT_EQ(processed[1].position.value(), 0.3F);
    EXPECT_FLOAT_EQ(processed[2].position.value(), 0.3F);
    EXPECT_FLOAT_EQ(processed[3].position.value(), 0.6F);
    EXPECT_FLOAT_EQ(processed[4].position.value(), 0.6F);
    EXPECT_EQ(processed[4].color, kCyan);
}

TEST(ColorStopTest, ColorStopsWithMissingMiddlePositions) {
    const auto processed =
        fixedColorStops({stop(kRed, 0), unpositioned(kGreen), unpositioned(kBlue), stop(kTransparent, 100)}, 100);

    ASSERT_EQ(processed.size(), 4U);
    EXPECT_FLOAT_EQ(processed[0].position.value(), 0.0F);
    EXPECT_FLOAT_EQ(processed[1].position.value(), 0.33333334F);
    EXPECT_FLOAT_EQ(processed[2].position.value(), 0.6666667F);
    EXPECT_FLOAT_EQ(processed[3].position.value(), 1.0F);
    EXPECT_EQ(processed[3].color, kTransparent);
}

TEST(ColorStopTest, ColorStopsWithMixedUnits) {
    const std::vector<ColorStop> stops{ColorStop{.color = kYellow, .position = ValueUnit{100, UnitType::Point}},
                                       stop(kBlue, 50)};
    const auto processed200 = fixedColorStops(stops, 200);
    const auto processed150 = fixedColorStops(stops, 150);

    EXPECT_FLOAT_EQ(processed200[0].position.value(), 0.5F);
    EXPECT_FLOAT_EQ(processed200[1].position.value(), 0.5F);
    EXPECT_FLOAT_EQ(processed150[0].position.value(), 0.6666667F);
    EXPECT_FLOAT_EQ(processed150[1].position.value(), 0.6666667F);
}

TEST(ColorStopTest, ColorStopsWithMultipleTransitionHints) {
    const auto processed =
        fixedColorStops({stop(kRed, 0), hint(10), stop(kGreen, 50), hint(85), stop(kBlue, 100)}, 100);

    ASSERT_EQ(processed.size(), 21U);
    EXPECT_EQ(processed.front().color, kRed);
    EXPECT_FLOAT_EQ(processed.front().position.value(), 0.0F);
    EXPECT_EQ(processed[10].color, kGreen);
    EXPECT_FLOAT_EQ(processed[10].position.value(), 0.5F);
    EXPECT_EQ(processed[20].color, kBlue);
    EXPECT_FLOAT_EQ(processed[20].position.value(), 1.0F);

    for (const auto& interpolated : processed) {
        EXPECT_TRUE(static_cast<bool>(interpolated.color));
        EXPECT_TRUE(interpolated.position.has_value());
    }
}

TEST(ColorStopTest, ColorStopsWithPositionedStopAdjacentToUnpositionedStop) {
    const auto processed = fixedColorStops(
        {stop(kRed, 0), stop(kGreen, 20), unpositioned(kBlue), stop(kYellow, 80), stop(kMagenta, 100)}, 100);

    ASSERT_EQ(processed.size(), 5U);
    EXPECT_FLOAT_EQ(processed[1].position.value(), 0.2F);
    EXPECT_FLOAT_EQ(processed[2].position.value(), 0.5F);
    EXPECT_FLOAT_EQ(processed[3].position.value(), 0.8F);
}

/** A gradient line with no length gives a length position nothing to be a fraction of, so it sits at the start. */
TEST(ColorStopTest, ALengthPositionOnAZeroLengthLineSitsAtTheStart) {
    const auto processed =
        fixedColorStops({ColorStop{.color = kRed, .position = ValueUnit{40, UnitType::Point}}, stop(kBlue, 100)}, 0);

    EXPECT_FLOAT_EQ(processed[0].position.value(), 0.0F);
}

/** A hint exactly between its stops is the default transition, so it disappears, as it does in a browser. */
TEST(ColorStopTest, AHintMidwayBetweenItsStopsIsDropped) {
    const auto processed = fixedColorStops({stop(kRed, 0), hint(50), stop(kBlue, 100)}, 100);

    ASSERT_EQ(processed.size(), 2U);
    EXPECT_EQ(processed[1].color, kBlue);
}

/** A hint on one of its stops is a hard edge: it takes the colour of the stop on its other side. */
TEST(ColorStopTest, AHintOnAStopTakesTheColourAcrossFromIt) {
    const auto atLeft = fixedColorStops({stop(kRed, 20), hint(20), stop(kBlue, 100)}, 100);
    const auto atRight = fixedColorStops({stop(kRed, 0), hint(80), stop(kBlue, 80)}, 100);

    EXPECT_EQ(atLeft[1].color, kBlue);
    EXPECT_EQ(atRight[1].color, kRed);
}

/** Nearer the left stop, the two short-side stops come first and the hint's colour is reached early. */
TEST(ColorStopTest, AHintNearItsLeftStopPutsTwoStopsBeforeItAndSevenAfter) {
    const auto processed = fixedColorStops({stop(kRed, 0), hint(25), stop(kBlue, 100)}, 100);

    ASSERT_EQ(processed.size(), 11U);
    EXPECT_FLOAT_EQ(processed[1].position.value(), 0.25F / 3.0F);
    EXPECT_FLOAT_EQ(processed[3].position.value(), 0.25F);
    EXPECT_EQ(facebook::react::blueFromColor(processed[3].color), 127U);
}

} // namespace
