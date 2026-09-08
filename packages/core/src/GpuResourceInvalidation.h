#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace react_native_linux {

/**
 * Every kind of cached resource in this process that a `VK_ERROR_DEVICE_LOST` can reach, and what the recovery
 * owes each one before the next paint.
 *
 * Recovering from a lost device is not a swapchain rebuild, and the reason it is a contract rather than a code
 * path is zed#62998: a cached handle that outlives the device it was created on is replayed into the fresh one
 * and crashes the next frame, and zed#58382, where the handle survives but the texture behind it is blank. The
 * only defence that keeps working as this renderer grows is that the set of caches is enumerated in one place and
 * the recovery switches over it exhaustively, so a cache added later is a compile error here rather than a
 * garbled frame after a resume.
 *
 * `GaneshResourceCacheAndGlyphAtlas` and `SwapchainImageSurfaces` are the two that hold device-owned memory: the
 * `GrDirectContext`'s own resource cache, which is where both an uploaded image texture and the glyph atlas the
 * text pipeline's paragraphs draw through actually live, and every `SkSurface` wrapping a swapchain image.
 *
 * `kGpuCachedResources` is in the order the recovery has to drop them in, which is why it is an array rather than
 * a set: the context is abandoned before the surfaces built on it are released, so releasing the last reference
 * to a surface cannot send a free through a device that is already gone.
 *
 * The other two hold none, and that is a property of this renderer's design rather than an accident worth
 * relying on silently. `RetainedSceneDecodedImagePixels` are `SkImages::RasterFromPixmapCopy` bitmaps, held by
 * the nodes drawing them; `TextPipelineParagraphLayouts` are built fresh by `layoutParagraph` on every
 * measurement and every paint and are never stored across frames. Neither names a `VkDevice`, so a rebuild
 * re-uploads them from host memory on first use and neither has to be dropped.
 */
enum class GpuCachedResource : uint8_t {
    GaneshResourceCacheAndGlyphAtlas,
    SwapchainImageSurfaces,
    RetainedSceneDecodedImagePixels,
    TextPipelineParagraphLayouts,
};

enum class GpuResourceFate : uint8_t { DroppedBeforeTheNextPaint, ReuploadedFromHostMemory };

constexpr std::array<GpuCachedResource, 4> kGpuCachedResources{
    GpuCachedResource::GaneshResourceCacheAndGlyphAtlas,
    GpuCachedResource::SwapchainImageSurfaces,
    GpuCachedResource::RetainedSceneDecodedImagePixels,
    GpuCachedResource::TextPipelineParagraphLayouts,
};

GpuResourceFate gpuResourceFateOnDeviceLoss(GpuCachedResource resource) noexcept;

std::string_view describeGpuCachedResource(GpuCachedResource resource) noexcept;

} // namespace react_native_linux
