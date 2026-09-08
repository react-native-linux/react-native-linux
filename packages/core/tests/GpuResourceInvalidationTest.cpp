#include "GpuResourceInvalidation.h"

#include <array>
#include <gtest/gtest.h>
#include <string_view>

namespace {

using react_native_linux::describeGpuCachedResource;
using react_native_linux::GpuCachedResource;
using react_native_linux::GpuResourceFate;
using react_native_linux::gpuResourceFateOnDeviceLoss;
using react_native_linux::kGpuCachedResources;

struct InvalidationRow {
    GpuCachedResource resource;
    std::string_view name;
    GpuResourceFate fate;
};

// The contract restated as data, in the order the recovery drops them in: the Ganesh context is abandoned before
// the surfaces built on it are released, so the last reference to a surface cannot free through a device that is
// already gone. The two host-memory entries are as load-bearing as the two device-owned ones, because they are
// what says a decoded bitmap and a laid-out paragraph must *not* be thrown away on a resume.
constexpr std::array<InvalidationRow, 4> kInvalidationTable{{
    {GpuCachedResource::GaneshResourceCacheAndGlyphAtlas, "ganesh-resource-cache-and-glyph-atlas",
     GpuResourceFate::DroppedBeforeTheNextPaint},
    {GpuCachedResource::SwapchainImageSurfaces, "swapchain-image-surfaces", GpuResourceFate::DroppedBeforeTheNextPaint},
    {GpuCachedResource::RetainedSceneDecodedImagePixels, "retained-scene-decoded-image-pixels",
     GpuResourceFate::ReuploadedFromHostMemory},
    {GpuCachedResource::TextPipelineParagraphLayouts, "text-pipeline-paragraph-layouts",
     GpuResourceFate::ReuploadedFromHostMemory},
}};

TEST(GpuResourceInvalidationTest, EveryCachedResourceHasItsFateOnDeviceLoss) {
    for (const InvalidationRow& row : kInvalidationTable) {
        EXPECT_EQ(gpuResourceFateOnDeviceLoss(row.resource), row.fate) << row.name;
    }
}

TEST(GpuResourceInvalidationTest, EveryCachedResourceIsNamedForATrace) {
    for (const InvalidationRow& row : kInvalidationTable) {
        EXPECT_EQ(describeGpuCachedResource(row.resource), row.name);
    }
}

// The recovery walks this array, so a cache the array forgets is a cache a device loss leaves stale — which is
// the whole failure zed#62998 reports.
TEST(GpuResourceInvalidationTest, TheRecoveryWalksEveryCachedResourceInTheContractsOrder) {
    ASSERT_EQ(kGpuCachedResources.size(), kInvalidationTable.size());

    for (size_t index = 0; index < kGpuCachedResources.size(); ++index) {
        EXPECT_EQ(kGpuCachedResources[index], kInvalidationTable[index].resource) << index;
    }
}

} // namespace
