#include "OutputScale.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <vector>

namespace {

using facebook::react::Rect;
using react_native_linux::bufferExtentOf;
using react_native_linux::kFractionalScaleDenominator;
using react_native_linux::scaleDamageOutward;

constexpr uint32_t kScaleOne = 120;
constexpr uint32_t kScaleOneAndAQuarter = 150;
constexpr uint32_t kScaleOneAndAHalf = 180;
constexpr uint32_t kScaleTwo = 240;

TEST(OutputScaleTest, ScaleOneLeavesTheExtentAsItIs) {
    EXPECT_EQ(bufferExtentOf(800, kScaleOne), 800U);
    EXPECT_EQ(bufferExtentOf(0, kScaleOne), 0U);
}

TEST(OutputScaleTest, AnExactProductNeedsNoRounding) {
    EXPECT_EQ(bufferExtentOf(800, kScaleOneAndAHalf), 1200U);
    EXPECT_EQ(bufferExtentOf(601, kScaleTwo), 1202U);
}

TEST(OutputScaleTest, AHalfPixelRoundsAwayFromZeroAsTheProtocolSpecifies) {
    EXPECT_EQ(bufferExtentOf(601, kScaleOneAndAHalf), 902U);
    EXPECT_EQ(bufferExtentOf(2, kScaleOneAndAQuarter), 3U);
}

TEST(OutputScaleTest, AnyOtherFractionRoundsToTheNearestPixel) {
    EXPECT_EQ(bufferExtentOf(1, kScaleOneAndAQuarter), 1U);
    EXPECT_EQ(bufferExtentOf(3, kScaleOneAndAQuarter), 4U);
}

TEST(OutputScaleTest, LogicalDamageBecomesTheBufferPixelsItCovers) {
    const std::vector<Rect> logical{Rect{.origin = {.x = 10, .y = 20}, .size = {.width = 30, .height = 40}}};

    EXPECT_EQ(scaleDamageOutward(logical, kScaleOneAndAHalf, kFractionalScaleDenominator),
              (std::vector<Rect>{Rect{.origin = {.x = 15, .y = 30}, .size = {.width = 45, .height = 60}}}));
}

TEST(OutputScaleTest, AFractionalEdgeGrowsOutwardSoNoCoveredPixelIsLost) {
    const std::vector<Rect> logical{Rect{.origin = {.x = 1, .y = 3}, .size = {.width = 1, .height = 1}}};

    EXPECT_EQ(scaleDamageOutward(logical, kScaleOneAndAHalf, kFractionalScaleDenominator),
              (std::vector<Rect>{Rect{.origin = {.x = 1, .y = 4}, .size = {.width = 2, .height = 2}}}));
}

TEST(OutputScaleTest, BufferDamageConvertsBackToTheLogicalUnitsThatContainIt) {
    const std::vector<Rect> buffer{Rect{.origin = {.x = 0, .y = 0}, .size = {.width = 1200, .height = 901}},
                                   Rect{.origin = {.x = 4, .y = 4}, .size = {.width = 1, .height = 1}}};

    EXPECT_EQ(scaleDamageOutward(buffer, kFractionalScaleDenominator, kScaleOneAndAHalf),
              (std::vector<Rect>{Rect{.origin = {.x = 0, .y = 0}, .size = {.width = 800, .height = 601}},
                                 Rect{.origin = {.x = 2, .y = 2}, .size = {.width = 2, .height = 2}}}));
}

TEST(OutputScaleTest, NoDamageStaysNoDamage) {
    EXPECT_TRUE(scaleDamageOutward({}, kScaleTwo, kFractionalScaleDenominator).empty());
}

} // namespace
