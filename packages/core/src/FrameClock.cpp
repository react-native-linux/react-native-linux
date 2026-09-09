#include "FrameClock.h"

#include <algorithm>

namespace react_native_linux {

namespace {

// The thermal cap of #335: under a serious or critical thermal state every fallback interval is this one
// 60 Hz frame, whatever the display's refresh rate or the window's activity would otherwise stretch it to.
constexpr std::chrono::nanoseconds kThermalCappedInterval{16'666'666};

// The stretch factors over the display's own frame period, in the states the compositor is not pacing.
constexpr double kSeriousThermalRatio = 0.9;
constexpr uint64_t kInactiveStretch = 3;
constexpr uint64_t kOccludedStretch = 8;

} // namespace

FrameClock::ThermalState FrameClock::thermalStateFromCriticalRatio(double ratio) {
    if (ratio >= 1.0) {
        return ThermalState::Critical;
    }

    return ratio >= kSeriousThermalRatio ? ThermalState::Serious : ThermalState::Nominal;
}

std::chrono::nanoseconds FrameClock::fallbackInterval(WindowActivity activity, ThermalState thermal,
                                                      std::chrono::nanoseconds refreshInterval) {
    if (thermal != ThermalState::Nominal) {
        return kThermalCappedInterval;
    }

    switch (activity) { // COV_EXCL: the switch's implicit default is untakeable when every enumerator is tested.
    case WindowActivity::Occluded:
        return refreshInterval * kOccludedStretch;
    case WindowActivity::Inactive:
        return refreshInterval * kInactiveStretch;
    case WindowActivity::Active:
        return refreshInterval;
    }

    return refreshInterval; // COV_EXCL: the switch above covers every enumerator; this return satisfies -Wreturn-type.
}

FrameClock::Tick FrameClock::onFrameCallback(std::chrono::steady_clock::time_point now) {
    const Tick tick = draw(now, Source::Callback);

    ++callbackTicks_;
    lastCallbackAt_ = now;

    return tick;
}

FrameClock::Tick FrameClock::onFallbackTimeout(std::chrono::steady_clock::time_point now, bool hasPendingWork) {
    if (!hasPendingWork) {
        return Tick{.shouldDraw = false, .deltaMilliseconds = 0.0, .source = Source::Timer, .resumed = false};
    }

    const Tick tick = draw(now, Source::Timer);

    ++timerTicks_;

    return tick;
}

uint64_t FrameClock::callbackTicks() const noexcept { return callbackTicks_; }

uint64_t FrameClock::timerTicks() const noexcept { return timerTicks_; }

uint64_t FrameClock::resumeTransitions() const noexcept { return resumeTransitions_; }

std::optional<std::chrono::steady_clock::time_point> FrameClock::lastCallbackAt() const noexcept {
    return lastCallbackAt_;
}

FrameClock::Tick FrameClock::draw(std::chrono::steady_clock::time_point now, Source source) {
    const double deltaMilliseconds =
        hasLastTick_ ? std::chrono::duration<double, std::milli>(now - lastTickTime_).count() : 0.0;
    const bool resumed = source == Source::Callback && wasLastTickTimer_;

    if (resumed) {
        ++resumeTransitions_;
    }

    hasLastTick_ = true;
    lastTickTime_ = now;
    wasLastTickTimer_ = source == Source::Timer;

    return Tick{.shouldDraw = true, .deltaMilliseconds = deltaMilliseconds, .source = source, .resumed = resumed};
}

} // namespace react_native_linux
