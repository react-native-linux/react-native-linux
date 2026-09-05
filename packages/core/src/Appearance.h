#pragma once

#include <functional>
#include <optional>

namespace react_native_linux {

enum class ColorScheme { Light, Dark };

/**
 * `Appearance.setColorScheme` from `NativeAppearance.js`: an app-level override that wins over whatever the
 * portal reports, until it is cleared with `null`.
 *
 * The portal round trip itself — `org.freedesktop.appearance color-scheme` over
 * `org.freedesktop.portal.Settings` — is issue #52 and is not implemented here; `onPortalColorSchemeChanged` is
 * the seam #52's D-Bus listener calls into. What this file owns is the precedence rule between that signal and
 * the override, and the rule is small enough to be three pure functions rather than a class with a D-Bus
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
 * Threading contract: like `Clipboard`, this is frame-thread state with no synchronisation of its own. The
 * portal listener in #52 marshals onto the frame thread before calling `onPortalColorSchemeChanged`, exactly as
 * `InputDispatcher` does for Wayland callbacks.
 */
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
