#include "ScrollPhysics.h"

#include <algorithm>
#include <cmath>

namespace react_native_linux {

namespace {

/**
 * React Native accepts `decelerationRate` values this curve has no finite answer for: `0` means "stop the moment
 * the finger lifts" and `1` means "never stop", and both the momentum integral and the wheel impulse divide by
 * `log(rate)`. The rate is therefore confined to the range where that division is defined.
 *
 * `0.5` per millisecond has decayed to 1.5e-5 of its velocity inside one 60 Hz frame, which is the "stop at once"
 * end of the range; `0.9999` takes about forty seconds to come to rest, which is the other.
 */
constexpr double kFastestDecelerationRate = 0.5;
constexpr double kSlowestDecelerationRate = 0.9999;

double usableDecelerationRate(double decelerationRate) {
    return std::clamp(decelerationRate, kFastestDecelerationRate, kSlowestDecelerationRate);
}

/**
 * The whole distance a velocity still has to cover: the integral of `velocity * rate^t` over `t` from now until
 * the velocity reaches zero.
 */
double momentumTravel(double velocity, double decelerationRate) {
    return velocity / -std::log(decelerationRate);
}

} // namespace

double maximumScrollOffset(double contentLength, double viewportLength) {
    return std::max(contentLength - viewportLength, 0.0);
}

double clampScrollOffset(double offset, double contentLength, double viewportLength) {
    return std::clamp(offset, 0.0, maximumScrollOffset(contentLength, viewportLength));
}

double velocityForTravel(double distance, double decelerationRate) {
    return distance * -std::log(usableDecelerationRate(decelerationRate));
}

ScrollAxisState dragAxis(const ScrollAxisState& axis, double delta, double frameMilliseconds, double contentLength,
                         double viewportLength) {
    const double moved = clampScrollOffset(axis.offset + delta, contentLength, viewportLength);
    const bool hasElapsed = frameMilliseconds > 0.0;

    return ScrollAxisState{.offset = moved,
                           .velocity = hasElapsed ? (moved - axis.offset) / frameMilliseconds : 0.0};
}

ScrollDestination scrollToDestination(double currentOffset, double targetOffset, bool isAnimated,
                                      double decelerationRate, double contentLength, double viewportLength) {
    const double destination = clampScrollOffset(targetOffset, contentLength, viewportLength);

    if (destination == currentOffset) {
        return ScrollDestination{};
    }

    if (!isAnimated) {
        return ScrollDestination{.offset = destination, .hasWork = true};
    }

    const double travel = destination - currentOffset;
    const double speed = velocityForTravel(std::abs(travel), decelerationRate);

    return ScrollDestination{.offset = currentOffset,
                             .velocity = travel < 0 ? -speed : speed,
                             .hasWork = true};
}

ScrollAxisState decelerateAxis(const ScrollAxisState& axis, double frameMilliseconds, double decelerationRate,
                               double contentLength, double viewportLength) {
    const double rate = usableDecelerationRate(decelerationRate);
    const double remainingTravel = momentumTravel(axis.velocity, rate);
    const bool isComingToRest = std::abs(remainingTravel) < kMinimumMomentumTravel;
    const double decay = std::pow(rate, frameMilliseconds);
    const double travel = isComingToRest ? remainingTravel : axis.velocity * (decay - 1.0) / std::log(rate);
    const double target = axis.offset + travel;
    const double moved = clampScrollOffset(target, contentLength, viewportLength);
    const bool hasStopped = isComingToRest || moved != target;

    return ScrollAxisState{.offset = moved, .velocity = hasStopped ? 0.0 : axis.velocity * decay};
}

} // namespace react_native_linux
