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

/**
 * Every snap point a landing point could possibly be answered with: the two ends of the range and the three
 * around the landing itself. An interval describes unboundedly many points and no more than these are ever
 * consulted, so the list is built by arithmetic rather than enumerated — a one-point interval costs what a page
 * costs. Repeats are left in it because a selection over the list cannot tell them apart.
 */
std::vector<double> intervalSnapPoints(double interval, double shift, double maximumOffset, double landingOffset) {
    const double firstIndex = std::ceil(shift / interval);
    const double lastIndex = std::floor((maximumOffset + shift) / interval);

    if (firstIndex > lastIndex) {
        return {};
    }

    const double landingIndex = std::clamp(std::round((landingOffset + shift) / interval), firstIndex, lastIndex);
    std::vector<double> points;

    for (const double index : {firstIndex, landingIndex - 1.0, landingIndex, landingIndex + 1.0, lastIndex}) {
        points.push_back(std::clamp(index, firstIndex, lastIndex) * interval - shift);
    }

    return points;
}

std::vector<double> offsetSnapPoints(const std::vector<double>& offsets, double maximumOffset) {
    std::vector<double> points;

    for (const double candidate : offsets) {
        if (candidate >= 0.0 && candidate <= maximumOffset) {
            points.push_back(candidate);
        }
    }

    return points;
}

/**
 * Which candidate a flick settles on.
 *
 * The nearest one, unless the choice is directional — `disableIntervalMomentum` — in which case it is the nearest
 * one strictly beyond the release in the direction the flick is going. That is
 * `ReactScrollView.flingAndSnap`'s larger-offset/smaller-offset rule, and it is what stops a release that is
 * already sitting on a snap point from travelling nowhere at all. A release with no velocity has no direction and
 * takes the nearest, and so does a directional choice with nothing left ahead of it.
 */
double chooseSnapPoint(const std::vector<double>& points, double from, double velocity, bool isDirectional) {
    const bool isDirectionalChoice = isDirectional && velocity != 0.0;
    double nearest = points.front();
    double ahead = points.front();
    bool hasAhead = false;

    for (const double point : points) {
        if (std::abs(point - from) < std::abs(nearest - from)) {
            nearest = point;
        }

        if (!isDirectionalChoice || (velocity > 0.0 ? point <= from : point >= from)) {
            continue;
        }

        if (!hasAhead || std::abs(point - from) < std::abs(ahead - from)) {
            ahead = point;
            hasAhead = true;
        }
    }

    return hasAhead ? ahead : nearest;
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
    const bool isPaging = snapping.isPagingEnabled && snapping.interval <= 0.0;
    const double interval = isPaging ? viewportLength : snapping.interval;
    const ScrollSnapAlignment alignment = isPaging ? ScrollSnapAlignment::Start : snapping.alignment;
    const bool hasInterval = snapping.offsets.empty() && interval > 0.0;
    std::vector<double> points =
        hasInterval ? intervalSnapPoints(interval, alignmentShift(alignment, viewportLength, interval),
                                         maximumOffset, snapFrom)
                    : offsetSnapPoints(snapping.offsets, maximumOffset);

    if (points.empty()) {
        return landingOffset;
    }

    const auto [smallest, largest] = std::minmax_element(points.begin(), points.end());
    const bool isBeforeEverySnapPoint = !snapping.snapToStart && snapFrom < *smallest;
    const bool isPastEverySnapPoint = !snapping.snapToEnd && snapFrom > *largest;

    if (isBeforeEverySnapPoint || isPastEverySnapPoint) {
        return landingOffset;
    }

    if (snapping.snapToStart) {
        points.push_back(0.0);
    }

    if (snapping.snapToEnd) {
        points.push_back(maximumOffset);
    }

    const double target =
        chooseSnapPoint(points, snapFrom, axis.velocity, snapping.isIntervalMomentumDisabled);

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
