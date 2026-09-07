#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
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
 * Threading contract: `AppearanceModel` has two writers on two different threads. `onPortalColorSchemeChanged`
 * is called from the frame thread, through `AppearancePortal::processPendingSignals` pumped from
 * `WindowSession::deliverInput`. `setColorScheme` is called from the JavaScript thread, by
 * `LinuxAppearanceModule::setColorScheme`; `colorScheme()` is read from the JavaScript thread by
 * `getColorScheme` and by `__rnlPlatformColor` on every call. That pair — a frame-thread writer and a
 * JS-thread reader-and-writer — is exactly what `DimensionsSource` guards with a mutex, and `AppearanceModel`
 * guards its fields with one the same way: every accessor takes `mutex_` for the duration of its read or
 * write.
 *
 * `mutex_` alone is not enough for the two writers, though: releasing it before invoking the change listener
 * — necessary so a listener calling back into the model cannot deadlock on it — opens a window where a second
 * writer's whole read-mutate-notify sequence can run between the first writer's release and its own delivery,
 * so the two `appearanceChanged` events reach JavaScript out of the order the state actually transitioned in.
 * `setColorScheme` and `onPortalColorSchemeChanged` therefore also take `notificationMutex_`, a second mutex
 * held for the writer's entire read-mutate-notify sequence, acquired **before** `mutex_` and released only
 * after the listener call returns. The lock order is always `notificationMutex_` then `mutex_`, in both
 * writers, which is what makes it safe: one writer's whole sequence — state mutation and delivery — completes
 * before the next writer's can begin, so the events JavaScript sees land in the same order the state did.
 * `colorScheme()` needs no part of this; it never emits, so it only ever takes `mutex_`.
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
 * `colorSchemeFromPortalSetting`, with the fallback applied. A `SettingChanged` signal that decodes to "no
 * preference" or a reserved value is not "the portal said nothing" — it is the portal saying nothing counts as a
 * preference — so it must resolve to `kFallbackColorScheme` rather than be discarded the way a message the bus
 * could not even parse is discarded.
 */
ColorScheme resolvePortalSettingOrFallback(uint32_t portalSettingValue);

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
    /**
     * The shared shape of `setColorScheme` and `onPortalColorSchemeChanged`: take `notificationMutex_`, mutate
     * state under `mutex_` via `mutateUnderStateLock` (which returns whether the change should emit), then
     * invoke the listener after `mutex_` is released but before `notificationMutex_` is. Both callers differ
     * only in what they mutate and which precedence rule decides `shouldEmit`.
     */
    void mutateAndNotify(const std::function<bool()>& mutateUnderStateLock);

    mutable std::mutex notificationMutex_;
    mutable std::mutex mutex_;
    std::optional<ColorScheme> colorSchemeOverride_;
    ColorScheme portalColorScheme_;
    std::function<void(ColorScheme)> changeListener_;
};

} // namespace react_native_linux
