#pragma once

#include <optional>
#include <vector>

namespace react_native_linux {

/**
 * `UIScrollView.decelerationRate`'s two named values. React Native's `decelerationRate` prop resolves `"normal"`
 * and `"fast"` to exactly these numbers and passes them through to `BaseScrollViewProps::decelerationRate`
 * unchanged, so they are the constants rather than an approximation of them.
 *
 * The number is a **per-millisecond** decay factor: momentum moving at `v` points per millisecond is moving at
 * `v * rate^t` after `t` milliseconds. Nothing here is expressed per frame, which is what makes a 120 Hz window
 * and a 60 Hz one travel the same distance for the same flick; at 60 Hz the same constants work out as
 * `0.998^16.667 = 0.9672` and `0.99^16.667 = 0.8457` per frame.
 */
inline constexpr double kDecelerationRateNormal = 0.998;
inline constexpr double kDecelerationRateFast = 0.99;

/**
 * How far one wheel notch scrolls. The notch is turned into the velocity whose deceleration curve covers exactly
 * this distance, so a wheel and a touchpad fling share one curve instead of the wheel jumping and the touchpad
 * gliding.
 */
inline constexpr double kWheelNotchDistance = 40.0;

/**
 * Momentum ends once the distance it still has to travel is under half a point. That remainder is applied in the
 * same step rather than dropped, so a fling always covers the full analytic distance of its velocity and this
 * threshold buys frames without costing position.
 */
inline constexpr double kMinimumMomentumTravel = 0.5;

/**
 * One axis of one `<ScrollView>`: where its content sits, and how fast that is still changing.
 *
 * `offset` is the matching component of React Native's `contentOffset`, in points, and grows as the content
 * scrolls up or left. `velocity` is points per millisecond in the same direction.
 */
struct ScrollAxisState {
    double offset{0.0};
    double velocity{0.0};
};

/**
 * The largest `contentOffset` that still leaves content in the viewport, which is zero when the content fits.
 */
double maximumScrollOffset(double contentLength, double viewportLength);

/**
 * `offset` confined to `[0, maximumScrollOffset(...)]`. Overscroll is not modelled at all; see the rubber-band
 * deferral in *ScrollView* in docs/cpp-toolchain.md.
 */
double clampScrollOffset(double offset, double contentLength, double viewportLength);

/**
 * The velocity whose deceleration curve travels exactly `distance` before coming to rest, which is what turns a
 * discrete wheel notch into a smooth scroll on the same curve a fling uses.
 */
double velocityForTravel(double distance, double decelerationRate);

/**
 * Applies a directly tracked delta — a touchpad finger, which moves the content one-to-one — and reports the
 * velocity that delta implies over the frame it arrived in. That velocity is what the fling starts from when the
 * finger lifts.
 */
ScrollAxisState dragAxis(const ScrollAxisState& axis, double delta, double frameMilliseconds, double contentLength,
                         double viewportLength);

/**
 * What a programmatic `scrollTo` turns into: where the content should end up, how fast it should be moving to get
 * there, and whether there is anything to do at all.
 *
 * `hasWork` false is the rule [core#34327](https://github.com/facebook/react-native/issues/34327) landed as: a
 * `scrollTo` to the offset the content is already at is **not** a scroll, so it must not emit `onScroll` and must
 * not open a momentum bracket. A library built on that behaviour looped forever on iOS Fabric when it changed.
 * Nothing here needs a special case for it — no work means no movement, and the cadence emits on movement.
 *
 * The destination is clamped **before** it is returned, which is the other half:
 * [core#41034](https://github.com/facebook/react-native/issues/41034) is a fast scroll to the top reporting
 * negative offsets to JavaScript because the clamp came after the event.
 *
 * An animated scroll reuses the deceleration curve rather than introducing a second motion model: the velocity
 * whose curve travels exactly the remaining distance is what `velocityForTravel` already answers for a wheel
 * notch, so an animated `scrollTo` is a flick aimed at a known destination and its bracket is the ordinary
 * momentum bracket.
 */
struct ScrollDestination {
    double offset{0.0};
    double velocity{0.0};
    bool hasWork{false};
};

ScrollDestination scrollToDestination(double currentOffset, double targetOffset, bool isAnimated,
                                      double decelerationRate, double contentLength, double viewportLength);

/**
 * `snapToAlignment`'s three values as arithmetic rather than as `ScrollViewSnapToAlignment`: which edge of a
 * snapped item the viewport lines up with. Keeping the React enum out of this header is what keeps the file free
 * of React types; `ScrollController` translates.
 */
enum class ScrollSnapAlignment { Start, Center, End };

/**
 * The snap half of `<ScrollView>`'s props, for one axis. Upstream treats these as alternatives rather than as a
 * set, and so does this: `snapToOffsets` wins over `snapToInterval`, which in turn wins over `isPagingEnabled`.
 * That is the precedence `RCTScrollView` resolves them in and the one the prop documentation states —
 * `snapToInterval` is "a more configurable alternative to `pagingEnabled`", not something layered on top of it.
 *
 * `isPagingEnabled` alone is an interval of exactly one viewport, `Start`-aligned, which is what a `UIScrollView`
 * page is and what Android implements the prop as.
 */
struct ScrollSnapConfiguration {
    double interval{0.0};
    std::vector<double> offsets{};
    ScrollSnapAlignment alignment{ScrollSnapAlignment::Start};
    bool snapToStart{true};
    bool snapToEnd{true};
    bool isPagingEnabled{false};
    bool isIntervalMomentumDisabled{false};
};

/**
 * Whether this configuration describes any snap point at all, which is what decides if a glide is re-aimed or
 * left on the curve its velocity already describes.
 */
bool hasSnapPoints(const ScrollSnapConfiguration& snapping);

/**
 * Where a flick comes to rest: the analytic landing point of `axis`, projected onto the nearest snap point.
 *
 * This is the whole of `snapToInterval`, `snapToOffsets`, `snapToAlignment`, `snapToStart`, `snapToEnd`,
 * `pagingEnabled` and `disableIntervalMomentum`, and it is a pure function of numbers so that
 * [core#48393](https://github.com/facebook/react-native/issues/48393) — snapping doing nothing when the viewport
 * width is fractional — is a table row rather than a rendering bug. Nothing here compares a float for equality
 * and no candidate list is enumerated, so a fractional viewport, a fractional interval and a fractional scale are
 * the same arithmetic as an integral one.
 *
 * The rules, in the order they apply:
 *
 * - The landing point is `offset` plus the whole remaining travel of `velocity`, clamped to the scrollable range
 *   before anything is projected onto it — the clamp is part of the target, not applied to the event afterwards.
 * - `isIntervalMomentumDisabled` replaces that landing point with the current offset and makes the choice
 *   directional: the nearest snap point strictly *ahead* of the release in the direction the flick is going,
 *   which is `ReactScrollView.flingAndSnap`'s larger-offset/smaller-offset rule. A release already sitting on a
 *   snap point therefore advances to the next one rather than travelling nowhere, and a release with no velocity
 *   has no direction and takes the nearest.
 * - Snap points are `snapping.offsets` when there are any, otherwise multiples of the interval — `snapToInterval`
 *   if it is positive, one viewport if only `isPagingEnabled` is — shifted by the
 *   alignment: `Start` puts an item's leading edge at the viewport's, `Center` centres it, `End` aligns trailing
 *   edges. Points outside `[0, maximumScrollOffset]` are not snap points.
 * - The two ends of the content are snap points as well, but only when `snapToStart` and `snapToEnd` say so. When
 *   one of them is false and the landing point is past the outermost configured snap point on that side, nothing
 *   snaps at all and the content settles where momentum put it — which is the "scroll freely between the start
 *   and the first offset" the props are documented as.
 * - With no snap point anywhere, the target is the landing point, so this function is also the plain answer to
 *   where an unsnapped flick stops.
 */
double settleTargetOffset(const ScrollAxisState& axis, double decelerationRate, double contentLength,
                          double viewportLength, const ScrollSnapConfiguration& snapping);

/**
 * One child of a `<ScrollView>`'s content view along one axis: which node it is, where it starts and how long it
 * is. `tag` is a `facebook::react::Tag`, kept as an `int` for the same reason `ScrollSnapAlignment` is not
 * `ScrollViewSnapToAlignment`: this header describes arithmetic and holds no React types.
 */
struct ScrollChildFrame {
    int tag{0};
    double position{0.0};
    double length{0.0};
};

/**
 * `maintainVisibleContentPosition`, which upstream parses onto `BaseScrollViewProps` as
 * `ScrollViewMaintainVisibleContentPosition` and every platform then implements for itself.
 */
struct MaintainVisibleContentPosition {
    int minimumIndexForVisible{0};
    std::optional<double> autoscrollToTopThreshold{};
};

/**
 * Half a point: the shift of an anchor below which nothing is adjusted, because a content change that moved the
 * anchor by less than that cannot be seen and an adjustment for it would be an `onScroll` nobody asked for. It is
 * the threshold `RCTScrollViewComponentView::_adjustForMaintainVisibleContentPosition` uses, and it is what keeps
 * this function off a float equality.
 */
inline constexpr double kMinimumAnchorShift = 0.5;

/**
 * Where the content has to sit after a commit changed the children, so that the child the user is looking at
 * stays where it was on screen. This is the whole of `maintainVisibleContentPosition`.
 *
 * The anchor is chosen from the children **as they were before the commit**: the first one at or after
 * `minimumIndexForVisible` whose trailing edge is past `offset` — the first one any part of which is visible —
 * and the last child when none of them is. That is
 * `RCTScrollViewComponentView::_prepareForMaintainVisibleScrollPosition`'s rule, and Android's
 * `MaintainVisibleScrollPositionHelper` picks the same child.
 *
 * The anchor is then found again in the children as they are *now*, **by tag rather than by index**, which is the
 * point of the whole exercise: a prepend renumbers every index and renames nothing. What the anchor moved by is
 * what the offset moves by, so the shift cancels and the anchor is painted exactly where it was.
 *
 * Two things stop it:
 *
 * - **An anchor that is not in the new children is not an anchor.** The child the offset was measured against was
 *   unmounted by the same commit, so there is nothing left to hold still and the offset stays where it is rather
 *   than being adjusted by a delta computed against a different node —
 *   [core#42905](https://github.com/facebook/react-native/issues/42905) is that adjustment applied to a header
 *   that was being unmounted, leaving white space behind it.
 * - **`autoscrollToTopThreshold` overrides the adjustment entirely.** A user already within that many points of
 *   the top is reading the top rather than a particular child, so a prepend takes them to the *new* top instead of
 *   pinning what used to be there. The offset it tests is the one before the adjustment, which is what upstream
 *   compares, and the threshold is only consulted once an adjustment is actually called for.
 *
 * The result is clamped against the content the commit produced, for the same reason `scrollToDestination` clamps
 * before it returns: the clamp is part of the offset, not something applied to the event afterwards.
 */
double maintainedScrollOffset(double offset, const std::vector<ScrollChildFrame>& previousChildren,
                              const std::vector<ScrollChildFrame>& currentChildren,
                              const MaintainVisibleContentPosition& maintaining, double contentLength,
                              double viewportLength);

/**
 * Integrates one frame of deceleration: the exact integral of `velocity * rate^t` over the frame rather than a
 * rectangle of it, so the distance covered does not depend on how the frame time was cut up.
 *
 * Reaching either end of the content stops the momentum dead. There is no rubber band and no bounce.
 */
ScrollAxisState decelerateAxis(const ScrollAxisState& axis, double frameMilliseconds, double decelerationRate,
                               double contentLength, double viewportLength);

} // namespace react_native_linux
