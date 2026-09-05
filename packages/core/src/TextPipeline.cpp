#include "TextPipeline.h"

#include "LineBoxMetrics.h"
#include "TextGeometry.h"

#include "include/core/SkFontTypes.h"
#include "include/core/SkColor.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkString.h"
#include "include/ports/SkFontMgr_directory.h"
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"
#include "modules/skparagraph/include/DartTypes.h"
#include "modules/skparagraph/include/FontCollection.h"
#include "modules/skparagraph/include/ParagraphBuilder.h"
#include "modules/skparagraph/include/ParagraphStyle.h"
#include "modules/skparagraph/include/TextStyle.h"
#include "modules/skunicode/include/SkUnicode.h"
#include "modules/skunicode/include/SkUnicode_icu.h"

#include <react/renderer/attributedstring/TextAttributes.h>
#include <react/renderer/attributedstring/primitives.h>
#include <react/renderer/graphics/Color.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace react_native_linux {

namespace {

// The family names of the fonts scripts/vendor-fonts.ts writes into packages/core/fonts, and the family
// skparagraph itself names as its default. Every text run asks for all of them, in that order.
constexpr char kBundledFontFamily[] = "Noto Sans";
// Noto Sans carries no emoji, so a codepoint outside it needs a second face. Naming that face in the family list
// rather than leaving it to `FontCollection::defaultEmojiFallback` is what makes it the *vendored* file: the
// character-fallback path only ever reaches a font manager's `matchFamilyStyleCharacter`, and the asset manager
// `SkFontMgr_New_Custom_Directory` builds answers nullptr to that, so an emoji resolved by fallback is resolved
// by fontconfig from whatever the machine has installed. A named family goes through `matchFamily` instead,
// which the asset manager does answer. See *Colour emoji and the fallback chain (#249)* in docs/cpp-toolchain.md.
constexpr char kEmojiFontFamily[] = "Noto Color Emoji";
constexpr char kFallbackFontFamily[] = DEFAULT_FONT_FAMILY;
constexpr char kEllipsisUtf8[] = "\xE2\x80\xA6";
constexpr float kDefaultFontSize = 14.0F;
constexpr float kUnlimitedLayoutWidth = 1.0e6F;
constexpr float kCaretWidth = 1.0F;
// The line height of an empty field, where there is no glyph to measure the caret against. It is the ratio
// SkParagraph's own strut uses for a font that reports no metrics, and it is only ever reached before the first
// character is typed.
constexpr float kEmptyLineHeightRatio = 1.2F;
constexpr char kSegmentationLocale[] = "en";

/**
 * `layout` takes a finite width, and React Native expresses "as wide as it wants" as an infinite maximum. A very
 * large finite width produces the same single-line result without feeding an infinity into Skia's arithmetic.
 */
float toLayoutWidth(float maximumWidth) {
    if (!std::isfinite(maximumWidth) || maximumWidth > kUnlimitedLayoutWidth) {
        return kUnlimitedLayoutWidth;
    }

    return std::max(maximumWidth, 0.0F);
}

SkColor toSkColor(facebook::react::SharedColor color) {
    return SkColorSetARGB(facebook::react::alphaFromColor(color), facebook::react::redFromColor(color),
                          facebook::react::greenFromColor(color), facebook::react::blueFromColor(color));
}

float resolvedFontSize(const facebook::react::TextAttributes& attributes) {
    const float fontSize = std::isnan(attributes.fontSize) ? kDefaultFontSize
                                                           : static_cast<float>(attributes.fontSize);
    const float multiplier = std::isnan(attributes.fontSizeMultiplier)
                                 ? 1.0F
                                 : static_cast<float>(attributes.fontSizeMultiplier);

    return fontSize * multiplier;
}

SkFontStyle::Slant toSlant(const facebook::react::TextAttributes& attributes) {
    if (attributes.fontStyle == facebook::react::FontStyle::Italic) {
        return SkFontStyle::kItalic_Slant;
    }

    if (attributes.fontStyle == facebook::react::FontStyle::Oblique) {
        return SkFontStyle::kOblique_Slant;
    }

    return SkFontStyle::kUpright_Slant;
}

SkFontStyle toFontStyle(const facebook::react::TextAttributes& attributes) {
    const int weight = attributes.fontWeight.has_value() ? static_cast<int>(attributes.fontWeight.value())
                                                         : static_cast<int>(SkFontStyle::kNormal_Weight);

    return SkFontStyle{weight, SkFontStyle::kNormal_Width, toSlant(attributes)};
}

/**
 * Reports a `fontFamily` that resolved to nothing, once per family name.
 *
 * A family nobody registered is substituted by the next one in the list, and a substitution nobody is told about
 * is the whole of react-native-windows [#16308](https://github.com/microsoft/react-native-windows/issues/16308):
 * an icon font that renders blank glyphs with no error anywhere. The text still draws — falling back is the right
 * behaviour, and refusing to draw would be worse — but it says so, and it says which family, which is the one
 * thing an author needs to find the missing asset.
 *
 * Once per name rather than once per paragraph, because a family that is missing is missing on every frame and a
 * line per frame is a line nobody reads. Guarded by the pipeline's own mutex, which every layout already takes.
 */
void reportUnresolvedFontFamily(const std::string& family, const SkString& substitute) {
    static std::unordered_set<std::string> reportedFamilies;

    if (reportedFamilies.insert(family).second) {
        std::cerr << "[text] fontFamily \"" << family << "\" is not registered; drawing \"" << substitute.c_str()
                  << "\" instead" << std::endl;
    }
}

/**
 * Whether the family a request resolved to is the family it asked for.
 *
 * Asking whether a name resolved at all is not the question: the default font manager is fontconfig, and
 * fontconfig substitutes rather than failing, so every name "resolves" and an icon font nobody installed comes
 * back as whatever the system had. Comparing the resolved face's own family name to the requested one is what
 * separates *found* from *substituted*, and the substitution is the thing to report.
 *
 * The CSS generic families are the exception, because resolving `monospace` to a real monospace face is the
 * whole point of asking for it rather than a failure to find it.
 */
bool isGenericFamily(const std::string& family) {
    static constexpr std::array<std::string_view, 5> kGenericFamilies{"serif", "sans-serif", "monospace", "cursive",
                                                                      "fantasy"};

    return std::find(kGenericFamilies.begin(), kGenericFamilies.end(), family) != kGenericFamilies.end();
}

std::vector<SkString> toFontFamilies(const facebook::react::TextAttributes& attributes,
                                     skia::textlayout::FontCollection& fontCollection) {
    std::vector<SkString> families;

    if (!attributes.fontFamily.empty()) {
        const std::vector<SkString> requested{SkString{attributes.fontFamily}};
        const std::vector<sk_sp<SkTypeface>> resolved = fontCollection.findTypefaces(requested,
                                                                                    toFontStyle(attributes));
        SkString resolvedFamily;

        if (!resolved.empty() && resolved.front() != nullptr) {
            resolved.front()->getFamilyName(&resolvedFamily);
        }

        if (!isGenericFamily(attributes.fontFamily) && !resolvedFamily.equals(attributes.fontFamily.c_str())) {
            reportUnresolvedFontFamily(attributes.fontFamily, resolvedFamily);
        }

        families.emplace_back(attributes.fontFamily);
    }

    families.emplace_back(kBundledFontFamily);
    families.emplace_back(kEmojiFontFamily);
    families.emplace_back(kFallbackFontFamily);

    return families;
}

skia::textlayout::TextDecoration toDecoration(const facebook::react::TextAttributes& attributes) {
    if (!attributes.textDecorationLineType.has_value()) {
        return skia::textlayout::TextDecoration::kNoDecoration;
    }

    switch (attributes.textDecorationLineType.value()) {
        case facebook::react::TextDecorationLineType::Underline:
            return skia::textlayout::TextDecoration::kUnderline;
        case facebook::react::TextDecorationLineType::Strikethrough:
            return skia::textlayout::TextDecoration::kLineThrough;
        case facebook::react::TextDecorationLineType::UnderlineStrikethrough:
            return static_cast<skia::textlayout::TextDecoration>(skia::textlayout::TextDecoration::kUnderline |
                                                                 skia::textlayout::TextDecoration::kLineThrough);
        case facebook::react::TextDecorationLineType::None:
            return skia::textlayout::TextDecoration::kNoDecoration;
    }

    return skia::textlayout::TextDecoration::kNoDecoration;
}

/**
 * `lineHeight` is an absolute point value in React Native and a multiple of the font size in Skia, so the ratio is
 * what crosses the boundary. Half leading splits the extra space above and below the line, which is what both
 * React Native platforms and CSS do; when `lineHeight` is under the font's own ascent plus descent that split is
 * negative and the glyphs overflow their line box rather than being shrunk or clipped. The arithmetic and the
 * policy are `src/LineBoxMetrics.h` and *Vertical metrics (#110)* in docs/cpp-toolchain.md.
 */
void applyLineHeight(skia::textlayout::TextStyle& style, const facebook::react::TextAttributes& attributes,
                     float fontSize) {
    const std::optional<float> ratio = lineHeightRatio(fontSize, static_cast<float>(attributes.lineHeight));

    if (!ratio.has_value()) {
        return;
    }

    style.setHeight(ratio.value());
    style.setHeightOverride(true);
    style.setHalfLeading(true);
}

skia::textlayout::TextStyle toTextStyle(const facebook::react::TextAttributes& attributes,
                                        skia::textlayout::FontCollection& fontCollection) {
    skia::textlayout::TextStyle style;
    const float fontSize = resolvedFontSize(attributes);

    style.setFontFamilies(toFontFamilies(attributes, fontCollection));
    style.setFontStyle(toFontStyle(attributes));
    style.setFontSize(fontSize);
    style.setFontHinting(SkFontHinting::kNone);
    style.setColor(toSkColor(attributes.foregroundColor));
    style.setDecoration(toDecoration(attributes));

    if (facebook::react::isColorMeaningful(attributes.textDecorationColor)) {
        style.setDecorationColor(toSkColor(attributes.textDecorationColor));
    } else {
        style.setDecorationColor(toSkColor(attributes.foregroundColor));
    }

    if (!std::isnan(attributes.letterSpacing)) {
        style.setLetterSpacing(static_cast<float>(attributes.letterSpacing));
    }

    if (facebook::react::isColorMeaningful(attributes.backgroundColor)) {
        SkPaint background;

        background.setColor(toSkColor(attributes.backgroundColor));
        style.setBackgroundPaint(background);
    }

    applyLineHeight(style, attributes, fontSize);

    return style;
}

skia::textlayout::TextAlign toTextAlign(const facebook::react::TextAttributes& attributes) {
    if (!attributes.alignment.has_value()) {
        return skia::textlayout::TextAlign::kStart;
    }

    switch (attributes.alignment.value()) {
        case facebook::react::TextAlignment::Left:
            return skia::textlayout::TextAlign::kLeft;
        case facebook::react::TextAlignment::Right:
            return skia::textlayout::TextAlign::kRight;
        case facebook::react::TextAlignment::Center:
            return skia::textlayout::TextAlign::kCenter;
        case facebook::react::TextAlignment::Justified:
            return skia::textlayout::TextAlign::kJustify;
        case facebook::react::TextAlignment::End:
            return skia::textlayout::TextAlign::kEnd;
        case facebook::react::TextAlignment::Natural:
        case facebook::react::TextAlignment::Start:
            return skia::textlayout::TextAlign::kStart;
    }

    return skia::textlayout::TextAlign::kStart;
}

/**
 * SkParagraph truncates at the tail and nowhere else, so `ellipsizeMode` collapses to "ellipsis or not". Head and
 * Middle are accepted and drawn as Tail; Clip drops the ellipsis and lets the line limit cut the text.
 */
skia::textlayout::ParagraphStyle toParagraphStyle(const facebook::react::AttributedString& attributedString,
                                                  const facebook::react::ParagraphAttributes& paragraphAttributes,
                                                  skia::textlayout::FontCollection& fontCollection) {
    skia::textlayout::ParagraphStyle style;
    const facebook::react::TextAttributes& baseAttributes = attributedString.getBaseTextAttributes();

    style.setTextStyle(toTextStyle(baseAttributes, fontCollection));
    style.setTextAlign(toTextAlign(baseAttributes));
    style.setTextDirection(skia::textlayout::TextDirection::kLtr);
    // kAll keeps the first line's ascent and the last line's descent inside the applied height, so every line box
    // in a paragraph is the same size. kDisableFirstAscent/kDisableLastDescent are the asymmetry upstream's two
    // platforms disagree over; see *Vertical metrics (#110)* in docs/cpp-toolchain.md.
    style.setTextHeightBehavior(skia::textlayout::TextHeightBehavior::kAll);

    if (paragraphAttributes.maximumNumberOfLines > 0) {
        style.setMaxLines(static_cast<size_t>(paragraphAttributes.maximumNumberOfLines));

        if (paragraphAttributes.ellipsizeMode != facebook::react::EllipsizeMode::Clip) {
            style.setEllipsis(SkString(kEllipsisUtf8));
        }
    }

    return style;
}

skia::textlayout::PlaceholderStyle toPlaceholderStyle(const facebook::react::AttributedString::Fragment& fragment) {
    const facebook::react::Size& size = fragment.parentShadowView.layoutMetrics.frame.size;

    return skia::textlayout::PlaceholderStyle{static_cast<float>(size.width), static_cast<float>(size.height),
                                              skia::textlayout::PlaceholderAlignment::kAboveBaseline,
                                              skia::textlayout::TextBaseline::kAlphabetic, 0.0F};
}

struct TextPipelineState {
    TextPipelineState()
        : fontCollection(sk_make_sp<skia::textlayout::FontCollection>()), unicode(SkUnicodes::ICU::Make()) {
        fontCollection->setAssetFontManager(SkFontMgr_New_Custom_Directory(RNL_BUNDLED_FONT_DIR));
        fontCollection->setDefaultFontManager(SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType()),
                                              kFallbackFontFamily);
    }

