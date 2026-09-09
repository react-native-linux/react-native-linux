#include "ScrollPhysics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
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

constexpr char kSpaceKey[] = " ";

/**
 * The keys that scroll, other than the space bar: the two page keys, the two extremes, and the four arrows, in
 * the DOM names `domKeyName` produces. A table rather than a chain of comparisons because it is a table — the
 * issue asks for the key-to-step mapping to be assertable as one.
 */
struct KeyboardScrollKey {
    const char* key;
    KeyboardScrollStep step;
    bool isHorizontal;
};

constexpr std::array<KeyboardScrollKey, 8> kKeyboardScrollKeys{
    {{.key = "PageUp", .step = KeyboardScrollStep::PageBackward, .isHorizontal = false},
     {.key = "PageDown", .step = KeyboardScrollStep::PageForward, .isHorizontal = false},
     {.key = "Home", .step = KeyboardScrollStep::ToStart, .isHorizontal = false},
     {.key = "End", .step = KeyboardScrollStep::ToEnd, .isHorizontal = false},
     {.key = "ArrowUp", .step = KeyboardScrollStep::LineBackward, .isHorizontal = false},
     {.key = "ArrowDown", .step = KeyboardScrollStep::LineForward, .isHorizontal = false},
     {.key = "ArrowLeft", .step = KeyboardScrollStep::LineBackward, .isHorizontal = true},
     {.key = "ArrowRight", .step = KeyboardScrollStep::LineForward, .isHorizontal = true}}};

double usableDecelerationRate(double decelerationRate) {
    return std::clamp(decelerationRate, kFastestDecelerationRate, kSlowestDecelerationRate);
}

/**
 * The whole distance a velocity still has to cover: the integral of `velocity * rate^t` over `t` from now until
 * the velocity reaches zero.
 */
double momentumTravel(double velocity, double decelerationRate) { return velocity / -std::log(decelerationRate); }

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

/**
 * The child the offset is measured against: the first one at or after `minimumIndexForVisible` that is even
 * partly visible, and the last child when none of them is.
 */
const ScrollChildFrame* anchorChild(const std::vector<ScrollChildFrame>& children, double offset,
                                    int minimumIndexForVisible) {
    const size_t firstIndex = static_cast<size_t>(std::max(minimumIndexForVisible, 0));

    if (firstIndex >= children.size()) {
        return nullptr;
    }

    for (size_t index = firstIndex; index < children.size(); ++index) {
        if (children[index].position + children[index].length > offset) {
            return &children[index];
        }
    }

    return &children.back();
}

} // namespace

double minimumScrollOffset(const ScrollAxisBounds& bounds) { return -bounds.leadingInset; }

double maximumScrollOffset(const ScrollAxisBounds& bounds) {
    return std::max(bounds.contentLength + bounds.trailingInset - bounds.viewportLength, minimumScrollOffset(bounds));
}

double clampScrollOffset(double offset, const ScrollAxisBounds& bounds) {
    return std::clamp(offset, minimumScrollOffset(bounds), maximumScrollOffset(bounds));
}

double velocityForTravel(double distance, double decelerationRate) {
    return distance * -std::log(usableDecelerationRate(decelerationRate));
}

ScrollAxisState dragAxis(const ScrollAxisState& axis, double delta, double frameMilliseconds,
                         const ScrollAxisBounds& bounds) {
    const double moved = clampScrollOffset(axis.offset + delta, bounds);
    const bool hasElapsed = frameMilliseconds > 0.0;

    return ScrollAxisState{.offset = moved, .velocity = hasElapsed ? (moved - axis.offset) / frameMilliseconds : 0.0};
}

ScrollDestination scrollToDestination(double currentOffset, double targetOffset, bool isAnimated,
                                      double decelerationRate, const ScrollAxisBounds& bounds) {
    const double destination = clampScrollOffset(targetOffset, bounds);

    if (destination == currentOffset) {
        return ScrollDestination{};
    }

    if (!isAnimated) {
        return ScrollDestination{.offset = destination, .hasWork = true};
    }

    const double travel = destination - currentOffset;
    const double speed = velocityForTravel(std::abs(travel), decelerationRate);

    return ScrollDestination{.offset = currentOffset, .velocity = travel < 0 ? -speed : speed, .hasWork = true};
}

bool hasSnapPoints(const ScrollSnapConfiguration& snapping) {
    return snapping.isPagingEnabled || snapping.interval > 0.0 || !snapping.offsets.empty();
}

