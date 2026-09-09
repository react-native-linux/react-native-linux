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
 * A callback always draws. A fallback timeout draws only if the caller reports pending work (damage, active scroll
 * physics, or a due JS timer): an idle occluded window must not spin the GPU every 50 ms just because nothing sends
 * it a callback. Either way the reported delta is wall-clock time since the last tick that actually drew, so an
 * animation driven only by the fallback advances at the right rate instead of stalling or jumping when it resumes.
 * The first callback to arrive after one or more fallback-driven ticks is flagged `resumed`, exactly once, so a
 * caller that cares can tell a real resume from an ordinary vsync-paced callback.
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

    Tick onFrameCallback(std::chrono::steady_clock::time_point now);
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
    std::optional<std::chrono::steady_clock::time_point> lastCallbackAt_;
};

} // namespace react_native_linux