    std::mutex mutex;
    sk_sp<skia::textlayout::FontCollection> fontCollection;
    sk_sp<SkUnicode> unicode;
};

TextPipelineState& textPipelineState() {
    static TextPipelineState state;

    return state;
}

facebook::react::Rect toRect(const SkRect& rect) {
    return facebook::react::Rect{
        .origin = facebook::react::Point{.x = rect.fLeft, .y = rect.fTop},
        .size = facebook::react::Size{.width = rect.width(), .height = rect.height()}};
}

std::vector<facebook::react::Rect> toRects(const std::vector<skia::textlayout::TextBox>& boxes) {
    std::vector<facebook::react::Rect> rects;

    rects.reserve(boxes.size());

    for (const skia::textlayout::TextBox& box : boxes) {
        rects.push_back(toRect(box.rect));
    }

    return rects;
}

std::vector<skia::textlayout::TextBox> rangeBoxes(skia::textlayout::Paragraph& paragraph, size_t beginUtf16,
                                                  size_t endUtf16) {
    if (endUtf16 <= beginUtf16) {
        return {};
    }

    return paragraph.getRectsForRange(static_cast<unsigned>(beginUtf16), static_cast<unsigned>(endUtf16),
                                      skia::textlayout::RectHeightStyle::kMax,
                                      skia::textlayout::RectWidthStyle::kTight);
}

