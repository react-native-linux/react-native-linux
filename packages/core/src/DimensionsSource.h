#pragma once

#include <mutex>
#include <optional>

namespace react_native_linux {

/**
 * One surface's metrics, in the shape `NativeDeviceInfo`'s `DisplayMetrics` declares: logical pixels, the scale
 * that maps them onto device pixels, and the user's text-size multiplier.
 */
struct DisplayMetrics {
    double width{0.0};
    double height{0.0};
    double scale{1.0};
    double fontScale{1.0};
};

/**
 * What `Dimensions.get('window')`, `Dimensions.get('screen')` and `useWindowDimensions()` answer from, for one
 * surface (#50).
 *
 * The rule this class exists to enforce is that no path through it can report `0`. React Native's macOS port
 * answers the same API from a global lookup — `[NSApp keyWindow]` — which is `nil` whenever no window has focus,
 * and reports `0x0` for it (rn-macos#2296, #2129). Wayland is stricter still: a client cannot ask for a screen
 * size at all, there is no key window, and the only authoritative extent is the last `xdg_toplevel.configure`.
 * So the value here starts at the surface's requested default, `configure` ignores any non-positive extent
 * instead of storing it, and the last good value survives everything else.
 *
 * `screen` equals `window`. Wayland exposes no screen size to a client without `wl_output`, which needs a
 * `wl_surface.enter` this platform does not track yet, and reporting a made-up number would be worse than
 * reporting the one extent that is true. See *Dimensions and TurboModules* in docs/cpp-toolchain.md.
 *
 * `scale` is always `kDefaultScale`: neither `wp_fractional_scale_v1` nor `wl_surface.preferred_buffer_scale` is
 * bound yet, so this client is told nothing about output scaling and 1 is the only honest answer. The parameter
 * exists so the day that changes is a call-site change and not a redesign.
 *
 * `takeChangeIfAny` is the coalescing half: any number of `configure` calls between two takes produce at most one
 * change, so a `useWindowDimensions` consumer cannot be re-rendered per compositor event (rn-macos#2083). A
 * `configure` that repeats the current metrics is not a change at all.
 *
 * Threading contract: `configure` and `takeChangeIfAny` run on the platform frame thread, `metrics` runs on the
 * JavaScript thread inside `getConstants`. The mutex is what makes that pair safe; there is no other
 * synchronisation between the two threads for this state.
 */
class DimensionsSource final {
public:
    static constexpr double kDefaultWidth = 800.0;
    static constexpr double kDefaultHeight = 600.0;
    static constexpr double kDefaultScale = 1.0;

    void configure(double width, double height, double scale);
    DisplayMetrics metrics() const;
    std::optional<DisplayMetrics> takeChangeIfAny();

private:
    mutable std::mutex mutex_;
    DisplayMetrics metrics_{.width = kDefaultWidth, .height = kDefaultHeight, .scale = kDefaultScale};
    bool hasPendingChange_{false};
};

} // namespace react_native_linux