double settleTargetOffset(const ScrollAxisState& axis, double decelerationRate, const ScrollAxisBounds& bounds,
                          const ScrollSnapConfiguration& snapping) {
    const double rate = usableDecelerationRate(decelerationRate);
    const double minimumOffset = minimumScrollOffset(bounds);
    const double maximumOffset = maximumScrollOffset(bounds);
    const double landingOffset = clampScrollOffset(axis.offset + momentumTravel(axis.velocity, rate), bounds);
    const double snapFrom =
        snapping.isIntervalMomentumDisabled ? clampScrollOffset(axis.offset, bounds) : landingOffset;
    const bool isPaging = snapping.isPagingEnabled && snapping.interval <= 0.0;
    const double interval = isPaging ? bounds.viewportLength : snapping.interval;
    const ScrollSnapAlignment alignment = isPaging ? ScrollSnapAlignment::Start : snapping.alignment;
    const bool hasInterval = snapping.offsets.empty() && interval > 0.0;
    std::vector<double> points =
        hasInterval ? intervalSnapPoints(interval, alignmentShift(alignment, bounds.viewportLength, interval),
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
        points.push_back(minimumOffset);
    }

    if (snapping.snapToEnd) {
        points.push_back(maximumOffset);
    }

    const double target = chooseSnapPoint(points, snapFrom, axis.velocity, snapping.isIntervalMomentumDisabled);

    return clampScrollOffset(target, bounds);
}

double maintainedScrollOffset(double offset, const std::vector<ScrollChildFrame>& previousChildren,
                              const std::vector<ScrollChildFrame>& currentChildren,
                              const MaintainVisibleContentPosition& maintaining, const ScrollAxisBounds& bounds) {
    const ScrollChildFrame* anchor = anchorChild(previousChildren, offset, maintaining.minimumIndexForVisible);

    if (anchor == nullptr) {
        return offset;
    }

    const auto moved = std::find_if(currentChildren.begin(), currentChildren.end(),
                                    [tag = anchor->tag](const ScrollChildFrame& child) { return child.tag == tag; });

    if (moved == currentChildren.end()) {
        return offset;
    }

    const double shift = moved->position - anchor->position;

    if (std::abs(shift) <= kMinimumAnchorShift) {
        return offset;
    }

    if (maintaining.autoscrollToTopThreshold.has_value() && offset <= maintaining.autoscrollToTopThreshold.value()) {
        return 0.0;
    }

    return clampScrollOffset(offset + shift, bounds);
}

ScrollAxisState decelerateAxis(const ScrollAxisState& axis, double frameMilliseconds, double decelerationRate,
                               const ScrollAxisBounds& bounds) {
    const double rate = usableDecelerationRate(decelerationRate);
    const double remainingTravel = momentumTravel(axis.velocity, rate);
    const bool isComingToRest = std::abs(remainingTravel) < kMinimumMomentumTravel;
    const double decay = std::pow(rate, frameMilliseconds);
    const double travel = isComingToRest ? remainingTravel : axis.velocity * (decay - 1.0) / std::log(rate);
    const double target = axis.offset + travel;
    const double moved = clampScrollOffset(target, bounds);
    const bool hasStopped = isComingToRest || moved != target;

    return ScrollAxisState{.offset = moved, .velocity = hasStopped ? 0.0 : axis.velocity * decay};
}

std::optional<KeyboardScrollIntent> keyboardScrollIntent(const std::string& key, bool isShiftDown,
                                                         bool isControlAltOrMetaDown) {
    if (isControlAltOrMetaDown) {
        return std::nullopt;
    }

    if (key == kSpaceKey) {
        return KeyboardScrollIntent{.step = isShiftDown ? KeyboardScrollStep::PageBackward
                                                        : KeyboardScrollStep::PageForward};
    }

    // Shift with anything but the space bar is a selection gesture rather than a scroll — shift-arrow and
    // shift-Page Down extend a selection everywhere on the desktop — so it is left for whoever owns selection.
    if (isShiftDown) {
        return std::nullopt;
    }

    const auto named = std::find_if(kKeyboardScrollKeys.begin(), kKeyboardScrollKeys.end(),
                                    [&key](const KeyboardScrollKey& candidate) { return key == candidate.key; });

    if (named == kKeyboardScrollKeys.end()) {
        return std::nullopt;
    }

    return KeyboardScrollIntent{.step = named->step, .isHorizontal = named->isHorizontal};
}

double keyboardScrollDestination(const KeyboardScrollIntent& intent, double currentOffset,
                                 const ScrollAxisBounds& bounds) {
    if (intent.step == KeyboardScrollStep::ToStart) {
        return minimumScrollOffset(bounds);
    }

    if (intent.step == KeyboardScrollStep::ToEnd) {
        return maximumScrollOffset(bounds);
    }

    const double page = std::max(bounds.viewportLength - kKeyboardLineDistance, kKeyboardLineDistance);

    if (intent.step == KeyboardScrollStep::PageBackward) {
        return clampScrollOffset(currentOffset - page, bounds);
    }

    if (intent.step == KeyboardScrollStep::PageForward) {
        return clampScrollOffset(currentOffset + page, bounds);
    }

    const double line =
        intent.step == KeyboardScrollStep::LineBackward ? -kKeyboardLineDistance : kKeyboardLineDistance;

    return clampScrollOffset(currentOffset + line, bounds);
}

} // namespace react_native_linux
