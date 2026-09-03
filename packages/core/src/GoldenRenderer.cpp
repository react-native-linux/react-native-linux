#include "GoldenRenderer.h"

#include "BundleRunner.h"
#include "ScenePainter.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/encode/SkPngEncoder.h"

#include <react/renderer/graphics/Float.h>
#include <react/renderer/graphics/Size.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace react_native_linux {

namespace {

struct PixelCoordinate {
    int x{};
    int y{};
};

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

// Issue #35. Every node in the fixture paints one colour that no other node paints, so a pixel names the node
// whose pixels are visible at that point, and the hit test's answer at the same point has to be the same node.
//
// The sample step is four device pixels: fine enough that every edge in the fixture is crossed several times,
// coarse enough that a run is a second rather than a minute. The floor is what stops the blend filter below from
// turning a disagreement into a pass by skipping everything — an image where nothing is unambiguous is a broken
// fixture, not a proof.
constexpr int kHitSampleStep = 4;
constexpr double kMinimumComparableFraction = 0.75;

uint32_t toOpaqueRgb(SkColor color) {
    return static_cast<uint32_t>(SkColorSetRGB(SkColorGetR(color), SkColorGetG(color), SkColorGetB(color)));
}

/**
 * Which node painted each colour, or nothing when two nodes share one. A fixture that reuses a colour cannot
 * prove anything, so this is a failure rather than a silently weaker comparison.
 */
std::optional<std::unordered_map<uint32_t, facebook::react::Tag>> colorsToTags(const SceneSnapshot& scene) {
    std::unordered_map<uint32_t, facebook::react::Tag> tagsByColor;

    for (const ScenePrimitive& primitive : scene) {
        if (SkColorGetA(primitive.backgroundColorArgb) != SK_AlphaOPAQUE) {
            std::cerr << "[golden] tag " << primitive.tag << " does not paint an opaque background" << std::endl;

            return std::nullopt;
        }

        const uint32_t color = toOpaqueRgb(primitive.backgroundColorArgb);

        if (!tagsByColor.emplace(color, primitive.tag).second) {
            std::cerr << "[golden] tags " << tagsByColor.at(color) << " and " << primitive.tag
                      << " paint the same colour, so a pixel does not name a node" << std::endl;

            return std::nullopt;
        }
    }

    return tagsByColor;
}

std::string toColorText(uint32_t color) {
    std::ostringstream text;

    text << "#" << std::hex << std::uppercase << std::setfill('0') << std::setw(6) << color;

    return text.str();
}

bool doHitsMatchPixels(const std::vector<FabricHitSample>& hits, const SkPixmap& pixels,
                       const std::unordered_map<uint32_t, facebook::react::Tag>& tagsByColor) {
    size_t comparedCount = 0;
    bool haveAgreed = true;

    for (const FabricHitSample& sample : hits) {
        const uint32_t painted = toOpaqueRgb(pixels.getColor(static_cast<int>(sample.point.x),
                                                             static_cast<int>(sample.point.y)));
        const auto paintedTag = tagsByColor.find(painted);

        // An anti-aliased edge is a blend of two nodes' colours, and "the node visible here" has no answer there.
        if (paintedTag == tagsByColor.end()) {
            continue;
        }

        comparedCount++;

        if (paintedTag->second != sample.tag) {
            std::cerr << "[golden] at (" << sample.point.x << ", " << sample.point.y << ") the picture is "
                      << toColorText(painted) << ", which is tag " << paintedTag->second
                      << ", and the press lands on tag " << sample.tag << std::endl;

            haveAgreed = false;
        }
    }

    const double comparableFraction = hits.empty()
                                          ? 0.0
                                          : static_cast<double>(comparedCount) / static_cast<double>(hits.size());

    if (comparableFraction < kMinimumComparableFraction) {
        std::cerr << "[golden] only " << comparedCount << " of " << hits.size()
                  << " samples landed on a colour a node painted, which is too few to prove anything" << std::endl;

        return false;
    }

    return haveAgreed;
}

/**
 * Rasterises a settled scene and writes it, which is the half every single-frame golden shares regardless of what
 * the run did before it settled.
 */
int paintSettledScene(const FabricRunResult& run, const std::string& outputPath, int width, int height) {
    const sk_sp<SkSurface> surface = makeRasterSurface(width, height);

    if (surface == nullptr) {
        return 1;
    }

    paintScene(*surface->getCanvas(), run.scene, {});

    if (!writeSurfaceAsPng(*surface, outputPath)) {
        return 1;
    }

    return run.hasReportedFatalError ? 1 : 0;
}

} // namespace

int renderGolden(const std::string& bundlePath, const std::string& outputPath, int width, int height) {
    return paintSettledScene(runFabricBundle(bundlePath, toSurfaceSize(width, height)), outputPath, width, height);
}

int renderScrollGolden(const std::string& bundlePath, const std::string& outputPath,
                       facebook::react::Point surfacePoint, int wheelNotches, int width, int height) {
    return paintSettledScene(
        runScrolledFabricBundle(bundlePath, toSurfaceSize(width, height), surfacePoint, wheelNotches), outputPath,
        width, height);
}

int renderFocusGolden(const std::string& bundlePath, const std::string& outputPath, int tabPresses, int width,
                      int height) {
    return paintSettledScene(runFocusTabbedFabricBundle(bundlePath, toSurfaceSize(width, height), tabPresses),
                             outputPath, width, height);
}

int renderTypedGolden(const std::string& bundlePath, const std::string& outputPath, const std::string& keySequence,
                      int width, int height) {
    return paintSettledScene(runTypedFabricBundle(bundlePath, toSurfaceSize(width, height), keySequence),
                             outputPath, width, height);
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

int renderHitPaintGolden(const std::string& bundlePath, const std::string& outputPath, int width, int height) {
    const sk_sp<SkSurface> surface = makeRasterSurface(width, height);

    if (surface == nullptr) {
        return 1;
    }

    const FabricHitPaintRunResult run = runHitSampledFabricBundle(bundlePath, toSurfaceSize(width, height),
                                                                 kHitSampleStep);
    const std::optional<std::unordered_map<uint32_t, facebook::react::Tag>> tagsByColor = colorsToTags(run.scene);

    if (!tagsByColor.has_value()) {
        return 1;
    }

    paintScene(*surface->getCanvas(), run.scene, {});

    SkPixmap pixels;

    if (!surface->peekPixels(&pixels)) {
        std::cerr << "[golden] the raster surface did not expose its pixels" << std::endl;

        return 1;
    }

    if (!doHitsMatchPixels(run.hits, pixels, tagsByColor.value())) {
        return 1;
    }

    if (!writeSurfaceAsPng(*surface, outputPath)) {
        return 1;
    }

    return run.hasReportedFatalError ? 1 : 0;
}

} // namespace react_native_linux
