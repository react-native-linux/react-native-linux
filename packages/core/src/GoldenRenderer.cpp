#include "GoldenRenderer.h"

#include "BundleRunner.h"
#include "ImageDecoder.h"
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

#include <chrono>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>

namespace react_native_linux {

namespace {

struct PixelCoordinate {
    int x{};
    int y{};
};

// A decode is asynchronous, and a headless render has no run loop to notice the frame damage a completion
// produces. Settling the decode queue before rasterising is what makes an image golden deterministic; without it
// the same bundle would produce a picture that depends on how fast a codec ran.
constexpr std::chrono::milliseconds kImageDecodeBudget{30000};

void settleImageDecodes() {
    if (!waitForPendingImageDecodes(kImageDecodeBudget)) {
        std::cerr << "[golden] gave up waiting for image decoding" << std::endl;
    }
}

facebook::react::Size toSurfaceSize(int width, int height) {
    return facebook::react::Size{.width = static_cast<facebook::react::Float>(width),
                                 .height = static_cast<facebook::react::Float>(height)};
}

// kRGBA_8888 rather than kN32 so the byte order in the PNG does not depend on the host's native order, and
// kPremul because that is what Skia rasterises into; the encoder unpremultiplies on the way out.
sk_sp<SkSurface> makeRasterSurface(int width, int height) {
    const SkImageInfo imageInfo = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(imageInfo);

    if (surface == nullptr) {
        std::cerr << "[golden] could not allocate a " << width << "x" << height << " raster surface" << std::endl;
    }

    return surface;
}

bool writeSurfaceAsPng(SkSurface& surface, const std::string& outputPath) {
    SkPixmap pixels;

    if (!surface.peekPixels(&pixels)) {
        std::cerr << "[golden] the raster surface did not expose its pixels" << std::endl;

        return false;
    }

    SkFILEWStream output(outputPath.c_str());

    if (!output.isValid()) {
        std::cerr << "[golden] could not open " << outputPath << " for writing" << std::endl;

        return false;
    }

    if (!SkPngEncoder::Encode(&output, pixels, SkPngEncoder::Options{})) {
        std::cerr << "[golden] PNG encoding failed for " << outputPath << std::endl;

        return false;
    }

    return true;
}

/**
 * The first pixel two surfaces disagree on, comparing only the bytes a pixel occupies: row padding is never drawn
 * into and would compare uninitialised memory.
 */
std::optional<PixelCoordinate> findFirstPixelDifference(const SkPixmap& first, const SkPixmap& second) {
    const size_t bytesPerPixel = static_cast<size_t>(first.info().bytesPerPixel());

    for (int y = 0; y < first.height(); ++y) {
        for (int x = 0; x < first.width(); ++x) {
            if (std::memcmp(first.addr(x, y), second.addr(x, y), bytesPerPixel) != 0) {
                return PixelCoordinate{.x = x, .y = y};
            }
        }
    }

    return std::nullopt;
}

bool areSurfacesIdentical(SkSurface& first, SkSurface& second) {
    SkPixmap firstPixels;
    SkPixmap secondPixels;

    if (!first.peekPixels(&firstPixels) || !second.peekPixels(&secondPixels)) {
        std::cerr << "[golden] a raster surface did not expose its pixels" << std::endl;

        return false;
    }

    const std::optional<PixelCoordinate> difference = findFirstPixelDifference(firstPixels, secondPixels);

    if (difference.has_value()) {
        std::cerr << "[golden] the damage-clipped redraw differs from the full redraw at (" << difference.value().x
                  << ", " << difference.value().y << ")" << std::endl;

        return false;
    }

    return true;
}

} // namespace

int renderGolden(const std::string& bundlePath, const std::string& outputPath, int width, int height) {
    const sk_sp<SkSurface> surface = makeRasterSurface(width, height);

    if (surface == nullptr) {
        return 1;
    }

    const FabricRunResult run = runFabricBundle(bundlePath, toSurfaceSize(width, height));

    settleImageDecodes();
    paintScene(*surface->getCanvas(), run.scene, {});

    if (!writeSurfaceAsPng(*surface, outputPath)) {
        return 1;
    }

    return run.hasReportedFatalError ? 1 : 0;
}

int renderDamageGolden(const std::string& bundlePath, const std::string& outputPath, int width, int height) {
    const sk_sp<SkSurface> fullSurface = makeRasterSurface(width, height);
    const sk_sp<SkSurface> damagedSurface = makeRasterSurface(width, height);

    if (fullSurface == nullptr || damagedSurface == nullptr) {
        return 1;
    }

    const FabricDamageRunResult run = runFabricBundleAcrossCommits(bundlePath, toSurfaceSize(width, height));

    if (!run.failure.empty()) {
        std::cerr << "[golden] " << run.failure << std::endl;

        return 1;
    }

    settleImageDecodes();

    // Both surfaces start as the same first frame, painted in full. The second frame is then drawn twice: once as
    // a full repaint, and once as the partial repaint a window would do, on top of the first frame's pixels. The
    // two have to be byte-identical, which is issue #12's acceptance criterion and the only thing that proves the
    // damage math without a GPU, a swapchain or a compositor.
    paintScene(*fullSurface->getCanvas(), run.firstScene, {});
    paintScene(*damagedSurface->getCanvas(), run.firstScene, {});

    paintScene(*fullSurface->getCanvas(), run.secondScene, {});
    paintScene(*damagedSurface->getCanvas(), run.secondScene, run.damage);

    if (!areSurfacesIdentical(*fullSurface, *damagedSurface)) {
        return 1;
    }

    if (!writeSurfaceAsPng(*damagedSurface, outputPath)) {
        return 1;
    }

    return run.hasReportedFatalError ? 1 : 0;
}

} // namespace react_native_linux
