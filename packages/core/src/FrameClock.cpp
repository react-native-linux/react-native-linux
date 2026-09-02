#include "FrameClock.h"

namespace react_native_linux {

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
