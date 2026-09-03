#include "DimensionsSource.h"

#include <gtest/gtest.h>

#include <initializer_list>
#include <optional>

namespace {

using react_native_linux::DimensionsSource;
using react_native_linux::DisplayMetrics;

constexpr double kWindowWidth = 1024.0;
constexpr double kWindowHeight = 768.0;
constexpr double kFractionalScale = 1.5;

TEST(DimensionsTest, ThePreConfigureValueIsTheRequestedSurfaceDefault) {
    const DimensionsSource source;

    const DisplayMetrics metrics = source.metrics();

    EXPECT_DOUBLE_EQ(metrics.width, DimensionsSource::kDefaultWidth);
    EXPECT_DOUBLE_EQ(metrics.height, DimensionsSource::kDefaultHeight);
    EXPECT_DOUBLE_EQ(metrics.scale, DimensionsSource::kDefaultScale);
    EXPECT_DOUBLE_EQ(metrics.fontScale, 1.0);
}

TEST(DimensionsTest, AConfigureBecomesTheReportedExtent) {
    DimensionsSource source;

    source.configure(kWindowWidth, kWindowHeight, DimensionsSource::kDefaultScale);

    const DisplayMetrics metrics = source.metrics();

    EXPECT_DOUBLE_EQ(metrics.width, kWindowWidth);
    EXPECT_DOUBLE_EQ(metrics.height, kWindowHeight);
    EXPECT_DOUBLE_EQ(metrics.scale, DimensionsSource::kDefaultScale);
}

TEST(DimensionsTest, AScaleIsReportedAsConfigured) {
    DimensionsSource source;

    source.configure(kWindowWidth, kWindowHeight, kFractionalScale);

    EXPECT_DOUBLE_EQ(source.metrics().scale, kFractionalScale);
}

TEST(DimensionsTest, ANonPositiveExtentIsRejectedAndTheLastGoodValueSurvives) {
    DimensionsSource source;

    source.configure(kWindowWidth, kWindowHeight, kFractionalScale);
    source.takeChangeIfAny();

    for (const double rejected : {0.0, -kWindowWidth}) {
        source.configure(rejected, kWindowHeight, kFractionalScale);
        source.configure(kWindowWidth, rejected, kFractionalScale);
        source.configure(kWindowWidth, kWindowHeight, rejected);
    }

    EXPECT_DOUBLE_EQ(source.metrics().width, kWindowWidth);
    EXPECT_DOUBLE_EQ(source.metrics().height, kWindowHeight);
    EXPECT_DOUBLE_EQ(source.metrics().scale, kFractionalScale);
    EXPECT_EQ(source.takeChangeIfAny(), std::nullopt);
}

TEST(DimensionsTest, NoPathThroughTheSourceCanReportZero) {
    DimensionsSource source;

    for (const double rejected : {0.0, -1.0}) {
        source.configure(rejected, rejected, rejected);

        const DisplayMetrics metrics = source.metrics();

        EXPECT_GT(metrics.width, 0.0);
        EXPECT_GT(metrics.height, 0.0);
        EXPECT_GT(metrics.scale, 0.0);
        EXPECT_GT(metrics.fontScale, 0.0);
    }
}

TEST(DimensionsTest, NothingIsPendingBeforeTheFirstConfigure) {
    DimensionsSource source;

    EXPECT_EQ(source.takeChangeIfAny(), std::nullopt);
}

TEST(DimensionsTest, ARepeatedConfigureIsNotAChange) {
    DimensionsSource source;

    source.configure(kWindowWidth, kWindowHeight, kFractionalScale);
    source.takeChangeIfAny();
    source.configure(kWindowWidth, kWindowHeight, kFractionalScale);

    EXPECT_EQ(source.takeChangeIfAny(), std::nullopt);
}

TEST(DimensionsTest, EachFieldOnItsOwnIsAChange) {
    DimensionsSource source;

    source.configure(kWindowWidth, kWindowHeight, DimensionsSource::kDefaultScale);
    source.takeChangeIfAny();

    source.configure(kWindowWidth + 1.0, kWindowHeight, DimensionsSource::kDefaultScale);
    EXPECT_TRUE(source.takeChangeIfAny().has_value());

    source.configure(kWindowWidth + 1.0, kWindowHeight + 1.0, DimensionsSource::kDefaultScale);
    EXPECT_TRUE(source.takeChangeIfAny().has_value());

    source.configure(kWindowWidth + 1.0, kWindowHeight + 1.0, kFractionalScale);
    EXPECT_TRUE(source.takeChangeIfAny().has_value());
}

TEST(DimensionsTest, ABurstOfConfiguresCoalescesIntoOneChange) {
    DimensionsSource source;

    for (double width = kWindowWidth; width < kWindowWidth + 10.0; width += 1.0) {
        source.configure(width, kWindowHeight, DimensionsSource::kDefaultScale);
    }

    const std::optional<DisplayMetrics> change = source.takeChangeIfAny();

    ASSERT_TRUE(change.has_value());
    EXPECT_DOUBLE_EQ(change.value().width, kWindowWidth + 9.0);
    EXPECT_DOUBLE_EQ(change.value().height, kWindowHeight);
    EXPECT_EQ(source.takeChangeIfAny(), std::nullopt);
}

} // namespace
