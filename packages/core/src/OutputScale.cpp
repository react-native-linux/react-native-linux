#include "OutputScale.h"

#include <cmath>

namespace react_native_linux {

uint32_t bufferExtentOf(uint32_t logicalExtent, uint32_t preferredScale) noexcept {
    const uint64_t scaled = static_cast<uint64_t>(logicalExtent) * preferredScale;

    return static_cast<uint32_t>((scaled + (kFractionalScaleDenominator / 2)) / kFractionalScaleDenominator);
}

std::vector<facebook::react::Rect> scaleDamageOutward(const std::vector<facebook::react::Rect>& damage,
                                                      uint32_t numerator, uint32_t denominator) {
    std::vector<facebook::react::Rect> scaled;
    scaled.reserve(damage.size());

    const auto scaleDown = [numerator, denominator](facebook::react::Float value) {
        return std::floor(static_cast<double>(value) * numerator / denominator);
    };
    const auto scaleUp = [numerator, denominator](facebook::react::Float value) {
        return std::ceil(static_cast<double>(value) * numerator / denominator);
    };

    for (const facebook::react::Rect& rectangle : damage) {
        const double left = scaleDown(rectangle.origin.x);
        const double top = scaleDown(rectangle.origin.y);
        const double right = scaleUp(rectangle.origin.x + rectangle.size.width);
        const double bottom = scaleUp(rectangle.origin.y + rectangle.size.height);

        scaled.push_back(facebook::react::Rect{
            .origin = {.x = static_cast<facebook::react::Float>(left), .y = static_cast<facebook::react::Float>(top)},
            .size = {.width = static_cast<facebook::react::Float>(right - left),
                     .height = static_cast<facebook::react::Float>(bottom - top)}});
    }

    return scaled;
}

} // namespace react_native_linux
