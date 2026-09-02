#include "GradientShader.h"

#include "include/core/SkColor.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPoint.h"
#include "include/core/SkTileMode.h"
#include "include/effects/SkGradient.h"

#include <react/renderer/graphics/ColorStop.h>
#include <react/renderer/graphics/Float.h>
#include <react/renderer/graphics/LinearGradient.h>
#include <react/renderer/graphics/RadialGradient.h>
#include <react/renderer/graphics/ValueUnit.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <optional>
#include <variant>
#include <vector>

namespace react_native_linux {

namespace {

constexpr float kQuarterTurnDegrees = 90.0F;
constexpr float kHalfTurnDegrees = 180.0F;
constexpr float kThreeQuarterTurnDegrees = 270.0F;
constexpr float kFullTurnDegrees = 360.0F;
// A radial gradient of zero radius is a division by zero inside Skia, and CSS says such a gradient paints the
// last stop's colour everywhere. The smallest positive radius does exactly that.
constexpr float kSmallestRadius = 0.00001F;
// The reference length that makes ValueUnit::resolve turn a percentage into the 0..1 fraction Skia wants.
constexpr float kFractionReference = 1.0F;

struct GradientRamp {
    std::vector<SkColor4f> colors;
    std::vector<float> positions;
};

struct GradientLine {
    SkPoint start{};
    SkPoint end{};
};

struct GradientRadii {
    float horizontal{0.0F};
    float vertical{0.0F};
};

float toDegrees(float radians) {
    return radians * kHalfTurnDegrees / std::numbers::pi_v<float>;
}

float toRadians(float degrees) {
    return degrees * std::numbers::pi_v<float> / kHalfTurnDegrees;
}

/**
 * Where one authored colour stop sits on the gradient line, as a fraction of it. A percentage is that fraction
 * directly; a length is a distance along a line whose length only the caller knows; an unset position is left for
 * the fix-up below to distribute.
 */
std::optional<float> resolveStopPosition(const facebook::react::ValueUnit& position, float gradientLineLength) {
    if (position.unit == facebook::react::UnitType::Percent) {
        return position.resolve(kFractionReference);
    }

    if (position.unit == facebook::react::UnitType::Point) {
        return gradientLineLength > 0 ? position.value / gradientLineLength : 0.0F;
    }

    return std::nullopt;
}

/**
 * The CSS colour-stop fix-up: https://drafts.csswg.org/css-images-4/#coloring-gradient-line. An unpositioned first
 * stop sits at 0 and an unpositioned last stop at 1, a position never moves backwards past the largest one before
 * it, and each run of unpositioned stops is spread evenly between the two positioned stops around it.
 */
std::vector<facebook::react::ProcessedColorStop> fixedColorStops(
    const std::vector<facebook::react::ColorStop>& colorStops, float gradientLineLength) {
    std::vector<facebook::react::ProcessedColorStop> fixed(colorStops.size());
    float largestPositionSoFar = resolveStopPosition(colorStops.front().position, gradientLineLength).value_or(0.0F);

    for (size_t index = 0; index < colorStops.size(); index++) {
        std::optional<float> position = resolveStopPosition(colorStops[index].position, gradientLineLength);

        if (!position.has_value() && index == 0) {
            position = 0.0F;
        }

        if (!position.has_value() && index + 1 == colorStops.size()) {
            position = 1.0F;
        }

        if (position.has_value()) {
            largestPositionSoFar = std::max(position.value(), largestPositionSoFar);
            fixed[index] = facebook::react::ProcessedColorStop{.color = colorStops[index].color,
                                                              .position = largestPositionSoFar};
        }
    }

    size_t lastPositionedIndex = 0;

    for (size_t index = 1; index < fixed.size(); index++) {
        if (!fixed[index].position.has_value()) {
            continue;
        }

        const size_t unpositionedCount = index - lastPositionedIndex - 1;
        const float startPosition = fixed[lastPositionedIndex].position.value();
        const float increment =
            (fixed[index].position.value() - startPosition) / static_cast<float>(unpositionedCount + 1);

        for (size_t offset = 1; offset <= unpositionedCount; offset++) {
            fixed[lastPositionedIndex + offset] = facebook::react::ProcessedColorStop{
                .color = colorStops[lastPositionedIndex + offset].color,
                .position = startPosition + (increment * static_cast<float>(offset))};
        }

        lastPositionedIndex = index;
    }

    return fixed;
}

GradientRamp toGradientRamp(const std::vector<facebook::react::ColorStop>& colorStops, float gradientLineLength) {
    GradientRamp ramp;

    for (const facebook::react::ProcessedColorStop& stop : fixedColorStops(colorStops, gradientLineLength)) {
        if (!stop.position.has_value()) {
            continue;
        }

        // React Native's Color is packed ARGB on this platform, which is SkColor's own layout.
        ramp.colors.push_back(SkColor4f::FromColor(static_cast<SkColor>(static_cast<uint32_t>(*stop.color))));
        ramp.positions.push_back(stop.position.value());
    }

    return ramp;
}

SkGradient toSkGradient(const GradientRamp& ramp) {
    return SkGradient{SkGradient::Colors{ramp.colors, ramp.positions, SkTileMode::kClamp},
                      SkGradient::Interpolation{}};
}

/**
 * The angle a corner keyword means: the direction perpendicular to the box's other diagonal, so the two corners
 * beside the named one land on the gradient line's endpoints.
 * https://www.w3.org/TR/css-images-3/#linear-gradient-syntax, "using keywords".
 */
float angleForKeyword(facebook::react::GradientKeyword keyword, float width, float height) {
    if (keyword == facebook::react::GradientKeyword::ToTopRight) {
        return kQuarterTurnDegrees - toDegrees(std::atan(width / height));
    }

    if (keyword == facebook::react::GradientKeyword::ToBottomRight) {
        return kQuarterTurnDegrees + toDegrees(std::atan(width / height));
    }

    if (keyword == facebook::react::GradientKeyword::ToTopLeft) {
        return kThreeQuarterTurnDegrees + toDegrees(std::atan(width / height));
    }

    return kHalfTurnDegrees + toDegrees(std::atan(height / width));
}

/**
 * The CSS gradient line for an angle, in the box's own coordinates: centred on the box, pointing at the angle,
 * and long enough that the perpendicular through each endpoint touches the corner farthest along it.
 *
 * The construction is Blink's, by way of React Native's Android `LinearGradient.endPointsFromAngle`: intersect
 * the gradient line with the perpendicular through the corner the angle points at, and mirror that offset about
 * the centre for the other end. The four axis-aligned angles are returned directly because the perpendicular
 * slope is infinite there.
 */
GradientLine gradientLine(float angleDegrees, float width, float height) {
    float angle = std::fmod(angleDegrees, kFullTurnDegrees);

    if (angle < 0) {
        angle += kFullTurnDegrees;
    }

    if (angle == 0.0F) {
        return GradientLine{.start = SkPoint{0, height}, .end = SkPoint{0, 0}};
    }

    if (angle == kQuarterTurnDegrees) {
        return GradientLine{.start = SkPoint{0, 0}, .end = SkPoint{width, 0}};
    }

    if (angle == kHalfTurnDegrees) {
        return GradientLine{.start = SkPoint{0, 0}, .end = SkPoint{0, height}};
    }

    if (angle == kThreeQuarterTurnDegrees) {
        return GradientLine{.start = SkPoint{width, 0}, .end = SkPoint{0, 0}};
    }

    const float slope = std::tan(toRadians(kQuarterTurnDegrees - angle));
    const float perpendicularSlope = -1.0F / slope;
    const float halfWidth = width / 2;
    const float halfHeight = height / 2;
    const float cornerX = angle < kHalfTurnDegrees ? halfWidth : -halfWidth;
    const float cornerY =
        (angle < kQuarterTurnDegrees || angle >= kThreeQuarterTurnDegrees) ? halfHeight : -halfHeight;
    const float intercept = cornerY - (perpendicularSlope * cornerX);
    const float endX = intercept / (slope - perpendicularSlope);
    const float endY = (perpendicularSlope * endX) + intercept;

    return GradientLine{.start = SkPoint{halfWidth - endX, halfHeight + endY},
                        .end = SkPoint{halfWidth + endX, halfHeight - endY}};
}

std::optional<float> resolveEdge(const std::optional<facebook::react::ValueUnit>& edge, float referenceLength) {
    if (!edge.has_value() || !edge.value()) {
        return std::nullopt;
    }

    return edge.value().resolve(referenceLength);
}

float centerOnAxis(const std::optional<facebook::react::ValueUnit>& leadingEdge,
                   const std::optional<facebook::react::ValueUnit>& trailingEdge, float length) {
    const std::optional<float> fromLeadingEdge = resolveEdge(leadingEdge, length);

    if (fromLeadingEdge.has_value()) {
        return fromLeadingEdge.value();
    }

    const std::optional<float> fromTrailingEdge = resolveEdge(trailingEdge, length);

    if (fromTrailingEdge.has_value()) {
        return length - fromTrailingEdge.value();
    }

    return length / 2;
}

/**
 * `closest-side` and `farthest-side`: the distance from the centre to the nearest or farthest edge on each axis.
 * A circle takes the smaller or larger of the two so both radii match.
 */
GradientRadii sideRadii(SkPoint center, float width, float height, bool isClosest, bool isCircle) {
    const float horizontal =
        isClosest ? std::min(center.fX, width - center.fX) : std::max(center.fX, width - center.fX);
    const float vertical =
        isClosest ? std::min(center.fY, height - center.fY) : std::max(center.fY, height - center.fY);

    if (isCircle) {
        const float radius = isClosest ? std::min(horizontal, vertical) : std::max(horizontal, vertical);

        return GradientRadii{.horizontal = radius, .vertical = radius};
    }

    return GradientRadii{.horizontal = horizontal, .vertical = vertical};
}

/**
 * `closest-corner` and `farthest-corner`: the ellipse through the nearest or farthest corner. Per
 * https://www.w3.org/TR/css-images-3/#typedef-radial-size it has the aspect ratio of the same-named side-sized
 * ellipse, so the semi-major axis follows from the ellipse equation through that corner.
 */
GradientRadii cornerRadii(SkPoint center, float width, float height, bool isClosest, bool isCircle) {
    const std::array<SkPoint, 4> corners{SkPoint{0, 0}, SkPoint{width, 0}, SkPoint{width, height},
                                         SkPoint{0, height}};
    SkPoint chosenCorner = corners.front();
    float chosenDistance = std::hypot(center.fX - chosenCorner.fX, center.fY - chosenCorner.fY);

    for (const SkPoint& corner : corners) {
        const float distance = std::hypot(center.fX - corner.fX, center.fY - corner.fY);

        if (isClosest ? distance < chosenDistance : distance > chosenDistance) {
            chosenDistance = distance;
            chosenCorner = corner;
        }
    }

    if (isCircle) {
        return GradientRadii{.horizontal = chosenDistance, .vertical = chosenDistance};
    }

    const GradientRadii side = sideRadii(center, width, height, isClosest, false);
    const float aspectRatio = side.horizontal / side.vertical;

    if (!std::isfinite(aspectRatio) || aspectRatio == 0.0F) {
        return GradientRadii{};
    }

    const float offsetX = chosenCorner.fX - center.fX;
    const float offsetY = (chosenCorner.fY - center.fY) * aspectRatio;
    const float semiMajorAxis = std::hypot(offsetX, offsetY);

    return GradientRadii{.horizontal = semiMajorAxis, .vertical = semiMajorAxis / aspectRatio};
}

GradientRadii gradientRadii(const facebook::react::RadialGradient& gradient, SkPoint center, float width,
                            float height) {
    const bool isCircle = gradient.shape == facebook::react::RadialGradientShape::Circle;

    if (std::holds_alternative<facebook::react::RadialGradientSize::Dimensions>(gradient.size.value)) {
        const facebook::react::RadialGradientSize::Dimensions& dimensions =
            std::get<facebook::react::RadialGradientSize::Dimensions>(gradient.size.value);
        const float horizontal = dimensions.x.resolve(width);
        const float vertical = dimensions.y.resolve(height);

        if (isCircle) {
            const float radius = std::max(horizontal, vertical);

            return GradientRadii{.horizontal = radius, .vertical = radius};
        }

        return GradientRadii{.horizontal = horizontal, .vertical = vertical};
    }

    const facebook::react::RadialGradientSize::SizeKeyword keyword =
        std::get<facebook::react::RadialGradientSize::SizeKeyword>(gradient.size.value);

    if (keyword == facebook::react::RadialGradientSize::SizeKeyword::ClosestSide ||
        keyword == facebook::react::RadialGradientSize::SizeKeyword::FarthestSide) {
        return sideRadii(center, width, height,
                         keyword == facebook::react::RadialGradientSize::SizeKeyword::ClosestSide, isCircle);
    }

    return cornerRadii(center, width, height,
                       keyword == facebook::react::RadialGradientSize::SizeKeyword::ClosestCorner, isCircle);
}

sk_sp<SkShader> makeLinearGradientShader(const facebook::react::LinearGradient& gradient,
                                         const facebook::react::Rect& frame) {
    const float width = frame.size.width;
    const float height = frame.size.height;
    const float angleDegrees =
        std::holds_alternative<facebook::react::Float>(gradient.direction)
            ? std::get<facebook::react::Float>(gradient.direction)
            : angleForKeyword(std::get<facebook::react::GradientKeyword>(gradient.direction), width, height);
    const GradientLine line = gradientLine(angleDegrees, width, height);
    const GradientRamp ramp =
        toGradientRamp(gradient.colorStops, std::hypot(line.end.fX - line.start.fX, line.end.fY - line.start.fY));
    const std::array<SkPoint, 2> points{SkPoint{line.start.fX + frame.origin.x, line.start.fY + frame.origin.y},
                                        SkPoint{line.end.fX + frame.origin.x, line.end.fY + frame.origin.y}};

    return SkShaders::LinearGradient(points.data(), toSkGradient(ramp));
}

/**
 * An ellipse is the circle of the horizontal radius scaled about the centre until its vertical radius matches,
 * which is one local matrix rather than a second shader type. This is what React Native's Android renderer does.
 */
sk_sp<SkShader> makeRadialGradientShader(const facebook::react::RadialGradient& gradient,
                                         const facebook::react::Rect& frame) {
    const float width = frame.size.width;
    const float height = frame.size.height;
    const SkPoint center{centerOnAxis(gradient.position.left, gradient.position.right, width),
                         centerOnAxis(gradient.position.top, gradient.position.bottom, height)};
    const GradientRadii radii = gradientRadii(gradient, center, width, height);
    const GradientRamp ramp = toGradientRamp(gradient.colorStops, std::max(radii.horizontal, radii.vertical));
    const SkPoint absoluteCenter{center.fX + frame.origin.x, center.fY + frame.origin.y};
    const float radius = std::max(radii.horizontal, kSmallestRadius);

    if (radii.horizontal == radii.vertical) {
        return SkShaders::RadialGradient(absoluteCenter, radius, toSkGradient(ramp));
    }

    SkMatrix ellipseMatrix;

    ellipseMatrix.setScale(1.0F, radii.vertical / radius, absoluteCenter.fX, absoluteCenter.fY);

    return SkShaders::RadialGradient(absoluteCenter, radius, toSkGradient(ramp), &ellipseMatrix);
}

bool hasColorStops(const facebook::react::BackgroundImage& backgroundImage) {
    if (std::holds_alternative<facebook::react::LinearGradient>(backgroundImage)) {
        return !std::get<facebook::react::LinearGradient>(backgroundImage).colorStops.empty();
    }

    return !std::get<facebook::react::RadialGradient>(backgroundImage).colorStops.empty();
}

} // namespace

sk_sp<SkShader> makeGradientShader(const facebook::react::BackgroundImage& backgroundImage,
                                   const facebook::react::Rect& frame) {
    if (frame.size.width <= 0 || frame.size.height <= 0 || !hasColorStops(backgroundImage)) {
        return nullptr;
    }

    if (std::holds_alternative<facebook::react::LinearGradient>(backgroundImage)) {
        return makeLinearGradientShader(std::get<facebook::react::LinearGradient>(backgroundImage), frame);
    }

    return makeRadialGradientShader(std::get<facebook::react::RadialGradient>(backgroundImage), frame);
}

} // namespace react_native_linux
