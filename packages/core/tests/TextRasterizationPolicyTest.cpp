#include "TextRasterizationPolicy.h"

#include <gtest/gtest.h>

namespace react_native_linux {

/**
 * Issue #71, item 1: the surface props are a deliberate, asserted choice — and one policy definition is the
 * only place the choice lives, so the golden raster surface and the window swapchain surfaces cannot drift
 * apart. The values pinned here are the ones the policy header documents: grayscale coverage (unknown pixel
 * geometry — no LCD strip order to fringe against) under device-independent fonts (Skia's platform gamma and
 * contrast adaptation off, so goldens cannot be moved by a driver or fontconfig change).
 */
TEST(TextRasterizationPolicyTest, ThePolicyIsGrayscaleCoverageUnderDeviceIndependentFonts) {
    const TextRasterizationPolicy policy = textRasterizationPolicy();

    EXPECT_TRUE(policy.deviceIndependentFonts);
    EXPECT_EQ(policy.pixelGeometry, TextPixelGeometry::Unknown);
    EXPECT_NE(policy.pixelGeometry, TextPixelGeometry::RgbHorizontal);
    EXPECT_NE(policy.pixelGeometry, TextPixelGeometry::BgrHorizontal);
}

/**
 * Issue #71, item 3: the gamma and contrast are Skia's build defaults (sRGB gamma, 0.5 contrast), which is a
 * stated pin rather than a tune — the device-independent flag is what stops a driver or fontconfig change from
 * moving them. Asserted so a Skia bump that changes the defaults names itself here.
 */
TEST(TextRasterizationPolicyTest, GammaAndContrastAreTheDocumentedSkiaDefaults) {
    const TextRasterizationPolicy policy = textRasterizationPolicy();

    EXPECT_FLOAT_EQ(policy.textGamma, 0.0F);
    EXPECT_FLOAT_EQ(policy.textContrast, 0.5F);
}

} // namespace react_native_linux
