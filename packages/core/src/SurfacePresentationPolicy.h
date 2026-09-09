#pragma once

#include <cstdint>
#include <string_view>

namespace react_native_linux {

/**
 * What the swapchain is created with, decided over the two fields of `VkSurfaceCapabilitiesKHR` and the
 * `VkSurfaceFormatKHR` list that make a window invisible when read wrong: `supportedCompositeAlpha` and the
 * surface's advertised formats. Both questions are "what does this surface actually support, given what was
 * asked for" and both fail the same way — a window that is technically presented but shows nothing usable — so
 * they are one table here rather than two guards spread through `SkiaVulkanRenderer::createSwapchain`. The table
 * is pure and includes neither Vulkan nor Skia, exactly as `VulkanResultPolicy` and `SurfaceCommitGate` do, which
 * is what puts it inside the `rnl_core_tests` coverage gate. `SkiaVulkanRenderer` maps the real
 * `VkCompositeAlphaFlagBitsKHR`/`VkFormat` values into this file's inputs and outputs; it decides nothing. See
 * *Surface format and composite alpha* in docs/cpp-toolchain.md.
 */

/**
 * The composite-alpha modes a `VkSurfaceKHR` can advertise, as their own wire constants rather than through
 * `vulkan_core.h` — frozen by the Vulkan specification the same way `VulkanResultPolicy`'s `VkResult` constants
 * are, and stating them here is what keeps this file free of Vulkan. `SkiaVulkanRenderer.cpp` compiles a
 * `static_assert` per constant against the real `VkCompositeAlphaFlagBitsKHR` enumerator.
 */
constexpr uint32_t kCompositeAlphaOpaqueBit = 0x00000001;
constexpr uint32_t kCompositeAlphaPreMultipliedBit = 0x00000002;
constexpr uint32_t kCompositeAlphaPostMultipliedBit = 0x00000004;
constexpr uint32_t kCompositeAlphaInheritBit = 0x00000008;

/**
 * The chosen mode. Skia always paints the swapchain image with premultiplied colour — `kPremul_SkAlphaType` is
 * not a choice this renderer makes per frame, it is how `SkSurfaces::WrapBackendRenderTarget` is called — so
 * `PreMultiplied` is the only mode that composites what was actually painted without another pass over every
 * pixel. `Opaque` is the safe fallback: the compositor ignores the alpha channel entirely, which loses
 * transparency but never blends it incorrectly, unlike presenting premultiplied colour into a mode that expects
 * straight alpha. `PostMultiplied` and `InheritFromWindowSystem` are accepted only when nothing better is
 * offered, so that swapchain creation succeeds rather than throwing on a surface capable of showing the window at
 * all.
 */
enum class SurfaceAlphaChoice : uint8_t {
    PreMultiplied,
    Opaque,
    PostMultiplied,
    InheritFromWindowSystem,
};

/**
 * The rule, in precedence order:
 *
 * 1. `PreMultiplied`, if `kCompositeAlphaPreMultipliedBit` is supported. Matches Skia's output exactly.
 * 2. `Opaque`, if `kCompositeAlphaOpaqueBit` is supported. Correct-looking, merely not transparent.
 * 3. `PostMultiplied`, if `kCompositeAlphaPostMultipliedBit` is supported. Blends translucent edges incorrectly against
 *    premultiplied source colour, but is still a valid swapchain.
 * 4. `InheritFromWindowSystem` otherwise — the INHERIT-only surface, which some compositors report alone with
 *    nothing else set. There is always at least one bit set per the Vulkan specification, so this is reached only
 *    when INHERIT is the one bit present.
 */
SurfaceAlphaChoice selectSurfaceAlpha(uint32_t supportedCompositeAlphaFlags) noexcept;

/** The wire flag bit a choice was selected from, for `SkiaVulkanRenderer` to place into the create info. */
uint32_t compositeAlphaFlagBitFor(SurfaceAlphaChoice choice) noexcept;

/** The spelling of a choice, for the `--window-debug` trace. */
std::string_view describeSurfaceAlphaChoice(SurfaceAlphaChoice choice) noexcept;

/**
 * The two 8-bit formats `SkiaVulkanRenderer` knows how to wrap into an `SkSurface` (`colorTypeForFormat`),
 * reduced to which of them a surface's `VkSurfaceFormatKHR` list actually offers with the sRGB non-linear colour
 * space, so the selection is a table lookup instead of a loop with a `break` buried in it.
 */
struct SurfaceFormatAvailability {
    bool isPreferredBgra8Available{false};
    bool isFallbackRgba8Available{false};
};

/**
 * The chosen swapchain image format. `PreferredBgra8` is `VK_FORMAT_B8G8R8A8_UNORM`, the format every driver this
 * platform targets reports; `FallbackRgba8` is `VK_FORMAT_R8G8B8A8_UNORM`, offered by some software drivers
 * instead. `NoUsableFormat` is the missing-preferred-format case where neither is present — a real surface
 * capability this renderer cannot show anything with, and a named failure rather than an unreachable default.
 */
enum class SurfaceFormatChoice : uint8_t {
    PreferredBgra8,
    FallbackRgba8,
    NoUsableFormat,
};

SurfaceFormatChoice selectSurfaceFormat(SurfaceFormatAvailability availability) noexcept;

std::string_view describeSurfaceFormatChoice(SurfaceFormatChoice choice) noexcept;

} // namespace react_native_linux
