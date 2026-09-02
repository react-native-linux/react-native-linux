#include "TextPipeline.h"

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
#include <cmath>
#include <cstddef>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace react_native_linux {

namespace {

// The family name of the fonts scripts/vendor-fonts.ts writes into packages/core/fonts, and the family skparagraph
// itself names as its default. Text with no `fontFamily` asks for both, in that order.
constexpr char kBundledFontFamily[] = "Noto Sans";
constexpr char kFallbackFontFamily[] = DEFAULT_FONT_FAMILY;
constexpr char kEllipsisUtf8[] = "\xE2\x80\xA6";
constexpr float kDefaultFontSize = 14.0F;
constexpr float kUnlimitedLayoutWidth = 1.0e6F;

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

std::vector<SkString> toFontFamilies(const facebook::react::TextAttributes& attributes) {
    std::vector<SkString> families;

    if (!attributes.fontFamily.empty()) {
        families.emplace_back(attributes.fontFamily);
    }

    families.emplace_back(kBundledFontFamily);
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
 * React Native platforms and CSS do.
 */
void applyLineHeight(skia::textlayout::TextStyle& style, const facebook::react::TextAttributes& attributes,
                     float fontSize) {
    if (std::isnan(attributes.lineHeight) || fontSize <= 0.0F) {
        return;
    }

    style.setHeight(static_cast<float>(attributes.lineHeight) / fontSize);
    style.setHeightOverride(true);
    style.setHalfLeading(true);
}

skia::textlayout::TextStyle toTextStyle(const facebook::react::TextAttributes& attributes) {
    skia::textlayout::TextStyle style;
    const float fontSize = resolvedFontSize(attributes);

    style.setFontFamilies(toFontFamilies(attributes));
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
                                                  const facebook::react::ParagraphAttributes& paragraphAttributes) {
    skia::textlayout::ParagraphStyle style;
    const facebook::react::TextAttributes& baseAttributes = attributedString.getBaseTextAttributes();

    style.setTextStyle(toTextStyle(baseAttributes));
    style.setTextAlign(toTextAlign(baseAttributes));
    style.setTextDirection(skia::textlayout::TextDirection::kLtr);

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

} // namespace

std::unique_ptr<skia::textlayout::Paragraph>
layoutParagraph(const facebook::react::AttributedString& attributedString,
                const facebook::react::ParagraphAttributes& paragraphAttributes, float maximumWidth) {
    TextPipelineState& state = textPipelineState();
    const std::lock_guard<std::mutex> guard(state.mutex);
    const std::unique_ptr<skia::textlayout::ParagraphBuilder> builder = skia::textlayout::ParagraphBuilder::make(
        toParagraphStyle(attributedString, paragraphAttributes), state.fontCollection, state.unicode);

    for (const facebook::react::AttributedString::Fragment& fragment : attributedString.getFragments()) {
        if (fragment.isAttachment()) {
            builder->addPlaceholder(toPlaceholderStyle(fragment));

            continue;
        }

        builder->pushStyle(toTextStyle(fragment.textAttributes));
        builder->addText(fragment.string.data(), fragment.string.size());
        builder->pop();
    }

    std::unique_ptr<skia::textlayout::Paragraph> paragraph = builder->Build();

    paragraph->layout(toLayoutWidth(maximumWidth));

    return paragraph;
}

} // namespace react_native_linux
