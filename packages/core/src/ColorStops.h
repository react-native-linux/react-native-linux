#pragma once

#include <vector>

#include <react/renderer/graphics/ColorStop.h>

namespace react_native_linux {

/**
 * One gradient layer's colour stops as the ramp paints them: every position resolved to a fraction of the gradient
 * line, CSS's fix-up applied (https://drafts.csswg.org/css-images-4/#coloring-gradient-line), and each transition
 * hint (a stop with a position and no colour) expanded into the nine coloured stops browsers paint it with (#418).
 *
 * A port of React Native's Android `ColorStopUtils.getFixedColorStops`, itself a port of Blink's, so the two
 * platforms paint the same ramp; `ColorStopTest` is Android's `ColorStopTest`, case for case. Skia-free, so the
 * unit tier scores it rather than only the gradient goldens.
 */
std::vector<facebook::react::ProcessedColorStop>
fixedColorStops(const std::vector<facebook::react::ColorStop>& colorStops, float gradientLineLength);

} // namespace react_native_linux
