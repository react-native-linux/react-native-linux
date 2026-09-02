#include <react/renderer/textlayoutmanager/TextLayoutManager.h>

#include "TextPipeline.h"

#include "include/core/SkRect.h"
#include "modules/skparagraph/include/DartTypes.h"
#include "modules/skparagraph/include/Paragraph.h"

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/AttributedStringBox.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/graphics/Float.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/graphics/Size.h>
#include <react/renderer/textlayoutmanager/TextLayoutContext.h>
#include <react/renderer/textlayoutmanager/TextMeasureCache.h>

#include <cmath>
#include <cstddef>
#include <memory>
#include <vector>

/**
 * The Linux `TextLayoutManager`: the one measurement Yoga asks for, answered by SkParagraph.
 *
 * This file replaces `react/renderer/textlayoutmanager/platform/cxx/.../TextLayoutManager.cpp`, whose `measure`
 * returns `layoutConstraints.minimumSize` and measures nothing. It is compiled in that file's place by a source
 * swap at our own CMake call site rather than by editing the vendored tree, and only when Skia is available; a
 * build configured with `-DRNL_ENABLE_SKIA=OFF` keeps the upstream stub, so the sanitizer presets and the
 * Skia-free unit build stay exactly as they were.
 *
 * The declaration is the vendored `platform/cxx` header, unchanged. That is what fixes the shape of this class:
 * `measure` is the only virtual, and `measureLines` and `prepareLayout` are absent, so the C++20 concepts in
 * `TextLayoutManagerExtended` report them unsupported and `ParagraphShadowNode` takes its non-prepared path.
 * Baseline alignment and `onTextLayout` therefore do not work yet; adding them means adding methods to a vendored
 * header, which is its own decision.
 *
 * The cache is `textMeasureCache_`, the upstream `TextMeasureCache` this class already owns: a 1024-entry
 * thread-safe LRU keyed by the layout-affecting parts of the attributed string, the paragraph attributes, the
 * constraints and the pixel scale factor. There is no second cache, because a paragraph cache keyed on the same
 * inputs would be the same cache.
 */
namespace facebook::react {

namespace {

/**
 * The attachment frames a measured paragraph produced, in fragment order.
 *
 * `ParagraphShadowNode::layout` asserts that there is exactly one attachment per attachment fragment, so a
 * placeholder that SkParagraph dropped — truncated away by a line limit — is reported as clipped rather than
 * omitted.
 */
TextMeasurement::Attachments measureAttachments(const AttributedString& attributedString,
                                                skia::textlayout::Paragraph& paragraph) {
    TextMeasurement::Attachments attachments;
    const std::vector<skia::textlayout::TextBox> placeholders = paragraph.getRectsForPlaceholders();
    size_t placeholderIndex = 0;

    for (const AttributedString::Fragment& fragment : attributedString.getFragments()) {
        if (!fragment.isAttachment()) {
            continue;
        }

        if (placeholderIndex >= placeholders.size()) {
            attachments.push_back(TextMeasurement::Attachment{.frame = {}, .isClipped = true});

            continue;
        }

        const SkRect& rect = placeholders[placeholderIndex].rect;

        attachments.push_back(TextMeasurement::Attachment{
            .frame = Rect{.origin = {.x = rect.fLeft, .y = rect.fTop},
                          .size = {.width = rect.width(), .height = rect.height()}},
            .isClipped = false});
        ++placeholderIndex;
    }

    return attachments;
}

TextMeasurement measureWithSkParagraph(const AttributedString& attributedString,
                                       const ParagraphAttributes& paragraphAttributes,
                                       const LayoutConstraints& layoutConstraints) {
    const std::unique_ptr<skia::textlayout::Paragraph> paragraph = react_native_linux::layoutParagraph(
        attributedString, paragraphAttributes, static_cast<float>(layoutConstraints.maximumSize.width));

    // getLongestLine is the width the text actually occupies, which is what Yoga wants; the layout width is only
    // the bound it was wrapped against. Rounding up keeps the last glyph inside the frame Yoga then assigns.
    const Size size = layoutConstraints.clamp(
        Size{.width = static_cast<Float>(std::ceil(paragraph->getLongestLine())),
             .height = static_cast<Float>(std::ceil(paragraph->getHeight()))});

    return TextMeasurement{.size = size, .attachments = measureAttachments(attributedString, *paragraph)};
}

} // namespace

TextLayoutManager::TextLayoutManager(const std::shared_ptr<const ContextContainer>& /*contextContainer*/)
    : textMeasureCache_(kSimpleThreadSafeCacheSizeCap) {}

TextMeasurement TextLayoutManager::measure(const AttributedStringBox& attributedStringBox,
                                           const ParagraphAttributes& paragraphAttributes,
                                           const TextLayoutContext& layoutContext,
                                           const LayoutConstraints& layoutConstraints) const {
    const TextMeasureCacheKey cacheKey{.attributedString = attributedStringBox.getValue(),
                                       .paragraphAttributes = paragraphAttributes,
                                       .layoutConstraints = layoutConstraints,
                                       .pointScaleFactor = layoutContext.pointScaleFactor};

    return textMeasureCache_.get(cacheKey, [&]() {
        return measureWithSkParagraph(attributedStringBox.getValue(), paragraphAttributes, layoutConstraints);
    });
}

} // namespace facebook::react