/**
 * The caret as a one-point-wide rectangle on the line it sits on.
 *
 * It is measured from the glyph after it, so it inherits that line's height rather than a constant — which is
 * react-native-macos#1395, where a caret kept a fixed height as the font size grew. At the end of the text there
 * is no glyph after it, so the glyph before it supplies the line and the caret goes on its trailing edge; an
 * empty field has neither, and only then is a height invented.
 */
facebook::react::Rect caretRectangle(skia::textlayout::Paragraph& paragraph, size_t caretUtf16, float emptyHeight) {
    const std::vector<skia::textlayout::TextBox> following = rangeBoxes(paragraph, caretUtf16, caretUtf16 + 1);

    if (!following.empty()) {
        const SkRect& rect = following.front().rect;

        return facebook::react::Rect{
            .origin = facebook::react::Point{.x = rect.fLeft, .y = rect.fTop},
            .size = facebook::react::Size{.width = kCaretWidth, .height = rect.height()}};
    }

    const std::vector<skia::textlayout::TextBox> preceding =
        caretUtf16 == 0 ? std::vector<skia::textlayout::TextBox>{}
                        : rangeBoxes(paragraph, caretUtf16 - 1, caretUtf16);

    if (!preceding.empty()) {
        const SkRect& rect = preceding.back().rect;

        return facebook::react::Rect{
            .origin = facebook::react::Point{.x = rect.fRight, .y = rect.fTop},
            .size = facebook::react::Size{.width = kCaretWidth, .height = rect.height()}};
    }

    return facebook::react::Rect{.origin = facebook::react::Point{.x = 0, .y = 0},
                                 .size = facebook::react::Size{.width = kCaretWidth, .height = emptyHeight}};
}

} // namespace

