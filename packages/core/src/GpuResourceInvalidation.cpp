#include "GpuResourceInvalidation.h"

namespace react_native_linux {

GpuResourceFate gpuResourceFateOnDeviceLoss(GpuCachedResource resource) noexcept {
    switch (resource) { // COV_EXCL: every GpuCachedResource value has a case, so no-match cannot execute
    case GpuCachedResource::GaneshResourceCacheAndGlyphAtlas:
    case GpuCachedResource::SwapchainImageSurfaces:
        return GpuResourceFate::DroppedBeforeTheNextPaint;
    case GpuCachedResource::RetainedSceneDecodedImagePixels:
    case GpuCachedResource::TextPipelineParagraphLayouts:
        return GpuResourceFate::ReuploadedFromHostMemory;
    }

    return GpuResourceFate::DroppedBeforeTheNextPaint; // COV_EXCL: every value has a case above
}

std::string_view describeGpuCachedResource(GpuCachedResource resource) noexcept {
    switch (resource) { // COV_EXCL: every GpuCachedResource value has a case, so no-match cannot execute
    case GpuCachedResource::GaneshResourceCacheAndGlyphAtlas:
        return "ganesh-resource-cache-and-glyph-atlas";
    case GpuCachedResource::SwapchainImageSurfaces:
        return "swapchain-image-surfaces";
    case GpuCachedResource::RetainedSceneDecodedImagePixels:
        return "retained-scene-decoded-image-pixels";
    case GpuCachedResource::TextPipelineParagraphLayouts:
        return "text-pipeline-paragraph-layouts";
    }

    return "ganesh-resource-cache-and-glyph-atlas"; // COV_EXCL: every value has a case above
}

} // namespace react_native_linux
