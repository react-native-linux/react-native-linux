#pragma once

namespace react_native_linux {

/**
 * The point size one run of text is drawn at once the user's text-scaling factor and the element's own ceiling on
 * it have both been applied.
 *
 * This is `PixelUtil.toPixelFromSP`'s arithmetic with the density factored out. Android folds three numbers into
 * one product — `sp * density * fontScale` — because there the density is a per-device bucket the text pipeline
 * has to apply itself. Here the output scale is applied once, by the surface, to every logical unit alike, so the
 * only factor this function owns is the text-scaling one and its result stays in points.
 *
 * `maxFontSizeMultiplier` is a **ceiling, never a floor**: a multiplier below it is left alone, and a ceiling
 * below `1` is no ceiling at all. Both platforms spell that the same way — `RCTAttributedTextUtils.mm`'s
 * `maxFontSizeMultiplier >= 1.0 ? fminf(...) : fontSizeMultiplier` and Android's `maxFontScale >= 1` guard in
 * `PixelUtil.toPixelFromSP` — and `TextAttributes` spells "no ceiling" as NaN, which fails that comparison and so
 * needs no case of its own. A NaN multiplier is the same absence and means `1`.
 *
 * On a desktop the scaling factor is not a device bucket but a setting the user types into
 * `org.gnome.desktop.interface text-scaling-factor`, routinely 1.25 or 1.5, which is why the ceiling an app puts
 * on it is load-bearing here rather than decorative. Where the factor reaches `TextAttributes` is #113; this is
 * the arithmetic it feeds.
 */
float scaledFontSize(float fontSize, float fontSizeMultiplier, float maxFontSizeMultiplier) noexcept;

} // namespace react_native_linux
