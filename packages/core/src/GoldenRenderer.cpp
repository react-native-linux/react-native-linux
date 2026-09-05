#include "GoldenRenderer.h"

#include "BundleRunner.h"
#include "ScenePainter.h"
#include "TextGeometry.h"
#include "TextRasterizationPolicySkia.h"
#include "include/core/SkAlphaType.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/core/SkSurfaceProps.h"
#include "include/encode/SkPngEncoder.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <react/renderer/graphics/Float.h>
#include <react/renderer/graphics/Size.h>
#include <react/renderer/textlayoutmanager/TextLayoutManager.h>
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
// kPremul because that is what Skia rasterises into; the encoder unpremultiplies on the way out. The surface
// props are the platform's text rasterization policy — the same call the swapchain surface makes.
sk_sp<SkSurface> makeRasterSurface(int width, int height) {
    const SkImageInfo imageInfo = SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
    const SkSurfaceProps surfaceProps = skSurfacePropsFor(textRasterizationPolicy());
    sk_sp<SkSurface> surface = SkSurfaces::Raster(imageInfo, &surfaceProps);

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
        const uint32_t painted =
            toOpaqueRgb(pixels.getColor(static_cast<int>(sample.point.x), static_cast<int>(sample.point.y)));
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

    const double comparableFraction =
        hits.empty() ? 0.0 : static_cast<double>(comparedCount) / static_cast<double>(hits.size());

    if (comparableFraction < kMinimumComparableFraction) {
        std::cerr << "[golden] only " << comparedCount << " of " << hits.size()
                  << " samples landed on a colour a node painted, which is too few to prove anything" << std::endl;

        return false;
    }

    return haveAgreed;
}

// Issue #41. Yoga rounds a measured size up to whole points and then assigns a frame, so the width a paragraph is
// painted at is never *more* than the width it was measured at, but it can be a fraction less — and a line that
// only just fitted then does not fit now. One point of slack absorbs the rounding; anything past that is a
// paragraph that re-wrapped, which is the whole of react-native-macos#2857.
constexpr float kTextFitTolerance = 1.0F;
// Wider than any fixture's text, so a measurement against it is the width the glyphs occupy or nothing at all.
constexpr float kShrinkToFitWidth = 10000.0F;

facebook::react::LayoutConstraints toConstraints(facebook::react::Float maximumWidth) {
    return facebook::react::LayoutConstraints{
        .maximumSize = facebook::react::Size{.width = maximumWidth,
                                             .height = std::numeric_limits<facebook::react::Float>::infinity()}};
}

bool doesParagraphFitItsBox(const ScenePrimitive& primitive, const facebook::react::TextLayoutManager& layoutManager) {
    const SceneTextContent& content = primitive.text.value();
    const float boxWidth = static_cast<float>(content.frame.size.width);
    const float boxHeight = static_cast<float>(content.frame.size.height);
    const ParagraphMetrics painted =
        measureParagraphMetrics(content.attributedString, content.paragraphAttributes, boxWidth);
    bool doesFit = true;

    if (painted.height > boxHeight + kTextFitTolerance || painted.longestLineWidth > boxWidth + kTextFitTolerance) {
        std::cerr << "[golden] tag " << primitive.tag << " was given a " << boxWidth << "x" << boxHeight
                  << " box and paints " << painted.lines.size() << " lines of " << painted.longestLineWidth << "x"
                  << painted.height << std::endl;

        doesFit = false;
    }

    // The measure path, twice: the second call is a cache hit, and a cache that could answer differently from the
    // paragraph behind it is the other half of the same bug.
    const facebook::react::LayoutConstraints constraints{
        .maximumSize = facebook::react::Size{.width = content.frame.size.width,
                                             .height = std::numeric_limits<facebook::react::Float>::infinity()}};
    const facebook::react::AttributedStringBox box{content.attributedString};
    const facebook::react::TextMeasurement first =
        layoutManager.measure(box, content.paragraphAttributes, {}, constraints);
    const facebook::react::TextMeasurement second =
        layoutManager.measure(box, content.paragraphAttributes, {}, constraints);

    if (first.size.height != second.size.height || first.size.width != second.size.width) {
        std::cerr << "[golden] tag " << primitive.tag << " measured " << first.size.width << "x" << first.size.height
                  << " and then " << second.size.width << "x" << second.size.height << std::endl;

        doesFit = false;
    }

    if (first.size.height < painted.height) {
        std::cerr << "[golden] tag " << primitive.tag << " measured " << first.size.height << " points tall and paints "
                  << painted.height << std::endl;

        doesFit = false;
    }

    return doesFit;
}

