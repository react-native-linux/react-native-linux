#pragma once

#include "RetainedScene.h"

#include <cstdint>

#include <react/renderer/graphics/Rect.h>

namespace react_native_linux {

/**
 * How long one revolution of the spinner takes. A second is what `UIActivityIndicatorView` and Material's
 * indeterminate circular progress both land within a few per cent of, and a round number is what makes a golden
 * rendered at a named frame count describe an angle a reader can check by hand.
 */
constexpr double kActivityIndicatorRevolutionMilliseconds = 1000.0;

/**
 * How much of the ring the arc covers. Three quarters leaves a gap wide enough that the rotation is legible at
 * twenty points across, which is the whole reason an indeterminate spinner is an arc and not a ring.
 */
constexpr float kActivityIndicatorSweepDegrees = 270.0F;

/**
 * The stroke widths `size="small"` and `size="large"` draw with.
 *
 * The prop is what picks the width, not the frame: React Native's own `ActivityIndicator.js` turns `size` into a
 * 20-point or 36-point style box, so a frame-proportional stroke would make the prop indistinguishable from the
 * style it produced — and would then thin out to nothing on a box an app sized itself. `UIActivityIndicatorView`
 * keeps its line width per style for the same reason.
 */
constexpr float kActivityIndicatorSmallStrokeWidth = 2.0F;
constexpr float kActivityIndicatorLargeStrokeWidth = 3.0F;

/**
 * The colour an indicator draws with when the app named none. React Native's own `ActivityIndicator.js` defaults
 * to this grey on iOS and to null on every other platform, where the platform's own control picks its accent;
 * this platform has no theme service to ask yet — the same deferral the focus ring's colour carries — so it
 * takes the one default upstream actually writes down.
 */
constexpr uint32_t kActivityIndicatorDefaultColorArgb = 0xFF999999U;

/**
 * Where the arc is stroked and how far around it goes.
 *
 * `bounds` is the circle's bounding box: the largest centred square the frame holds, inset by half the stroke so
 * a stroke centred on the path stays inside the frame the scene damages for this node. `startAngleDegrees` is
 * measured the way Skia measures it — zero at three o'clock, growing clockwise — with the phase turned into a
 * whole revolution, so the gap in the ring travels once around per `kActivityIndicatorRevolutionMilliseconds`.
 */
struct ActivityIndicatorGeometry {
    facebook::react::Rect bounds;
    float strokeWidth{0.0F};
    float startAngleDegrees{0.0F};
    float sweepAngleDegrees{kActivityIndicatorSweepDegrees};
};

/**
 * How far into its current revolution an indicator is, as a fraction of one, `elapsedMilliseconds` after it
 * started spinning.
 *
 * Arithmetic on elapsed time rather than a counter of frames, which is the rule `animatedImageFrameIndex`
 * already sets: a 120 Hz display asks twice as often as a 60 Hz one and gets the same angle at the same instant.
 */
float activityIndicatorPhase(double elapsedMilliseconds);

/**
 * The arc one indicator draws this frame. A frame with no room for a stroked circle produces an empty box rather
 * than a negative one, so a collapsed indicator draws nothing instead of drawing inside out.
 */
ActivityIndicatorGeometry activityIndicatorGeometry(const facebook::react::Rect& frame,
                                                    const SceneActivityIndicatorContent& content);

/**
 * Whether an indicator draws at all: a stopped one is invisible when `hidesWhenStopped` is set and holds its
 * last angle when it is not, which is `UIActivityIndicatorView`'s rule and the one `ActivityIndicator.js`
 * documents.
 */
bool isActivityIndicatorVisible(const SceneActivityIndicatorContent& content);

} // namespace react_native_linux
