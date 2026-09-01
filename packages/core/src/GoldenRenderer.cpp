#include "GoldenRenderer.h"

#include "BundleRunner.h"
#include "ScenePainter.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/encode/SkPngEncoder.h"

#include <react/renderer/graphics/Float.h>
#include <react/renderer/graphics/Size.h>

#include <iostream>
#include <string>

namespace react_native_linux {

namespace {

facebook::react::Size toSurfaceSize(int width, int height) {
    return facebook::react::Size{.width = static_cast<facebook::react::Float>(width),
                                 .height = static_cast<facebook::react::Float>(height)};
}

} // namespace

int renderGolden(const std::string& bundlePath, const std::string& outputPath, int width, int height) {
    // kRGBA_8888 rather than kN32 so the byte order in the PNG does not depend on the host's native order, and
    // kPremul because that is what Skia rasterises into; the encoder unpremultiplies on the way out.
    const SkImageInfo imageInfo = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    const sk_sp<SkSurface> surface = SkSurfaces::Raster(imageInfo);

    if (surface == nullptr) {
        std::cerr << "[golden] could not allocate a " << width << "x" << height << " raster surface" << std::endl;

        return 1;
    }

    const FabricRunResult run = runFabricBundle(bundlePath, toSurfaceSize(width, height));

    paintScene(*surface->getCanvas(), run.scene);

    SkPixmap pixels;

    if (!surface->peekPixels(&pixels)) {
        std::cerr << "[golden] the raster surface did not expose its pixels" << std::endl;

        return 1;
    }

    SkFILEWStream output(outputPath.c_str());

    if (!output.isValid()) {
        std::cerr << "[golden] could not open " << outputPath << " for writing" << std::endl;

        return 1;
    }

    if (!SkPngEncoder::Encode(&output, pixels, SkPngEncoder::Options{})) {
        std::cerr << "[golden] PNG encoding failed for " << outputPath << std::endl;

        return 1;
    }

    return run.hasReportedFatalError ? 1 : 0;
}

} // namespace react_native_linux
