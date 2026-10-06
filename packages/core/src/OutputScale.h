#pragma once

#include <cstdint>
#include <vector>

#include <react/renderer/graphics/Rect.h>

namespace react_native_linux {

/** `wp_fractional_scale_v1.preferred_scale` is the scale multiplied by this, so 120 is a scale of exactly 1. */
inline constexpr uint32_t kFractionalScaleDenominator = 120;

/**
 * The buffer pixels a toplevel of `logicalExtent` surface units has at `preferredScale` (in units of 1/120), rounded
 * half away from zero as fractional-scale-v1 specifies for toplevel surfaces.
 *
 * Pure, and free of Skia and Wayland, so it sits inside the `rnl_core_tests` coverage gate. See *Scale* in
 * docs/cpp-toolchain.md.
 */
uint32_t bufferExtentOf(uint32_t logicalExtent, uint32_t preferredScale) noexcept;

/**
 * Every damage rectangle multiplied by `numerator / denominator` and grown outward to whole pixels of the target
 * grid, so converted damage never covers less than the original did. Logical to buffer is
 * `(preferredScale, kFractionalScaleDenominator)`; buffer to logical is the reverse pair.
 */
std::vector<facebook::react::Rect> scaleDamageOutward(const std::vector<facebook::react::Rect>& damage,
                                                      uint32_t numerator, uint32_t denominator);

} // namespace react_native_linux
