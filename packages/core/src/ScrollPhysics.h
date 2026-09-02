#pragma once

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
 * Integrates one frame of deceleration: the exact integral of `velocity * rate^t` over the frame rather than a
 * rectangle of it, so the distance covered does not depend on how the frame time was cut up.
 *
 * Reaching either end of the content stops the momentum dead. There is no rubber band and no bounce.
 */
ScrollAxisState decelerateAxis(const ScrollAxisState& axis, double frameMilliseconds, double decelerationRate,
                               double contentLength, double viewportLength);

} // namespace react_native_linux
