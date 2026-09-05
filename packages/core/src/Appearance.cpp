#include "Appearance.h"

#include <utility>

namespace react_native_linux {

ColorScheme resolveEffectiveColorScheme(std::optional<ColorScheme> colorSchemeOverride, ColorScheme portalColorScheme) {
    return colorSchemeOverride.value_or(portalColorScheme);
}

bool shouldEmitOnOverrideChange(std::optional<ColorScheme> previousOverride, std::optional<ColorScheme> nextOverride) {
    return previousOverride != nextOverride;
}

bool shouldEmitOnPortalChange(std::optional<ColorScheme> currentOverride, ColorScheme previousPortalColorScheme,
                               ColorScheme nextPortalColorScheme) {
    return !currentOverride.has_value() && previousPortalColorScheme != nextPortalColorScheme;
}

AppearanceModel::AppearanceModel(ColorScheme initialPortalColorScheme) : portalColorScheme_(initialPortalColorScheme) {}

ColorScheme AppearanceModel::colorScheme() const {
    return resolveEffectiveColorScheme(colorSchemeOverride_, portalColorScheme_);
}

void AppearanceModel::setColorScheme(std::optional<ColorScheme> colorSchemeOverride) {
    const bool shouldEmit = shouldEmitOnOverrideChange(colorSchemeOverride_, colorSchemeOverride);

    colorSchemeOverride_ = colorSchemeOverride;

    if (shouldEmit && changeListener_) {
        changeListener_(colorScheme());
    }
}

void AppearanceModel::onPortalColorSchemeChanged(ColorScheme portalColorScheme) {
    const bool shouldEmit = shouldEmitOnPortalChange(colorSchemeOverride_, portalColorScheme_, portalColorScheme);

    portalColorScheme_ = portalColorScheme;

    if (shouldEmit && changeListener_) {
        changeListener_(colorScheme());
    }
}

void AppearanceModel::setChangeListener(std::function<void(ColorScheme)> listener) { changeListener_ = std::move(listener); }

} // namespace react_native_linux