std::unique_ptr<skia::textlayout::Paragraph>
layoutParagraph(const facebook::react::AttributedString& attributedString,
                const facebook::react::ParagraphAttributes& paragraphAttributes, float maximumWidth) {
    TextPipelineState& state = textPipelineState();
    const std::lock_guard<std::mutex> guard(state.mutex);
    const std::unique_ptr<skia::textlayout::ParagraphBuilder> builder = skia::textlayout::ParagraphBuilder::make(
        toParagraphStyle(attributedString, paragraphAttributes, *state.fontCollection), state.fontCollection,
        state.unicode);

    for (const facebook::react::AttributedString::Fragment& fragment : attributedString.getFragments()) {
        if (fragment.isAttachment()) {
            builder->addPlaceholder(toPlaceholderStyle(fragment));

            continue;
        }

        builder->pushStyle(toTextStyle(fragment.textAttributes, *state.fontCollection));
        builder->addText(fragment.string.data(), fragment.string.size());
        builder->pop();
    }

    std::unique_ptr<skia::textlayout::Paragraph> paragraph = builder->Build();

    paragraph->layout(toLayoutWidth(maximumWidth));

    return paragraph;
}

EditorGeometry measureEditorGeometry(const SceneTextContent& text, const SceneEditorContent& editor) {
    const EditorGeometryRequest request{.caretUtf16 = editor.state.caretUtf16,
                                        .selectionBeginUtf16 = editor.state.selectionBeginUtf16,
                                        .selectionEndUtf16 = editor.state.selectionEndUtf16,
                                        .compositionBeginUtf16 = editor.state.compositionBeginUtf16,
                                        .compositionEndUtf16 = editor.state.compositionEndUtf16,
                                        .isMultiline = editor.isMultiline};

    return measureEditorGeometry(text.attributedString, text.paragraphAttributes,
                                 static_cast<float>(text.frame.size.width), request);
}

