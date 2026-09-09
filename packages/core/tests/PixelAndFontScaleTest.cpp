#include "FontSizeScaling.h"
#include "WindowDecorations.h"

#include <cmath>
#include <gtest/gtest.h>

// The pixel and font-scale half of #418: `PixelUtilTest.kt`'s six cases, ported case for case.
//
// | Upstream case (`ReactAndroid/.../uimanager/PixelUtilTest.kt`, v0.87.1)  | Here                                |
// | ---------------------------------------------------------------------- | ----------------------------------- |
// | `toPixelFromSP_respectsFontScaleLessThanOne`                            | `AFontScaleBelowOneShrinksTheText`  |
// | `toPixelFromSP_respectsFontScaleGreaterThanOne`                         | `AFontScaleAboveOneGrowsTheText`    |
// | `toPixelFromSP_respectsMaxFontScale`                                    | `TheCeilingCapsAFontScaleAboveIt`   |
// | `toPixelFromSP_doesNotApplyMaxFontScaleWhenFontScaleIsLess`             | `TheCeilingIsNeverAFloor`           |
// | `toPixelFromDIP_convertsCorrectly`                                      | `LogicalUnitsScaleByTheOutputScale` |
// | `initDisplayMetrics_preservesFontScale`                                 | not applicable — see below          |
//
// Upstream folds the density into every `sp` literal, because on Android `sp -> px` is one product of three
// numbers: `sp * density * fontScale`. Here those two factors live in different places — the text-scaling factor
// is `TextAttributes::fontSizeMultiplier` and stays in points, and the output scale is applied once by the
// surface to every logical unit alike — so each `sp` case asserts both halves: the point size this platform
// computes, and that point size times Android's density, which is upstream's own literal to the digit.
//
// `initDisplayMetrics_preservesFontScale` has no port. It asserts that `DisplayMetricsHolder.initDisplayMetrics`
// copies `scaledDensity` off an Android `Context`'s `Resources` under a Robolectric-mocked `WINDOW_SERVICE`, and
// its inputs are density buckets (`DisplayMetrics.DENSITY_XXHIGH`). Wayland has no density bucket and no screen
// metrics a client may read: the scale a surface is drawn at is the output scale the compositor sends it, which
// is fractional far more often than it is a bucket, and where it comes from is #113. There is no mechanism here
// for that case to describe, so it is recorded rather than manufactured.
//
// The two cases below the upstream table are not ports. They cover the `maxFontSizeMultiplier` branch upstream's
// six leave untested — `PixelUtil.toPixelFromSP`'s `maxFontScale >= 1` guard is never exercised with an argument
// that fails it — which `TextAttributes` reaches on every default-constructed run, since it spells "no ceiling"
// as NaN.