/**
 * Issue #111. Three properties of the measure path that a paragraph's own box cannot show, asserted for every
 * text node in every fixture rather than for a table of strings, because a fixture is where a real
 * `AttributedString` with real fonts and real fragments comes from.
 *
 * 1. **A paragraph shrinks to its longest line.** Measured against a constraint far wider than the text, the
 *    answer has to be the width the glyphs occupy and not the constraint —
 *    [core#54571](https://github.com/facebook/react-native/issues/54571) is multi-line `<Text>` that stopped
 *    being able to.
 * 2. **An empty paragraph has no width.** [core#55468](https://github.com/facebook/react-native/issues/55468) is
 *    an empty `<Text>` measuring non-zero, and the check is the same string with its fragments removed.
 * 3. **The cache key is the whole input.** Measuring the same string at a different width, and then with a
 *    different `maximumNumberOfLines`, has to produce a different answer from the first measurement — a cache
 *    keyed on less than it depends on returns the first answer forever, which is a stale size reaching layout.
 */
bool doesMeasurementDependOnItsInputs(const ScenePrimitive& primitive,
                                      const facebook::react::TextLayoutManager& layoutManager) {
    const SceneTextContent& content = primitive.text.value();
    const facebook::react::AttributedStringBox box{content.attributedString};
    const facebook::react::Size measured =
        layoutManager.measure(box, content.paragraphAttributes, {}, toConstraints(content.frame.size.width)).size;
    const facebook::react::Size unbounded =
        layoutManager.measure(box, content.paragraphAttributes, {}, toConstraints(kShrinkToFitWidth)).size;
    bool isDependent = true;

    if (unbounded.width >= kShrinkToFitWidth) {
        std::cerr << "[golden] tag " << primitive.tag << " measured " << unbounded.width << " wide against a "
                  << kShrinkToFitWidth << " point constraint instead of shrinking to its text" << std::endl;

        isDependent = false;
    }

    facebook::react::AttributedString emptied = content.attributedString;

    emptied.getFragments().clear();

    const facebook::react::Size empty =
        layoutManager
            .measure(facebook::react::AttributedStringBox{emptied}, content.paragraphAttributes, {},
                     toConstraints(content.frame.size.width))
            .size;

    if (empty.width != 0) {
        std::cerr << "[golden] tag " << primitive.tag << " measures an empty paragraph " << empty.width << " wide"
                  << std::endl;

        isDependent = false;
    }

    // The cache key has to include the paragraph attributes, and a line limit is the attribute with an
    // unambiguous consequence: a paragraph that wraps onto more than one line must measure strictly shorter when
    // it is limited to one. A cache that ignored the attributes would answer with the height it already had.
    // A paragraph that is already one line has nothing to truncate and is skipped rather than asserted about.
    const ParagraphMetrics painted = measureParagraphMetrics(content.attributedString, content.paragraphAttributes,
                                                             static_cast<float>(content.frame.size.width));

    if (painted.lines.size() > 1) {
        facebook::react::ParagraphAttributes oneLine = content.paragraphAttributes;

        oneLine.maximumNumberOfLines = 1;

        const facebook::react::Size limited =
            layoutManager.measure(box, oneLine, {}, toConstraints(content.frame.size.width)).size;

        if (limited.height >= measured.height) {
            std::cerr << "[golden] tag " << primitive.tag << " measured " << measured.height << " points tall over "
                      << painted.lines.size() << " lines and " << limited.height << " limited to one" << std::endl;

            isDependent = false;
        }
    }

    return isDependent;
}

