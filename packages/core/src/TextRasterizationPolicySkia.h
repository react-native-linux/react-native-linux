#pragma once

#include "TextRasterizationPolicy.h"
#include "include/core/SkSurfaceProps.h"

namespace react_native_linux {

/**
 * The one Skia-side conversion of the text rasterization policy, used by every surface creation site (the
 * golden raster surface and the swapchain surfaces) so the renderers cannot drift in how the policy maps to
 * `SkSurfaceProps`. Lives in a Skia-typed header because the Hermes-free test target asserts the policy values
 * without linking Skia.
 */
inline SkSurfaceProps skSurfacePropsFor(const TextRasterizationPolicy& policy) {
    SkPixelGeometry geometry = kUnknown_SkPixelGeometry;

    switch (policy.pixelGeometry) {
    case TextPixelGeometry::RgbHorizontal:
        geometry = kRGB_H_SkPixelGeometry;
        break;
    case TextPixelGeometry::BgrHorizontal:
        geometry = kBGR_H_SkPixelGeometry;
        break;
    case TextPixelGeometry::RgbVertical:
        geometry = kRGB_V_SkPixelGeometry;
        break;
    case TextPixelGeometry::BgrVertical:
        geometry = kBGR_V_SkPixelGeometry;
        break;
    case TextPixelGeometry::Unknown:
        geometry = kUnknown_SkPixelGeometry;
        break;
    }

    const uint32_t deviceIndependentFonts =
        policy.deviceIndependentFonts ? SkSurfaceProps::kUseDeviceIndependentFonts_Flag : SkSurfaceProps::kDefault_Flag;

    return SkSurfaceProps{deviceIndependentFonts, geometry, policy.textContrast, policy.textGamma};
}

} // namespace react_native_linux