namespace {

using react_native_linux::DecorationMetrics;
using react_native_linux::DecorationMode;
using react_native_linux::logicalToSurface;
using react_native_linux::scaledFontSize;
using react_native_linux::surfaceToLogical;
using react_native_linux::WindowExtent;

constexpr float kFontSize = 16.0F;

// `DisplayMetrics.DENSITY_XXHIGH`, the bucket every upstream case sets up, kept only so the ported assertions can
// state upstream's own pixel literals.
constexpr float kAndroidDensity = 3.0F;

// Upstream compares with `abs(result - expected) < 0.1f`.
constexpr float kTolerance = 0.1F;

// `TextAttributes` defaults both multipliers to NaN, which is how React Native spells an absent one.
const float kAbsent = std::nanf("");

// Server-side decorations, where the title-bar term is zero and the conversion pair is a pure scale conversion.
constexpr DecorationMetrics kNoBar{};

float logicalToSurfaceWidth(uint32_t logicalWidth, double scale) {
    return static_cast<float>(logicalToSurface(DecorationMode::Server, /*isFullscreen=*/false, kNoBar, scale,
                                               WindowExtent{.width = logicalWidth, .height = logicalWidth})
                                  .width);
}

TEST(PixelAndFontScaleTest, AFontScaleBelowOneShrinksTheText) {
    const float points = scaledFontSize(kFontSize, 0.85F, kAbsent);

    EXPECT_NEAR(points, 13.6F, kTolerance);
    EXPECT_NEAR(points * kAndroidDensity, 40.8F, kTolerance);
}

TEST(PixelAndFontScaleTest, AFontScaleAboveOneGrowsTheText) {
    const float points = scaledFontSize(kFontSize, 1.3F, kAbsent);

    EXPECT_NEAR(points, 20.8F, kTolerance);
    EXPECT_NEAR(points * kAndroidDensity, 62.4F, kTolerance);
}

TEST(PixelAndFontScaleTest, TheCeilingCapsAFontScaleAboveIt) {
    const float points = scaledFontSize(kFontSize, 2.0F, 1.5F);

    EXPECT_NEAR(points, 24.0F, kTolerance);
    EXPECT_NEAR(points * kAndroidDensity, 72.0F, kTolerance);
    EXPECT_LT(points, scaledFontSize(kFontSize, 2.0F, kAbsent));
}

TEST(PixelAndFontScaleTest, TheCeilingIsNeverAFloor) {
    const float points = scaledFontSize(kFontSize, 0.8F, 1.5F);

    EXPECT_NEAR(points, 12.8F, kTolerance);
    EXPECT_NEAR(points * kAndroidDensity, 38.4F, kTolerance);
    EXPECT_FLOAT_EQ(points, scaledFontSize(kFontSize, 0.8F, kAbsent));
}

TEST(PixelAndFontScaleTest, AnAbsentOrSubUnitCeilingDoesNotClampAtAll) {
    EXPECT_FLOAT_EQ(scaledFontSize(kFontSize, 1.5F, kAbsent), 24.0F);
    EXPECT_FLOAT_EQ(scaledFontSize(kFontSize, 1.5F, 0.5F), 24.0F);
    EXPECT_FLOAT_EQ(scaledFontSize(kFontSize, 1.5F, 1.0F), kFontSize);
}

TEST(PixelAndFontScaleTest, AnAbsentFontScaleIsOne) {
    EXPECT_FLOAT_EQ(scaledFontSize(kFontSize, kAbsent, kAbsent), kFontSize);
    EXPECT_FLOAT_EQ(scaledFontSize(kFontSize, kAbsent, 1.5F), kFontSize);
}

// The desktop values the GNOME `text-scaling-factor` setting actually takes, run through the ceiling an app puts
// on them. At 1.0 the ceiling is invisible, which is why the arithmetic can be wrong on Android for years before
// anyone notices; at 1.5 it is the difference between 24 pt and 20 pt.
TEST(PixelAndFontScaleTest, ADesktopTextScalingFactorRunsThroughTheSameCeiling) {
    EXPECT_FLOAT_EQ(scaledFontSize(kFontSize, 1.0F, 1.25F), kFontSize);
    EXPECT_FLOAT_EQ(scaledFontSize(kFontSize, 1.25F, 1.25F), 20.0F);
    EXPECT_FLOAT_EQ(scaledFontSize(kFontSize, 1.5F, 1.25F), 20.0F);
}

// `toPixelFromDIP_convertsCorrectly` — `dp -> px` is `dp * density`, and its inverse `px -> dp` is
// `PixelUtil.toDIPFromPixel`. Both live in `WindowDecorations`' conversion pair here, the only logical-to-surface
// converter this platform has; upstream's literal is the density-3.0 row.
TEST(PixelAndFontScaleTest, LogicalUnitsScaleByTheOutputScale) {
    EXPECT_FLOAT_EQ(logicalToSurfaceWidth(16, kAndroidDensity), 48.0F);
    EXPECT_FLOAT_EQ(logicalToSurfaceWidth(16, 1.25), 20.0F);
    EXPECT_FLOAT_EQ(logicalToSurfaceWidth(16, 1.5), 24.0F);

    EXPECT_EQ(surfaceToLogical(DecorationMode::Server, /*isFullscreen=*/false, kNoBar, 1.5,
                               WindowExtent{.width = 24, .height = 24})
                  .width,
              16U);
}

} // namespace
