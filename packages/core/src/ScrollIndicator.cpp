#include "ScrollIndicator.h"

#include <algorithm>

namespace react_native_linux {

namespace {

using facebook::react::Float;
using facebook::react::Rect;

// A desktop overlay scrollbar's proportions: thin, off the edge, and never so short it cannot be seen.
constexpr Float kThickness = 6;
constexpr Float kInset = 2;
constexpr Float kMinimumThumbLength = 24;

/** Where the thumb starts along the track, and how long it is, for one axis. */
struct AxisThumb {
    Float trackLength;
    Float start;
    Float length;
};

std::optional<AxisThumb> thumbAlong(Float viewport, Float content, Float offset) noexcept {
    const Float trackLength = viewport - (2 * kInset);

    if (content <= viewport || trackLength <= 0) {
        return std::nullopt;
    }

    const Float length =
        std::clamp(trackLength * viewport / content, std::min(kMinimumThumbLength, trackLength), trackLength);
    const Float progress = std::clamp(offset / (content - viewport), Float{0}, Float{1});

    return AxisThumb{
        .trackLength = trackLength, .start = kInset + ((trackLength - length) * progress), .length = length};
}

} // namespace

std::optional<ScrollIndicatorGeometry> verticalScrollIndicator(facebook::react::Size viewport,
                                                               facebook::react::Size content, Float offset) noexcept {
    const std::optional<AxisThumb> thumb = thumbAlong(viewport.height, content.height, offset);

    if (!thumb.has_value() || viewport.width < kInset + kThickness) {
        return std::nullopt;
    }

    const Float edge = viewport.width - kInset - kThickness;

    return ScrollIndicatorGeometry{
        .track = Rect{.origin = {.x = edge, .y = kInset}, .size = {.width = kThickness, .height = thumb->trackLength}},
        .thumb =
            Rect{.origin = {.x = edge, .y = thumb->start}, .size = {.width = kThickness, .height = thumb->length}}};
}

std::optional<ScrollIndicatorGeometry> horizontalScrollIndicator(facebook::react::Size viewport,
                                                                 facebook::react::Size content, Float offset) noexcept {
    const std::optional<AxisThumb> thumb = thumbAlong(viewport.width, content.width, offset);

    if (!thumb.has_value() || viewport.height < kInset + kThickness) {
        return std::nullopt;
    }

    const Float edge = viewport.height - kInset - kThickness;

    return ScrollIndicatorGeometry{
        .track = Rect{.origin = {.x = kInset, .y = edge}, .size = {.width = thumb->trackLength, .height = kThickness}},
        .thumb =
            Rect{.origin = {.x = thumb->start, .y = edge}, .size = {.width = thumb->length, .height = kThickness}}};
}

} // namespace react_native_linux
