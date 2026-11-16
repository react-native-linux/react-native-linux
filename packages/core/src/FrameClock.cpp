#include "FrameClock.h"

#include <algorithm>
#include <array>

namespace react_native_linux {

namespace {

// The thermal floor of #335: under a serious or critical thermal state no fallback interval is shorter than one
// 60 Hz frame, whatever the display's refresh rate would otherwise poll at.
constexpr std::chrono::nanoseconds kNominalRefreshInterval{16'666'666};
constexpr std::chrono::nanoseconds kThermalMinimumInterval = kNominalRefreshInterval;

// How many display periods a callback the last draw armed may take before the fallback draws instead (#703).
constexpr int64_t kLateCallbackPeriods = 2;

// The stretch factors over the display's own frame period, in the states the compositor is not pacing.
constexpr uint64_t kInactiveStretch = 3;
constexpr uint64_t kOccludedStretch = 8;

std::chrono::nanoseconds activityInterval(FrameClock::WindowActivity activity,
                                          std::chrono::nanoseconds refreshInterval) {
    switch (activity) { // COV_EXCL: the switch's implicit default is untakeable when every enumerator is tested.
    case FrameClock::WindowActivity::Occluded:
        return refreshInterval * kOccludedStretch;
    case FrameClock::WindowActivity::Inactive:
        return refreshInterval * kInactiveStretch;
    case FrameClock::WindowActivity::Active:
        return refreshInterval;
    }

    return refreshInterval; // COV_EXCL: the switch above covers every enumerator; this return satisfies -Wreturn-type.
}

constexpr std::array<const char*, 3> kActivityNames{"active", "inactive", "occluded"};

} // namespace

std::chrono::nanoseconds FrameClock::fallbackInterval(WindowActivity activity, ThermalState thermal,
                                                      std::chrono::nanoseconds refreshInterval) {
    const std::chrono::nanoseconds interval = activityInterval(activity, refreshInterval);

    return thermal == ThermalState::Nominal ? interval : std::max(interval, kThermalMinimumInterval);
}

FrameClock::WindowActivity FrameClock::windowActivity(const ToplevelState& state) noexcept {
    if (state.suspended) {
        return WindowActivity::Occluded;
    }

    return state.activated ? WindowActivity::Active : WindowActivity::Inactive;
}

std::string FrameClock::formatFallbackLine(WindowActivity activity, std::chrono::nanoseconds interval,
                                           std::chrono::steady_clock::time_point at) {
    return std::string("{\"fallbackNs\":") + std::to_string(interval.count()) + ",\"activity\":\"" +
           kActivityNames.at(static_cast<size_t>(activity)) + "\",\"atNs\":" +
           std::to_string(std::chrono::duration_cast<std::chrono::nanoseconds>(at.time_since_epoch()).count()) + "}";
}

std::chrono::nanoseconds FrameClock::pacingPeriod(bool hasDrawn,
                                                  std::optional<std::chrono::nanoseconds> displayPeriod) {
    if (!displayPeriod.has_value()) {
        return kNominalRefreshInterval;
    }

    return hasDrawn ? std::max(kNominalRefreshInterval, displayPeriod.value() * kLateCallbackPeriods)
                    : displayPeriod.value();
}

FrameClock::Tick FrameClock::onFrameCallback(std::chrono::steady_clock::time_point now, bool hasPendingWork) {
    if (hasPendingWork || isRetryPending_) {
        settleCallbacks_ = 0;
    } else if (settleCallbacks_ < kSettleCallbacks) {
        ++settleCallbacks_;
    } else {
        return Tick{.shouldDraw = false, .deltaMilliseconds = 0.0, .source = Source::Callback, .resumed = false};
    }

    const Tick tick = draw(now, Source::Callback);

    ++callbackTicks_;
    lastCallbackAt_ = now;

    return tick;
}

FrameClock::Tick FrameClock::onFallbackTimeout(std::chrono::steady_clock::time_point now, bool hasPendingWork) {
    if (!hasPendingWork && !isRetryPending_) {
        return Tick{.shouldDraw = false, .deltaMilliseconds = 0.0, .source = Source::Timer, .resumed = false};
    }

    const Tick tick = draw(now, Source::Timer);

    settleCallbacks_ = 0;
    ++timerTicks_;

    return tick;
}

void FrameClock::recordDrawOutcome(bool presented) noexcept { isRetryPending_ = !presented; }

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
