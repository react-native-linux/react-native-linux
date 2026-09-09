#include "FontSizeScaling.h"

#include <algorithm>
#include <cmath>

namespace react_native_linux {

float scaledFontSize(float fontSize, float fontSizeMultiplier, float maxFontSizeMultiplier) noexcept {
    const float multiplier = std::isnan(fontSizeMultiplier) ? 1.0F : fontSizeMultiplier;
    const bool hasCeiling = maxFontSizeMultiplier >= 1.0F;

    return fontSize * (hasCeiling ? std::min(multiplier, maxFontSizeMultiplier) : multiplier);
}

} // namespace react_native_linux
