#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace react_native_linux {

/**
 * Frame-source liveness state machine for ADR-0001 decision 3 (see #59): `wl_surface.frame` throttles redraw when
 * the compositor sends it, but withholds it entirely for an occluded or inactive-workspace window, so the fallback
 * timeout in `WaylandWindow::waitForRedraw` is the only remaining frame source there. This turns "a callback
 * fired" or "the fallback timed out" into a draw decision without ever reading a clock itself — every timestamp is
 * passed in by the caller, so the machine is deterministic and has no Wayland dependency.
 *
 * Either input draws only if the caller reports pending work (damage, active scroll physics, a pending animation
 * frame or worklet job; a JS timer is not one, because what it changes arrives as damage): an idle occluded window must
 * not spin the GPU just because nothing sends it a callback, and an idle visible window must not present an unchanged
 * picture on every callback (#703). A callback still draws for two frames after the work runs out, so every swapchain
 * image takes the last change while the window is busy instead of on the first frame that answers the next input. A
 * skipped callback is what stops the presents, because only a draw re-arms `wl_surface.frame`; the loop then keeps
 * polling on the fallback timeout, which is where the first pending work wakes it. Either way the reported delta is
 * wall-clock time since the last tick that actually drew, so an animation driven only by the fallback advances at the
 * right rate instead of stalling or jumping when it resumes. The first callback to arrive after one or more
 * fallback-driven ticks is flagged `resumed`, exactly once, so a caller that cares can tell a real resume from an
 * ordinary vsync-paced callback.
 */
class FrameClock final {
public:
    enum class Source : uint8_t { Callback, Timer };

    struct Tick {
        bool shouldDraw{false};
        double deltaMilliseconds{0.0};
        Source source{Source::Callback};
        bool resumed{false};
    };

    /**
     * The callbacks that still draw once the work runs out: enough for a triple-buffered swapchain's other two images
     * to take the last change, so the frame that answers the next input repaints only what that input changed.
     */
    static constexpr uint32_t kSettleCallbacks = 2;

    Tick onFrameCallback(std::chrono::steady_clock::time_point now, bool hasPendingWork);
    Tick onFallbackTimeout(std::chrono::steady_clock::time_point now, bool hasPendingWork);

    /**
     * How long the fallback timeout waits when the compositor is not pacing the window (#335): a deactivated or
     * occluded window stretches the interval rather than burning the GPU at full rate, and a serious or critical
     * thermal state caps the interval at one 60 Hz frame — the machine is telling us to stop chasing the display,
     * and the stretch is a saving only while the thermal state is nominal. `refreshInterval` is the display's own
     * frame period, so a 144 Hz panel stretches from 6.9 ms and a 60 Hz panel from 16.7 ms.
     */
    enum class WindowActivity : uint8_t { Active, Inactive, Occluded };
    enum class ThermalState : uint8_t { Nominal, Serious, Critical };

    [[nodiscard]] static std::chrono::nanoseconds fallbackInterval(WindowActivity activity, ThermalState thermal,
                                                                   std::chrono::nanoseconds refreshInterval);

    /**
     * The `refreshInterval` the fallback stretches over (#703), from the display period `wp_presentation` reported
     * (60 Hz until it has). After a skipped iteration no callback is armed and the fallback is how an idle window
     * polls for work, so it is the display period. After a draw a callback is armed and the fallback only covers a
     * compositor withholding it, so it is two display periods and never under 60 Hz: a callback a little late at
     * 144 Hz must not lose to an off-vsync timer draw.
     */
    [[nodiscard]] static std::chrono::nanoseconds pacingPeriod(bool hasDrawn,
                                                               std::optional<std::chrono::nanoseconds> displayPeriod);

    /**
     * The thermal state from a thermal zone's temperature as a fraction of its own critical trip temperature:
     * at or above the trip is critical, 0.9 of it is serious, anything cooler is nominal.
     */
    [[nodiscard]] static ThermalState thermalStateFromCriticalRatio(double ratio);

    uint64_t callbackTicks() const noexcept;
    uint64_t timerTicks() const noexcept;
    uint64_t resumeTransitions() const noexcept;
    std::optional<std::chrono::steady_clock::time_point> lastCallbackAt() const noexcept;

private:
    Tick draw(std::chrono::steady_clock::time_point now, Source source);

    bool hasLastTick_{false};
    std::chrono::steady_clock::time_point lastTickTime_{};
    bool wasLastTickTimer_{false};
    uint64_t callbackTicks_{0};
    uint64_t timerTicks_{0};
    uint64_t resumeTransitions_{0};
    uint32_t settleCallbacks_{kSettleCallbacks};
    std::optional<std::chrono::steady_clock::time_point> lastCallbackAt_;
};

} // namespace react_native_linux
