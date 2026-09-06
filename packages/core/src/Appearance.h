#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace react_native_linux {

enum class ColorScheme { Light, Dark };

/**
 * The documented fallback for "no portal said anything": no `org.freedesktop.portal.Desktop` on the bus, a
 * portal too old to carry `org.freedesktop.appearance`, or the value 0 "no preference". Light rather than dark
 * because that is what the XDG specification says an unset preference means, and because a headless run — every
 * golden in this repository — has no bus at all and must still be reproducible.
 */
constexpr ColorScheme kFallbackColorScheme = ColorScheme::Light;

/**
 * `Appearance.setColorScheme` from `NativeAppearance.js`: an app-level override that wins over whatever the
 * portal reports, until it is cleared with `null`.
 *
 * The portal round trip itself — `org.freedesktop.appearance color-scheme` over
 * `org.freedesktop.portal.Settings` — lives in `AppearancePortal`, which calls `onPortalColorSchemeChanged` once
 * at start and again on every `SettingChanged`. What this file owns is the precedence rule between that signal
 * and the override, and the rule is small enough to be three pure functions rather than a class with a D-Bus
 * dependency:
 *
 * - `resolveEffectiveColorScheme` — override, if set, otherwise the portal value. `getColorScheme()` is this.
 * - `shouldEmitOnOverrideChange` — `setColorScheme` fires `appearanceChanged` once whenever the override itself
 *   changes (set, cleared, or switched), independent of whether the *effective* scheme also changed — clearing
 *   an override back to a portal value that happens to match is still an event apps can act on.
 * - `shouldEmitOnPortalChange` — a portal signal fires `appearanceChanged` only when there is no override in
 *   place and the reported value actually changed. An override in effect swallows the signal; the portal value
 *   is still recorded, so the next `setColorScheme(null)` resolves to it without a second round trip.
 *
 * Threading contract: like `Clipboard`, this is frame-thread state with no synchronisation of its own. Every
 * write comes from the frame thread — `AppearancePortal::processPendingSignals` is pumped from
 * `WindowSession::deliverInput`, the same place `publishPendingDimensions` is called from — and every read that
 * JavaScript makes goes through the module's `CallInvoker`. Nothing here is touched off the frame thread, which
 * is why there is no mutex and no dispatch thread: the bus is polled where the frame already is.
 */
/**
 * `org.freedesktop.appearance color-scheme`, decoded. The XDG desktop portal settings interface defines exactly
 * three values — 0 "no preference", 1 "prefer dark", 2 "prefer light" — and reserves the rest, so anything else
 * is as much "the portal did not say" as 0 is. Nothing is returned for those, and the caller keeps its fallback.
 *
 * https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Settings.html
 */
std::optional<ColorScheme> colorSchemeFromPortalSetting(uint32_t portalSettingValue);

/**
 * `ColorSchemeName` and `ColorSchemeOverride` from `NativeAppearance.js`, decoded. `light` and `dark` name a
 * scheme; `auto`, `unspecified` and anything else are "no override", which is what `setColorScheme` clears with.
 */
std::optional<ColorScheme> colorSchemeFromName(std::string_view colorSchemeName);

/**
 * The `ColorSchemeName` string `Appearance.getColorScheme()` answers with.
 */
std::string_view nameOfColorScheme(ColorScheme colorScheme);

ColorScheme resolveEffectiveColorScheme(std::optional<ColorScheme> colorSchemeOverride, ColorScheme portalColorScheme);

bool shouldEmitOnOverrideChange(std::optional<ColorScheme> previousOverride, std::optional<ColorScheme> nextOverride);

bool shouldEmitOnPortalChange(std::optional<ColorScheme> currentOverride, ColorScheme previousPortalColorScheme,
                               ColorScheme nextPortalColorScheme);

class AppearanceModel {
public:
    explicit AppearanceModel(ColorScheme initialPortalColorScheme);

    ColorScheme colorScheme() const;

    void setColorScheme(std::optional<ColorScheme> colorSchemeOverride);
    void onPortalColorSchemeChanged(ColorScheme portalColorScheme);

    void setChangeListener(std::function<void(ColorScheme)> listener);

private:
    std::optional<ColorScheme> colorSchemeOverride_;
    ColorScheme portalColorScheme_;
    std::function<void(ColorScheme)> changeListener_;
};

} // namespace react_native_linux