ParagraphMetrics measureParagraphMetrics(const facebook::react::AttributedString& attributedString,
                                         const facebook::react::ParagraphAttributes& paragraphAttributes,
                                         float maximumWidth) {
    const std::unique_ptr<skia::textlayout::Paragraph> paragraph =
        layoutParagraph(attributedString, paragraphAttributes, maximumWidth);
    ParagraphMetrics metrics{.longestLineWidth = paragraph->getLongestLine(), .height = paragraph->getHeight()};

    std::vector<skia::textlayout::LineMetrics> lines;

    paragraph->getLineMetrics(lines);

    for (const skia::textlayout::LineMetrics& line : lines) {
        metrics.lines.push_back(ParagraphLineMetrics{.width = static_cast<float>(line.fWidth),
                                                     .height = static_cast<float>(line.fHeight)});
    }

    return metrics;
}

EditorGeometry measureEditorGeometry(const facebook::react::AttributedString& attributedString,
                                     const facebook::react::ParagraphAttributes& paragraphAttributes,
                                     float maximumWidth, const EditorGeometryRequest& request) {
    // A single line is measured unwrapped, because a value longer than its box scrolls rather than wrapping. If
    // it turns out to fit, it is laid out again against the box so `textAlign` still applies — a field centred
    // in a 1e6-point line would otherwise be drawn a long way off screen.
    const float unwrappedWidth = request.isMultiline ? maximumWidth : kUnlimitedLayoutWidth;
    const std::unique_ptr<skia::textlayout::Paragraph> paragraph =
        layoutParagraph(attributedString, paragraphAttributes, unwrappedWidth);
    const float contentWidth = paragraph->getLongestLine();
    float layoutWidth = unwrappedWidth;

    if (!request.isMultiline && contentWidth <= maximumWidth) {
        TextPipelineState& state = textPipelineState();
        const std::lock_guard<std::mutex> guard(state.mutex);

        paragraph->layout(toLayoutWidth(maximumWidth));
        layoutWidth = maximumWidth;
    }

    const float emptyHeight = paragraph->getHeight() > 0.0F
                                  ? paragraph->getHeight()
                                  : resolvedFontSize(attributedString.getBaseTextAttributes()) * kEmptyLineHeightRatio;

    return EditorGeometry{
        .caret = caretRectangle(*paragraph, request.caretUtf16, emptyHeight),
        .selection = toRects(rangeBoxes(*paragraph, request.selectionBeginUtf16, request.selectionEndUtf16)),
        .composition = toRects(rangeBoxes(*paragraph, request.compositionBeginUtf16, request.compositionEndUtf16)),
        .contentWidth = contentWidth,
        .contentHeight = paragraph->getHeight(),
        .layoutWidth = layoutWidth};
}

