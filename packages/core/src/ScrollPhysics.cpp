#include "ScrollPhysics.h"

#include <algorithm>
#include <cmath>
#include <vector>

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

/**
 * The two outermost snap points of a configuration and the one nearest a given offset. Three numbers rather than
 * a list because an interval describes infinitely many candidates and only these three are ever consulted.
 */
struct SnapPointRange {
    double smallest{0.0};
    double nearest{0.0};
    double largest{0.0};
    bool hasAny{false};
};

/**
 * How far a snapped item's offset sits behind the multiple of the interval that names it, which is the whole of
 * `snapToAlignment`: an item `interval` long inside a viewport `viewportLength` long is centred by half the
 * difference between them and trailing-aligned by all of it.
 */
double alignmentShift(ScrollSnapAlignment alignment, double viewportLength, double interval) {
    if (alignment == ScrollSnapAlignment::Center) {
        return (viewportLength - interval) / 2.0;
    }

    if (alignment == ScrollSnapAlignment::End) {
        return viewportLength - interval;
    }

    return 0.0;
}

SnapPointRange intervalSnapPoints(double interval, double shift, double maximumOffset, double landingOffset) {
    const double firstIndex = std::ceil(shift / interval);
    const double lastIndex = std::floor((maximumOffset + shift) / interval);

    if (firstIndex > lastIndex) {
        return SnapPointRange{};
    }

    const double nearestIndex = std::clamp(std::round((landingOffset + shift) / interval), firstIndex, lastIndex);

    return SnapPointRange{.smallest = firstIndex * interval - shift,
                          .nearest = nearestIndex * interval - shift,
                          .largest = lastIndex * interval - shift,
                          .hasAny = true};
}

SnapPointRange offsetSnapPoints(const std::vector<double>& offsets, double maximumOffset, double landingOffset) {
    SnapPointRange range{};

    for (const double candidate : offsets) {
        if (candidate < 0.0 || candidate > maximumOffset) {
            continue;
        }

        if (!range.hasAny) {
            range = SnapPointRange{
                .smallest = candidate, .nearest = candidate, .largest = candidate, .hasAny = true};

            continue;
        }

        range.smallest = std::min(range.smallest, candidate);
        range.largest = std::max(range.largest, candidate);

        if (std::abs(candidate - landingOffset) < std::abs(range.nearest - landingOffset)) {
            range.nearest = candidate;
        }
    }

    return range;
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

bool hasSnapPoints(const ScrollSnapConfiguration& snapping) {
    return snapping.isPagingEnabled || snapping.interval > 0.0 || !snapping.offsets.empty();
}

double settleTargetOffset(const ScrollAxisState& axis, double decelerationRate, double contentLength,
                          double viewportLength, const ScrollSnapConfiguration& snapping) {
    const double rate = usableDecelerationRate(decelerationRate);
    const double maximumOffset = maximumScrollOffset(contentLength, viewportLength);
    const double landingOffset =
        clampScrollOffset(axis.offset + momentumTravel(axis.velocity, rate), contentLength, viewportLength);
    const double snapFrom = snapping.isIntervalMomentumDisabled
                                ? clampScrollOffset(axis.offset, contentLength, viewportLength)
                                : landingOffset;
    const double interval = snapping.isPagingEnabled ? viewportLength : snapping.interval;
    const ScrollSnapAlignment alignment =
        snapping.isPagingEnabled ? ScrollSnapAlignment::Start : snapping.alignment;
    const bool hasInterval = snapping.offsets.empty() && interval > 0.0;
    const SnapPointRange snapPoints =
        hasInterval ? intervalSnapPoints(interval, alignmentShift(alignment, viewportLength, interval),
                                         maximumOffset, snapFrom)
                    : offsetSnapPoints(snapping.offsets, maximumOffset, snapFrom);
    const bool isBeforeEverySnapPoint = !snapping.snapToStart && snapFrom < snapPoints.smallest;
    const bool isPastEverySnapPoint = !snapping.snapToEnd && snapFrom > snapPoints.largest;

    if (!snapPoints.hasAny || isBeforeEverySnapPoint || isPastEverySnapPoint) {
        return landingOffset;
    }

    double target = snapPoints.nearest;

    if (snapping.snapToStart && std::abs(snapFrom) < std::abs(snapFrom - target)) {
        target = 0.0;
    }

    if (snapping.snapToEnd && std::abs(snapFrom - maximumOffset) < std::abs(snapFrom - target)) {
        target = maximumOffset;
    }

    return clampScrollOffset(target, contentLength, viewportLength);
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
