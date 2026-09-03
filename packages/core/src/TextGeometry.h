#pragma once

#include "EditorModel.h"

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Rect.h>

#include <cstddef>
#include <string>
#include <vector>

namespace react_native_linux {

/**
 * The Skia-free declaration of everything a text field needs from SkParagraph that is not a draw call.
 *
 * The definitions live in `src/TextPipeline.cpp`, beside `layoutParagraph`, for the reason that function exists
 * at all: a caret measured against a second layout of the same string would eventually disagree with the
 * paragraph that was drawn, and a caret in the wrong place is the most visible bug a text field has —
 * react-native-macos#1395, #1921 and #2127 are three of them. The declaration is here rather than in
 * `TextPipeline.h` so that translation units without Skia's include path can call it, and it is guarded at each
 * call site by `RNL_ENABLE_TEXT_GEOMETRY`, which the build defines only when Skia is present. Without Skia every
 * paragraph already measures as zero, so a field has no geometry to be wrong about.
 *
 * Every offset crossing this boundary is a **UTF-16 index into the displayed string**, because that is the index
 * space SkParagraph's public API speaks. `utf16LengthOfUtf8` and `utf8OffsetForUtf16Index` in `EditorModel.h` are
 * the two conversions, and they are there because they are arithmetic and arithmetic belongs where the coverage
 * gate can see it. The displayed string is the masked one when `secureTextEntry` is set, so no unmasked
 * character reaches a paragraph even to be measured.
 */
struct EditorGeometryRequest {
    size_t caretUtf16{0};
    size_t selectionBeginUtf16{0};
    size_t selectionEndUtf16{0};
    size_t compositionBeginUtf16{0};
    size_t compositionEndUtf16{0};
    bool isMultiline{false};
};

/**
 * The rectangles a text field draws that its text does not: the caret, the selection highlight behind the text,
 * and the composing run's underline. All of them are relative to the paragraph's own origin, so the painter adds
 * the content-box origin and nothing else.
 *
 * `contentWidth` is the laid-out width of the longest line, which is what a single-line field scrolls by when the
 * caret leaves the visible box.
 *
 * `layoutWidth` is the width these rectangles were measured against, and the painter has to draw the paragraph
 * against the same one or the two disagree. It is **not** always the content box: a single-line field is laid out
 * unwrapped, because a single line that is longer than its box scrolls rather than wrapping. When the line does
 * fit, the box wins again, so `textAlign` still centres and right-aligns a field whose value fits inside it.
 */
struct EditorGeometry {
    facebook::react::Rect caret;
    std::vector<facebook::react::Rect> selection;
    std::vector<facebook::react::Rect> composition;
    float contentWidth{0.0F};
    float layoutWidth{0.0F};
};

EditorGeometry measureEditorGeometry(const facebook::react::AttributedString& attributedString,
                                     const facebook::react::ParagraphAttributes& paragraphAttributes,
                                     float maximumWidth, const EditorGeometryRequest& request);

/**
 * The UTF-16 index the given point in paragraph-local coordinates falls on, which is what a click or a drag
 * turns into a caret position.
 */
size_t utf16IndexAtPoint(const facebook::react::AttributedString& attributedString,
                         const facebook::react::ParagraphAttributes& paragraphAttributes, float maximumWidth,
                         facebook::react::Point localPoint);

/**
 * One laid-out paragraph reduced to the numbers a layout consumer can be wrong about: how many lines it broke
 * into, how tall each of them is, how wide the longest one is, and how tall the whole is.
 *
 * This is the paint side of issue #41 made observable. Measurement and painting call one `layoutParagraph`, so
 * they cannot shape a string differently; what they can differ in is the **width they call it with**, because
 * measurement is given Yoga's constraint and painting is given the frame Yoga assigned from the answer. A
 * paragraph that re-wraps at that second width is react-native-macos#2857 — a box measured for one paragraph and
 * painted with another.
 */
struct ParagraphLineMetrics {
    float width{0.0F};
    float height{0.0F};
};

struct ParagraphMetrics {
    std::vector<ParagraphLineMetrics> lines;
    float longestLineWidth{0.0F};
    float height{0.0F};
};

ParagraphMetrics measureParagraphMetrics(const facebook::react::AttributedString& attributedString,
                                         const facebook::react::ParagraphAttributes& paragraphAttributes,
                                         float maximumWidth);

/**
 * ICU's grapheme and word boundaries for one string, through the same SkUnicode instance the paragraph builder
 * uses. This is what makes a caret step over a combining sequence or an emoji ZWJ sequence in one press instead
 * of splitting it.
 */
TextSegments segmentText(const std::string& text);

} // namespace react_native_linux
