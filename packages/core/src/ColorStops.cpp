#include "ColorStops.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>

#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/ValueUnit.h>

namespace react_native_linux {

namespace {

using facebook::react::ColorStop;
using facebook::react::ProcessedColorStop;
using facebook::react::SharedColor;

constexpr float kFractionReference = 1.0F;
// Android's `FloatUtil.floatsEqual` epsilon, so a hint exactly between its stops is dropped on both platforms.
constexpr float kPositionEpsilon = 0.00001F;
constexpr size_t kStopsPerHint = 9;
constexpr float kLongSideDivisions = 13.0F;
constexpr float kShortSideDivisions = 3.0F;
constexpr size_t kLongSideStops = 7;

/**
 * Where one authored colour stop sits on the gradient line, as a fraction of it. A percentage is that fraction
 * directly; a length is a distance along a line whose length only the caller knows; an unset position is left for
 * the fix-up to distribute.
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
 * CSS's fix-up: an unpositioned first stop sits at 0 and an unpositioned last stop at 1, a position never moves
 * backwards past the largest one before it, and each run of unpositioned stops is spread evenly between the two
 * positioned stops around it.
 */
std::vector<ProcessedColorStop> positionedStops(const std::vector<ColorStop>& colorStops, float gradientLineLength) {
    std::vector<ProcessedColorStop> fixed(colorStops.size());
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
            fixed[index] = ProcessedColorStop{.color = colorStops[index].color, .position = largestPositionSoFar};
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
            fixed[lastPositionedIndex + offset] =
                ProcessedColorStop{.color = colorStops[lastPositionedIndex + offset].color,
                                   .position = startPosition + (increment * static_cast<float>(offset))};
        }

        lastPositionedIndex = index;
    }

    return fixed;
}

/** Android's `ColorUtils.blendARGB`: each channel interpolated, then truncated. */
SharedColor blend(SharedColor left, SharedColor right, float weighting) {
    const auto channel = [weighting](uint8_t from, uint8_t to) {
        return static_cast<uint8_t>((static_cast<float>(from) * (1.0F - weighting)) +
                                    (static_cast<float>(to) * weighting));
    };

    return facebook::react::colorFromRGBA(
        channel(facebook::react::redFromColor(left), facebook::react::redFromColor(right)),
        channel(facebook::react::greenFromColor(left), facebook::react::greenFromColor(right)),
        channel(facebook::react::blueFromColor(left), facebook::react::blueFromColor(right)),
        channel(facebook::react::alphaFromColor(left), facebook::react::alphaFromColor(right)));
}

/**
 * The nine stops one hint between `left` and `right` at `hint` becomes: seven on the longer side of the hint and
 * two on the shorter, each coloured where the hint's exponential curve puts it. Blink's spacing, as Android ports it.
 */
std::array<ProcessedColorStop, kStopsPerHint> expandHint(const ProcessedColorStop& left, float hint,
                                                         const ProcessedColorStop& right) {
    const float leftPosition = left.position.value();
    const float rightPosition = right.position.value();
    const float leftDistance = hint - leftPosition;
    const float rightDistance = rightPosition - hint;
    const float totalDistance = rightPosition - leftPosition;
    const double logRatio = std::log(0.5) / std::log(leftDistance / totalDistance);
    std::array<ProcessedColorStop, kStopsPerHint> stops{};

    for (size_t index = 0; index < kStopsPerHint; index++) {
        const float step = static_cast<float>(index);
        float position = 0.0F;

        if (leftDistance > rightDistance) {
            position = index < kLongSideStops
                           ? leftPosition + (leftDistance * ((kLongSideStops + step) / kLongSideDivisions))
                           : hint + (rightDistance * ((step - kLongSideStops + 1.0F) / kShortSideDivisions));
        } else {
            position = index < 2 ? leftPosition + (leftDistance * ((step + 1.0F) / kShortSideDivisions))
                                 : hint + (rightDistance * ((step - 2.0F) / kLongSideDivisions));
        }

        const auto weighting = static_cast<float>(std::pow((position - leftPosition) / totalDistance, logRatio));

        stops[index] = ProcessedColorStop{.color = blend(left.color, right.color, weighting), .position = position};
    }

    return stops;
}

} // namespace

std::vector<ProcessedColorStop> fixedColorStops(const std::vector<ColorStop>& colorStops, float gradientLineLength) {
    std::vector<ProcessedColorStop> stops = positionedStops(colorStops, gradientLineLength);

    // A hint is never first or last, and CSS's grammar puts a colour on each side of it.
    for (size_t index = 1; index + 1 < stops.size(); index++) {
        if (stops[index].color) {
            continue;
        }

        const float hint = stops[index].position.value();
        const float leftDistance = hint - stops[index - 1].position.value();
        const float rightDistance = stops[index + 1].position.value() - hint;

        if (std::abs(leftDistance - rightDistance) < kPositionEpsilon) {
            stops.erase(stops.begin() + static_cast<std::ptrdiff_t>(index));
            index--;
        } else if (leftDistance < kPositionEpsilon) {
            stops[index].color = stops[index + 1].color;
        } else if (rightDistance < kPositionEpsilon) {
            stops[index].color = stops[index - 1].color;
        } else {
            const std::array<ProcessedColorStop, kStopsPerHint> expanded =
                expandHint(stops[index - 1], hint, stops[index + 1]);

            stops.erase(stops.begin() + static_cast<std::ptrdiff_t>(index));
            stops.insert(stops.begin() + static_cast<std::ptrdiff_t>(index), expanded.begin(), expanded.end());
            index += kStopsPerHint - 1;
        }
    }

    return stops;
}

} // namespace react_native_linux
