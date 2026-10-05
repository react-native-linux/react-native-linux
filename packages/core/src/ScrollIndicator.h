#pragma once

#include <optional>

#include <react/renderer/graphics/Float.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/graphics/Size.h>

namespace react_native_linux {

/**
 * One overlay scroll indicator (#49), in the scroll view's own coordinates: the strip along its edge the indicator
 * may occupy, and the thumb inside it. On a canvas there is no system scrollbar, so the indicator is something this
 * platform paints above the content and keeps out of the hit path; it overlays rather than insets, so it never
 * changes the content's layout.
 */
struct ScrollIndicatorGeometry {
    facebook::react::Rect track;
    facebook::react::Rect thumb;
};

/** The right-edge indicator, or none when the content is no taller than the viewport. */
std::optional<ScrollIndicatorGeometry> verticalScrollIndicator(facebook::react::Size viewport,
                                                               facebook::react::Size content,
                                                               facebook::react::Float offset) noexcept;

/** The bottom-edge indicator, or none when the content is no wider than the viewport. */
std::optional<ScrollIndicatorGeometry> horizontalScrollIndicator(facebook::react::Size viewport,
                                                                 facebook::react::Size content,
                                                                 facebook::react::Float offset) noexcept;

} // namespace react_native_linux
