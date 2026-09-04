#pragma once

namespace react_native_linux {

/**
 * Whether a pixel's colour stripes have a known order. Mirrors Skia's `SkPixelGeometry` without including it:
 * this header is included where Skia is not linked (the Hermes-free test target), and the renderers convert.
 */
enum class TextPixelGeometry {
    Unknown,
    RgbHorizontal,
    BgrHorizontal,
    RgbVertical,
    BgrVertical,
};

/**
 * The text rasterization policy every surface this platform draws text into is created with — the offscreen
 * golden raster surface and the window's Vulkan swapchain surfaces alike, because a golden that assumed one
 * pixel geometry and a window that rendered another would not predict each other (issue #71, item 1).
 *
 * The choice, and why:
 *
 * - **`pixelGeometry = Unknown` (grayscale coverage).** Subpixel LCD antialiasing assumes it knows the order of
 *   the pixel's colour stripes; a Wayland surface is composited with alpha over whatever is underneath, so
 *   nobody does — and assuming an order is what produces colour fringing on thin glyphs
 *   (https://github.com/microsoft/react-native-windows/issues/16340). Grayscale coverage is the policy, so the
 *   geometry is unknown rather than an RGB/BGR order that would re-enable LCD stripping.
 * - **`deviceIndependentFonts = true`.** The flag disables Skia's platform gamma and contrast adaptation, so
 *   the pixels a golden pins are the pixels Skia draws for every caller — a driver or fontconfig change cannot
 *   move them (item 3, and *Golden images*' font pin in docs/cpp-toolchain.md).
 * - **Gamma and contrast are Skia's build defaults** (sRGB gamma, 0.5 contrast — `SkTypes.h`'s `SK_GAMMA_*`),
 *   stated here rather than tuned: the device-independent flag above is what keeps a system's fontconfig or
 *   driver from moving them, which is what item 3 asks for. A one-line change here re-pins every surface at
 *   once, and the renderers pass `textGamma`/`textContrast` into `SkSurfaceProps` explicitly.
 *
 * The renderers convert this into `SkSurfaceProps` at surface creation; the conversion is mechanical (each
 * field maps one to one), which keeps "one policy, both paths" true without dragging a Skia header into files
 * that must stay Skia-free.
 */
struct TextRasterizationPolicy {
    bool deviceIndependentFonts{true};
    TextPixelGeometry pixelGeometry{TextPixelGeometry::Unknown};
    float textGamma{0.0F};   // sRGB, Skia's SK_GAMMA_EXPONENT default
    float textContrast{0.5F}; // Skia's SK_GAMMA_CONTRAST default
};

/**
 * The policy, as a value. Called by the renderers at surface creation so the construction site reads as the
 * decision it is.
 */
constexpr TextRasterizationPolicy textRasterizationPolicy() {
    return TextRasterizationPolicy{};
}

} // namespace react_native_linux
