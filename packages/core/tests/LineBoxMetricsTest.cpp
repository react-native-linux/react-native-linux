#include "LineBoxMetrics.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <optional>
#include <string_view>

namespace {

using react_native_linux::LineBoxMetrics;
using react_native_linux::lineHeightRatio;
using react_native_linux::measureLineBox;

// A font whose ascent and descent add up to more than its font size, which is what every real text font does and
// the reason `lineHeight == fontSize` is the degenerate case react-native#49886 reports.
constexpr float kAscent = 22.0F;
constexpr float kDescent = 6.0F;
constexpr float kNaturalHeight = 28.0F;
constexpr float kFontSize = 20.0F;

// React Native spells "no lineHeight" as NaN, all the way down to `TextAttributes::lineHeight`.
const float kAbsentLineHeight = std::nanf("");

struct LineBoxCase {
    std::string_view name;
    float fontSize;
    float lineHeight;
    float lineBoxHeight;
    float halfLeading;
    float baselineFromTop;
};

constexpr std::array<LineBoxCase, 4> kLineBoxCases{
    LineBoxCase{.name = "lineHeight equal to the font size",
                .fontSize = kFontSize,
                .lineHeight = 20.0F,
                .lineBoxHeight = 20.0F,
                .halfLeading = -4.0F,
                .baselineFromTop = 18.0F},
    LineBoxCase{.name = "lineHeight below the font size",
                .fontSize = kFontSize,
                .lineHeight = 14.0F,
                .lineBoxHeight = 14.0F,
                .halfLeading = -7.0F,
                .baselineFromTop = 15.0F},
    LineBoxCase{.name = "lineHeight equal to the natural ascent plus descent",
                .fontSize = kFontSize,
                .lineHeight = kNaturalHeight,
                .lineBoxHeight = kNaturalHeight,
                .halfLeading = 0.0F,
                .baselineFromTop = kAscent},
    LineBoxCase{.name = "lineHeight at twice the font size",
                .fontSize = kFontSize,
                .lineHeight = 40.0F,
                .lineBoxHeight = 40.0F,
                .halfLeading = 6.0F,
                .baselineFromTop = 28.0F}};

TEST(LineBoxMetricsTest, SplitsTheLeadingEvenlyAcrossTheLineHeightTable) {
    for (const LineBoxCase& lineBoxCase : kLineBoxCases) {
        SCOPED_TRACE(lineBoxCase.name);

        const LineBoxMetrics metrics = measureLineBox(kAscent, kDescent, lineBoxCase.fontSize, lineBoxCase.lineHeight);

        EXPECT_FLOAT_EQ(metrics.lineBoxHeight, lineBoxCase.lineBoxHeight);
        EXPECT_FLOAT_EQ(metrics.halfLeading, lineBoxCase.halfLeading);
        EXPECT_FLOAT_EQ(metrics.baselineFromTop, lineBoxCase.baselineFromTop);
        EXPECT_FLOAT_EQ(metrics.baselineFromTop, metrics.halfLeading + kAscent);
        EXPECT_FLOAT_EQ(metrics.lineBoxHeight - metrics.baselineFromTop - kDescent, metrics.halfLeading);
    }
}

TEST(LineBoxMetricsTest, FallsBackToTheFontsOwnMetricsWithoutAUsableLineHeight) {
    const LineBoxMetrics absent = measureLineBox(kAscent, kDescent, kFontSize, kAbsentLineHeight);
    const LineBoxMetrics zeroFontSize = measureLineBox(kAscent, kDescent, 0.0F, 40.0F);

    EXPECT_FLOAT_EQ(absent.lineBoxHeight, kNaturalHeight);
    EXPECT_FLOAT_EQ(absent.halfLeading, 0.0F);
    EXPECT_FLOAT_EQ(absent.baselineFromTop, kAscent);
    EXPECT_FLOAT_EQ(zeroFontSize.lineBoxHeight, kNaturalHeight);
    EXPECT_FLOAT_EQ(zeroFontSize.halfLeading, 0.0F);
    EXPECT_FLOAT_EQ(zeroFontSize.baselineFromTop, kAscent);
}

TEST(LineBoxMetricsTest, OverflowsTheLineBoxInsteadOfShrinkingTheGlyphs) {
    const LineBoxMetrics tight = measureLineBox(kAscent, kDescent, kFontSize, 14.0F);

    EXPECT_LT(tight.halfLeading, 0.0F);
    EXPECT_LT(tight.lineBoxHeight, kNaturalHeight);
    EXPECT_GT(tight.baselineFromTop + kDescent, tight.lineBoxHeight);
    EXPECT_LT(tight.baselineFromTop - kAscent, 0.0F);
}

TEST(LineBoxMetricsTest, ConvertsLineHeightPointsToAMultipleOfTheFontSize) {
    const std::optional<float> doubled = lineHeightRatio(kFontSize, 40.0F);
    const std::optional<float> matched = lineHeightRatio(kFontSize, kFontSize);
    const std::optional<float> tight = lineHeightRatio(kFontSize, 14.0F);

    ASSERT_TRUE(doubled.has_value());
    ASSERT_TRUE(matched.has_value());
    ASSERT_TRUE(tight.has_value());
    EXPECT_FLOAT_EQ(doubled.value(), 2.0F);
    EXPECT_FLOAT_EQ(matched.value(), 1.0F);
    EXPECT_FLOAT_EQ(tight.value(), 0.7F);
}

TEST(LineBoxMetricsTest, ReportsNoRatioWithoutALineHeightOrAPositiveFontSize) {
    EXPECT_FALSE(lineHeightRatio(kFontSize, kAbsentLineHeight).has_value());
    EXPECT_FALSE(lineHeightRatio(0.0F, 40.0F).has_value());
    EXPECT_FALSE(lineHeightRatio(-10.0F, 40.0F).has_value());
}

} // namespace