size_t utf16IndexAtPoint(const facebook::react::AttributedString& attributedString,
                         const facebook::react::ParagraphAttributes& paragraphAttributes, float maximumWidth,
                         facebook::react::Point localPoint) {
    const std::unique_ptr<skia::textlayout::Paragraph> paragraph =
        layoutParagraph(attributedString, paragraphAttributes, maximumWidth);
    const skia::textlayout::PositionWithAffinity position =
        paragraph->getGlyphPositionAtCoordinate(static_cast<SkScalar>(localPoint.x),
                                                static_cast<SkScalar>(localPoint.y));

    return position.position <= 0 ? 0 : static_cast<size_t>(position.position);
}

TextSegments segmentText(const std::string& text) {
    TextSegments segments;

    segments.graphemeStarts.push_back(0);
    segments.wordStarts.push_back(0);

    if (text.empty()) {
        return segments;
    }

    TextPipelineState& state = textPipelineState();
    const std::lock_guard<std::mutex> guard(state.mutex);
    std::string segmented = text;
    skia_private::TArray<SkUnicode::CodeUnitFlags, true> codeUnitFlags;

    if (state.unicode->computeCodeUnitFlags(segmented.data(), static_cast<int>(segmented.size()), false,
                                            &codeUnitFlags)) {
        for (int index = 1; index < codeUnitFlags.size() && index < static_cast<int>(text.size()); ++index) {
            if (SkUnicode::hasGraphemeStartFlag(codeUnitFlags[index])) {
                segments.graphemeStarts.push_back(static_cast<size_t>(index));
            }
        }
    }

    std::vector<SkUnicode::Position> words;

    if (state.unicode->getUtf8Words(text.data(), static_cast<int>(text.size()), kSegmentationLocale, &words)) {
        for (const SkUnicode::Position word : words) {
            if (word > 0 && word < text.size()) {
                segments.wordStarts.push_back(word);
            }
        }
    }

    segments.graphemeStarts.push_back(text.size());
    segments.wordStarts.push_back(text.size());

    return segments;
}

} // namespace react_native_linux
