#include "ActivityIndicatorContent.h"

#include <algorithm>
#include <cmath>

namespace react_native_linux {

namespace {

constexpr float kFullTurnDegrees = 360.0F;

} // namespace

float activityIndicatorPhase(double elapsedMilliseconds) {
    return static_cast<float>(std::fmod(elapsedMilliseconds, kActivityIndicatorRevolutionMilliseconds) /
                              kActivityIndicatorRevolutionMilliseconds);
}

ActivityIndicatorGeometry activityIndicatorGeometry(const facebook::react::Rect& frame,
                                                    const SceneActivityIndicatorContent& content) {
    const float strokeWidth = content.isLarge ? kActivityIndicatorLargeStrokeWidth : kActivityIndicatorSmallStrokeWidth;
    const float diameter =
        std::max(static_cast<float>(std::min(frame.size.width, frame.size.height)) - strokeWidth, 0.0F);
    const facebook::react::Point origin{.x = frame.origin.x + ((frame.size.width - diameter) / 2),
                                        .y = frame.origin.y + ((frame.size.height - diameter) / 2)};

    return ActivityIndicatorGeometry{
        .bounds = facebook::react::Rect{.origin = origin,
                                        .size = facebook::react::Size{.width = diameter, .height = diameter}},
        .strokeWidth = strokeWidth,
        .startAngleDegrees = activityIndicatorPhase(content.elapsedMilliseconds) * kFullTurnDegrees,
        .sweepAngleDegrees = kActivityIndicatorSweepDegrees};
}

bool isActivityIndicatorVisible(const SceneActivityIndicatorContent& content) {
    return content.isAnimating || !content.hidesWhenStopped;
}

} // namespace react_native_linux
