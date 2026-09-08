#include "SurfacePresentationPolicy.h"

namespace react_native_linux {

SurfaceAlphaChoice selectSurfaceAlpha(uint32_t supportedCompositeAlphaFlags) noexcept {
    if ((supportedCompositeAlphaFlags & kCompositeAlphaPreMultipliedBit) != 0) {
        return SurfaceAlphaChoice::PreMultiplied;
    }

    if ((supportedCompositeAlphaFlags & kCompositeAlphaOpaqueBit) != 0) {
        return SurfaceAlphaChoice::Opaque;
    }

    if ((supportedCompositeAlphaFlags & kCompositeAlphaPostMultipliedBit) != 0) {
        return SurfaceAlphaChoice::PostMultiplied;
    }

    return SurfaceAlphaChoice::InheritFromWindowSystem;
}

uint32_t compositeAlphaFlagBitFor(SurfaceAlphaChoice choice) noexcept {
    switch (choice) { // COV_EXCL: every SurfaceAlphaChoice value has a case, so no-match cannot execute
    case SurfaceAlphaChoice::PreMultiplied:
        return kCompositeAlphaPreMultipliedBit;
    case SurfaceAlphaChoice::Opaque:
        return kCompositeAlphaOpaqueBit;
    case SurfaceAlphaChoice::PostMultiplied:
        return kCompositeAlphaPostMultipliedBit;
    case SurfaceAlphaChoice::InheritFromWindowSystem:
        return kCompositeAlphaInheritBit;
    }

    return kCompositeAlphaInheritBit; // COV_EXCL: every SurfaceAlphaChoice value has a case above
}

std::string_view describeSurfaceAlphaChoice(SurfaceAlphaChoice choice) noexcept {
    switch (choice) { // COV_EXCL: every SurfaceAlphaChoice value has a case, so no-match cannot execute
    case SurfaceAlphaChoice::PreMultiplied:
        return "pre-multiplied";
    case SurfaceAlphaChoice::Opaque:
        return "opaque";
    case SurfaceAlphaChoice::PostMultiplied:
        return "post-multiplied";
    case SurfaceAlphaChoice::InheritFromWindowSystem:
        return "inherit-from-window-system";
    }

    return "inherit-from-window-system"; // COV_EXCL: every SurfaceAlphaChoice value has a case above
}

SurfaceFormatChoice selectSurfaceFormat(SurfaceFormatAvailability availability) noexcept {
    if (availability.isPreferredBgra8Available) {
        return SurfaceFormatChoice::PreferredBgra8;
    }

    if (availability.isFallbackRgba8Available) {
        return SurfaceFormatChoice::FallbackRgba8;
    }

    return SurfaceFormatChoice::NoUsableFormat;
}

std::string_view describeSurfaceFormatChoice(SurfaceFormatChoice choice) noexcept {
    switch (choice) { // COV_EXCL: every SurfaceFormatChoice value has a case, so no-match cannot execute
    case SurfaceFormatChoice::PreferredBgra8:
        return "bgra8";
    case SurfaceFormatChoice::FallbackRgba8:
        return "rgba8";
    case SurfaceFormatChoice::NoUsableFormat:
        return "no-usable-format";
    }

    return "no-usable-format"; // COV_EXCL: every SurfaceFormatChoice value has a case above
}

} // namespace react_native_linux
