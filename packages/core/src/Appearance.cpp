#include "Appearance.h"

#include <utility>

namespace react_native_linux {

namespace {

constexpr uint32_t kPortalPreferDark = 1;
constexpr uint32_t kPortalPreferLight = 2;
constexpr std::string_view kLightName = "light";
constexpr std::string_view kDarkName = "dark";

} // namespace

std::optional<ColorScheme> colorSchemeFromPortalSetting(uint32_t portalSettingValue) {
    if (portalSettingValue == kPortalPreferDark) {
        return ColorScheme::Dark;
    }

    if (portalSettingValue == kPortalPreferLight) {
        return ColorScheme::Light;
    }

    return std::nullopt;
}

ColorScheme resolvePortalSettingOrFallback(uint32_t portalSettingValue) {
    return colorSchemeFromPortalSetting(portalSettingValue).value_or(kFallbackColorScheme);
}

std::optional<ColorScheme> colorSchemeFromName(std::string_view colorSchemeName) {
    if (colorSchemeName == kLightName) {
        return ColorScheme::Light;
    }

    if (colorSchemeName == kDarkName) {
        return ColorScheme::Dark;
    }

    return std::nullopt;
}

std::string_view nameOfColorScheme(ColorScheme colorScheme) {
    return colorScheme == ColorScheme::Dark ? kDarkName : kLightName;
}

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
    const std::lock_guard<std::mutex> lock(mutex_);

    return resolveEffectiveColorScheme(colorSchemeOverride_, portalColorScheme_);
}

void AppearanceModel::mutateAndNotify(const std::function<bool()>& mutateUnderStateLock) {
    const std::lock_guard<std::mutex> notificationLock(notificationMutex_);
    std::function<void(ColorScheme)> listenerToInvoke;
    ColorScheme resolvedColorScheme = kFallbackColorScheme;

    {
        const std::lock_guard<std::mutex> lock(mutex_);
        const bool shouldEmit = mutateUnderStateLock();

        if (shouldEmit) {
            resolvedColorScheme = resolveEffectiveColorScheme(colorSchemeOverride_, portalColorScheme_);
            listenerToInvoke = changeListener_;
        }
    }

    if (listenerToInvoke) {
        listenerToInvoke(resolvedColorScheme);
    }
}

void AppearanceModel::setColorScheme(std::optional<ColorScheme> colorSchemeOverride) {
    mutateAndNotify([this, colorSchemeOverride] {
        const bool shouldEmit = shouldEmitOnOverrideChange(colorSchemeOverride_, colorSchemeOverride);

        colorSchemeOverride_ = colorSchemeOverride;

        return shouldEmit;
    });
}

void AppearanceModel::onPortalColorSchemeChanged(ColorScheme portalColorScheme) {
    mutateAndNotify([this, portalColorScheme] {
        const bool shouldEmit = shouldEmitOnPortalChange(colorSchemeOverride_, portalColorScheme_, portalColorScheme);

        portalColorScheme_ = portalColorScheme;

        return shouldEmit;
    });
}

void AppearanceModel::setChangeListener(std::function<void(ColorScheme)> listener) {
    const std::lock_guard<std::mutex> lock(mutex_);

    changeListener_ = std::move(listener);
}

} // namespace react_native_linux
