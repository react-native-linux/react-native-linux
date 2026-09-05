#pragma once

#include "RetainedScene.h"

#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/graphics/Size.h>

#include <cstdint>

namespace react_native_linux {

/**
 * The size a `<Switch>` measures at when nothing else constrains it.
 *
 * `UISwitch` is 51x31 and `AppleSwitchShadowNode::measureContent` returns that plus two points of width, with a
 * comment saying UIKit under-reports its own borders. There is no UIKit here and nothing to under-report, so the
 * two points are not copied: this is the control React Native's own `Switch.js` lays out with
 * `alignSelf: flex-start` and no width of its own on every platform, at the size iOS draws it, and the box
 * react-native-windows reports too.
 */
constexpr facebook::react::Size kSwitchSize{.width = 51.0F, .height = 31.0F};

/**
 * How long the thumb takes to travel from one end of the track to the other. react-native-windows animates its
 * own for 167 ms and Android's `SwitchCompat` for 250 ms; this sits between them, which is long enough to be read
 * as motion and short enough that a deliberate double toggle is not queued behind it.
 */
constexpr double kSwitchToggleMilliseconds = 150.0;

/**
 * The gap between the thumb and the track on every side, which is also what makes the thumb smaller than the
 * track is tall.
 */
constexpr float kSwitchThumbInset = 2.0F;

/**
 * Where a switch's two parts are drawn inside its frame: the pill the track fills and the circle the thumb is.
 *
 * The track is the whole frame, rounded into a pill by `roundedBorderBox` — the one function every rounded box in
 * this renderer goes through, so the corner clamp of issue #99 applies here too. The radius is the control's own
 * and not the node's `borderRadius`: a switch is a pill on every platform that has one, and the authored radius
 * still cuts the node's background fill and its hit region exactly as it does on any other node.
 *
 * The thumb is the one thing that moves, and it moves along the only axis the track leaves it.
 */
struct SwitchGeometry {
    SceneRoundedBox track;
    facebook::react::Point thumbCenter;
    float thumbRadius{0.0F};
};

/**
 * The track and thumb of a switch whose thumb has travelled `thumbProgress` of the way from off to on.
 *
 * The travel is the track's width less the thumb's diameter and the inset on both sides, so progress zero puts
 * the thumb's left edge one inset inside the track and progress one puts its right edge one inset inside the
 * other end. A frame too small to hold an inset thumb collapses the radius to zero rather than inverting it,
 * which is what keeps a switch inside a `scaleY: 0.01` animation from drawing a circle turned inside out.
 */
SwitchGeometry switchGeometry(const facebook::react::Rect& frame, float thumbProgress);

/**
 * Where the thumb is one frame of `frameMilliseconds` later, given where it is now and which end it is heading
 * for.
 *
 * This is the whole of the toggle animation, and it is arithmetic on elapsed time rather than a step per frame:
 * a 120 Hz display and a 60 Hz one move the thumb the same distance in the same wall-clock interval. A progress
 * already at the end it is heading for stays there, which is what makes the advance a no-op — and therefore
 * damages nothing — for every switch nobody has touched.
 */
float advanceSwitchThumbProgress(float thumbProgress, bool isOn, double frameMilliseconds);

/**
 * The colour the track is filled with at `thumbProgress`: the off colour and the on colour mixed in the same
 * proportion the thumb has travelled, per channel including alpha.
 *
 * Mixed rather than switched at the halfway point, because a track that changed colour in one frame while the
 * thumb was still sliding is two animations of different lengths describing one gesture. The painter calls this
 * rather than computing it, so the mix is inside the coverage gate.
 */
uint32_t switchTrackColorArgb(const SceneSwitchContent& content);

} // namespace react_native_linux
