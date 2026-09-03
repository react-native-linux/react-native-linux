#include "LineBoxMetrics.h"

#include <cmath>
#include <optional>

namespace react_native_linux {

namespace {

bool hasLineHeightOverride(float fontSize, float lineHeight) {
    return fontSize > 0.0F && !std::isnan(lineHeight);
}

} // namespace

std::optional<float> lineHeightRatio(float fontSize, float lineHeight) {
    if (!hasLineHeightOverride(fontSize, lineHeight)) {
        return std::nullopt;
    }

    return lineHeight / fontSize;
}

LineBoxMetrics measureLineBox(float ascent, float descent, float fontSize, float lineHeight) {
    const float naturalHeight = ascent + descent;
    const float lineBoxHeight = hasLineHeightOverride(fontSize, lineHeight) ? lineHeight : naturalHeight;
    const float halfLeading = (lineBoxHeight - naturalHeight) / 2.0F;

    return LineBoxMetrics{
        .lineBoxHeight = lineBoxHeight, .halfLeading = halfLeading, .baselineFromTop = halfLeading + ascent};
}

} // namespace react_native_linux