// Issue #53, and rn-macos#1395: a caret whose height is a constant looks right at one font size and wrong at
// every other. The caret has to be as tall as the line it is on, whatever that line's font size and `lineHeight`
// work out as, so this asks the paragraph which line the caret's own midpoint lands in and compares the two.
// One point of tolerance, because the caret is the line's exact height and the line metric is a rounded one —
// at forty points those differ by half a point. A caret that ignored the line would be wrong by tens.
constexpr float kCaretHeightTolerance = 1.0F;

bool doesCaretMatchItsLine(const ScenePrimitive& primitive) {
    const SceneTextContent& content = primitive.text.value();
    const EditorGeometry geometry = measureEditorGeometry(content, primitive.editor.value());
    const ParagraphMetrics metrics =
        measureParagraphMetrics(content.attributedString, content.paragraphAttributes, geometry.layoutWidth);
    const float caretMiddle = static_cast<float>(geometry.caret.origin.y + (geometry.caret.size.height / 2));
    float lineTop = 0.0F;

    for (const ParagraphLineMetrics& line : metrics.lines) {
        const float lineBottom = lineTop + line.height;

        if (caretMiddle < lineBottom) {
            if (std::abs(static_cast<float>(geometry.caret.size.height) - line.height) > kCaretHeightTolerance) {
                std::cerr << "[golden] tag " << primitive.tag << " draws a " << geometry.caret.size.height
                          << " point caret on a " << line.height << " point line" << std::endl;

                return false;
            }

            return true;
        }

        lineTop = lineBottom;
    }

    // An empty field has no line to be as tall as, and the caret is the font's own line height there.
    return metrics.lines.empty();
}

// Issue #114, item 6: a `<TextInput>` and a `<Text>` holding the same string in the same style have the same
// height. This does not hold against a field given an explicit height of its own — several fixtures style a
// field taller or shorter than its text on purpose, to prove the caret and the scrolling rules — so the proof is
// scoped to a `<TextInput>`/`<Text>` pair whose *text* matches, rather than to every field in every fixture: a
// fixture that wants this proof holds one of each, styled identically and with a string distinctive enough that
// a coincidental match is not a concern, and gives neither an explicit height. Matching on the string rather
// than the whole attributed string (style included) is deliberate — a `<TextInput>` and a `<Text>` build their
// fragments through different code paths with different incidental attributes (a `parentShadowView`, a text
// alignment default) that do not bear on height, and asking for those to agree too would make the proof about
// the fixture rather than about the layout. Every other fixture's fields simply have no `<Text>` with the same
// words, so this is a silent no-op for them.
bool doesEveryTextInputAgreeWithACompanionText(const SceneSnapshot& scene) {
    bool doAllAgree = true;

    for (const ScenePrimitive& field : scene) {
        if (!field.text.has_value() || !field.editor.has_value()) {
            continue;
        }

        for (const ScenePrimitive& text : scene) {
            if (!text.text.has_value() || text.editor.has_value()) {
                continue;
            }

            if (field.text->attributedString.getString() != text.text->attributedString.getString()) {
                continue;
            }

            if (std::abs(static_cast<float>(field.text->frame.size.height) -
                        static_cast<float>(text.text->frame.size.height)) > kTextFitTolerance) {
                std::cerr << "[golden] tag " << field.tag << " is a TextInput " << field.text->frame.size.height
                          << " points tall, but tag " << text.tag << " holds the same string in a <Text> "
                          << text.text->frame.size.height << " points tall" << std::endl;

                doAllAgree = false;
            }
        }
    }

    return doAllAgree;
}

bool doParagraphsFitTheirBoxes(const SceneSnapshot& scene) {
    const facebook::react::TextLayoutManager layoutManager{nullptr};
    size_t paragraphCount = 0;
    bool doAllFit = true;

    for (const ScenePrimitive& primitive : scene) {
        if (!primitive.text.has_value()) {
            continue;
        }

        paragraphCount++;

        // A `<TextInput>` is a window onto content that is allowed to be longer than it is — that is what
        // scrolling a field means — so the box-holds-the-paragraph rule is a `<Text>` rule and the caret rule is
        // the field's. See *The measured-paragraph proof* in docs/cpp-toolchain.md.
        if (primitive.editor.has_value()) {
            doAllFit = doesCaretMatchItsLine(primitive) && doAllFit;

            continue;
        }

        doAllFit = doesParagraphFitItsBox(primitive, layoutManager) && doAllFit;
        doAllFit = doesMeasurementDependOnItsInputs(primitive, layoutManager) && doAllFit;
    }

    if (paragraphCount == 0) {
        std::cerr << "[golden] the scene has no text, so there is nothing to prove about it" << std::endl;

        return false;
    }

    return doAllFit && doesEveryTextInputAgreeWithACompanionText(scene);
}

// Issue #46. Geometry only: the frame, the composed matrix, and the clip frames the primitive was painted under.
// Colours and pixels are left out on purpose — an image that decoded between the two snapshots is supposed to
// appear, and a caret that blinked is supposed to blink; the layout under them is what must not move.
bool haveSameGeometry(const ScenePrimitive& first, const ScenePrimitive& settled) {
    if (first.tag != settled.tag || first.frame != settled.frame || first.clips.size() != settled.clips.size()) {
        return false;
    }

    if (std::memcmp(&first.matrix, &settled.matrix, sizeof(SceneMatrix)) != 0) {
        return false;
    }

    for (size_t clip = 0; clip < first.clips.size(); clip++) {
        if (first.clips[clip].frame != settled.clips[clip].frame) {
            return false;
        }
    }

    return true;
}

bool doFirstAndSettledFramesAgree(const SceneSnapshot& first, const SceneSnapshot& settled) {
    if (first.empty()) {
        std::cerr << "[golden] the bundle committed no scene, so there is no first frame to hold to" << std::endl;

        return false;
    }

    if (first.size() != settled.size()) {
        std::cerr << "[golden] the first frame painted " << first.size() << " primitives and the settled one "
                  << settled.size() << std::endl;

        return false;
    }

    for (size_t index = 0; index < first.size(); index++) {
        if (!haveSameGeometry(first[index], settled[index])) {
            std::cerr << "[golden] tag " << first[index].tag << " was at (" << first[index].frame.origin.x << ", "
                      << first[index].frame.origin.y << ") " << first[index].frame.size.width << "x"
                      << first[index].frame.size.height << " on the first frame and at ("
                      << settled[index].frame.origin.x << ", " << settled[index].frame.origin.y << ") "
                      << settled[index].frame.size.width << "x" << settled[index].frame.size.height
                      << " once settled" << std::endl;

            return false;
        }
    }

    return true;
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
    const FabricRunResult run = runTypedFabricBundle(bundlePath, toSurfaceSize(width, height), keySequence);

    // A typed golden is a field with a caret in it, so it is also where the caret geometry of #53 is proved.
    if (!doParagraphsFitTheirBoxes(run.scene)) {
        return 1;
    }

    return paintSettledScene(run, outputPath, width, height);
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

    const FabricHitPaintRunResult run =
        runHitSampledFabricBundle(bundlePath, toSurfaceSize(width, height), kHitSampleStep);
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

int renderTextFitGolden(const std::string& bundlePath, const std::string& outputPath, int width, int height) {
    const FabricRunResult run = runFabricBundle(bundlePath, toSurfaceSize(width, height));

    if (!doParagraphsFitTheirBoxes(run.scene)) {
        return 1;
    }

    return paintSettledScene(run, outputPath, width, height);
}

int renderFirstFrameGolden(const std::string& bundlePath, const std::string& outputPath, int width, int height) {
    const FabricFrameRunResult run = runFabricBundleAcrossFrames(bundlePath, toSurfaceSize(width, height));

    if (!doFirstAndSettledFramesAgree(run.firstScene, run.settledScene)) {
        return 1;
    }

    return paintSettledScene(FabricRunResult{.scene = run.settledScene,
                                             .sceneDump = {},
                                             .hasReportedFatalError = run.hasReportedFatalError},
                             outputPath, width, height);
}

} // namespace react_native_linux
