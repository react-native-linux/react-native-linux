#include "TextGeometry.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <gtest/gtest.h>
#include <string>
#include <vector>

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>

namespace react_native_linux {
namespace {

constexpr float kWrapWidth = 120.0F;

facebook::react::AttributedString paragraphOf(const std::string& text,
                                              facebook::react::WritingDirection writingDirection) {
    facebook::react::AttributedString attributedString;
    facebook::react::AttributedString::Fragment fragment;

    fragment.string = text;
    fragment.textAttributes = facebook::react::TextAttributes::defaultTextAttributes();
    fragment.textAttributes.fontSize = 16;
    fragment.textAttributes.baseWritingDirection = writingDirection;
    attributedString.appendFragment(std::move(fragment));

    return attributedString;
}

/** The code points of `text`, BMP-only like every fixture here, so one per UTF-16 offset. */
std::vector<char32_t> codePointsOf(const std::string& text) {
    std::vector<char32_t> codePoints;

    for (size_t index = 0; index < text.size();) {
        const auto lead = static_cast<unsigned char>(text[index]);
        const size_t length = lead < 0x80U ? 1 : (lead < 0xE0U ? 2 : 3);
        char32_t codePoint = length == 1 ? lead : (lead & (length == 2 ? 0x1FU : 0x0FU));

        for (size_t continuation = 1; continuation < length; ++continuation) {
            codePoint = (codePoint << 6U) | (static_cast<unsigned char>(text[index + continuation]) & 0x3FU);
        }

        codePoints.push_back(codePoint);
        index += length;
    }

    return codePoints;
}

enum class Strength : uint8_t { RightToLeft, LeftToRight, Neutral };

/**
 * A strong character's own direction, which fixes the edge it starts at independently of anything this platform
 * computes: a Hebrew letter starts at its right edge, a Latin letter or a digit at its left. Spaces and punctuation
 * take the direction of their context, so they are not asked about.
 */
Strength strengthOf(char32_t codePoint) {
    if (codePoint >= 0x05D0U && codePoint <= 0x05EAU) {
        return Strength::RightToLeft;
    }

    const bool isLatinLetterOrDigit = (codePoint >= U'a' && codePoint <= U'z') ||
                                      (codePoint >= U'A' && codePoint <= U'Z') ||
                                      (codePoint >= U'0' && codePoint <= U'9');

    return isLatinLetterOrDigit ? Strength::LeftToRight : Strength::Neutral;
}

/**
 * Every strong character that disagrees, as `offset:caretX/edgeX->hit`. Two things are asked of each, and the
 * character's box comes from the selection geometry while the edge it starts at comes from its script — the right
 * edge for a Hebrew letter, the left for a Latin letter or a digit — so neither answer is taken from the code under
 * test:
 *
 * - a point a quarter of the way in from that edge hit-tests back to the character's own offset (#72 item 3);
 * - the caret before the character sits at that edge (#72 item 4, #343's position-to-index round trip).
 */
std::vector<std::string> mismatchesOf(const std::string& text, facebook::react::WritingDirection writingDirection) {
    const facebook::react::AttributedString attributedString = paragraphOf(text, writingDirection);
    const facebook::react::ParagraphAttributes paragraphAttributes;
    const std::vector<char32_t> codePoints = codePointsOf(text);
    std::vector<std::string> mismatches;

    for (size_t offset = 0; offset < codePoints.size(); ++offset) {
        const Strength strength = strengthOf(codePoints[offset]);

        if (strength == Strength::Neutral) {
            continue;
        }

        const EditorGeometry geometry = measureEditorGeometry(attributedString, paragraphAttributes, kWrapWidth,
                                                              EditorGeometryRequest{.caretUtf16 = offset,
                                                                                    .selectionBeginUtf16 = offset,
                                                                                    .selectionEndUtf16 = offset + 1,
                                                                                    .isMultiline = true});
        const facebook::react::Rect& glyph = geometry.selection.front();
        const bool isRightToLeft = strength == Strength::RightToLeft;
        const float leadingEdge = isRightToLeft ? glyph.origin.x + glyph.size.width : glyph.origin.x;
        const facebook::react::Point probe{.x =
                                               leadingEdge + ((isRightToLeft ? -1.0F : 1.0F) * glyph.size.width / 4.0F),
                                           .y = glyph.origin.y + (glyph.size.height / 2.0F)};
        const size_t hit = utf16IndexAtPoint(attributedString, paragraphAttributes, kWrapWidth, probe);

        if (hit != offset || std::abs(geometry.caret.origin.x - leadingEdge) > 1.0F) {
            mismatches.push_back(std::to_string(offset) + ":" + std::to_string(geometry.caret.origin.x) + "/" +
                                 std::to_string(leadingEdge) + "->" + std::to_string(hit));
        }
    }

    return mismatches;
}

std::string joined(const std::vector<std::string>& parts) {
    std::string result;

    for (const std::string& part : parts) {
        result += part + " ";
    }

    return result;
}

// Issue #72, item 3, react-native-windows#7792: a point on a character of wrapped bidirectional text maps back to
// that character's logical offset, the trailing character of every wrapped line included.
/** How many lines the fixture breaks into, so a test that means "wrapped" can say so rather than assume it. */
size_t lineCountOf(const std::string& text) {
    const facebook::react::AttributedString attributedString =
        paragraphOf(text, facebook::react::WritingDirection::RightToLeft);

    return measureParagraphMetrics(attributedString, facebook::react::ParagraphAttributes{}, kWrapWidth).lines.size();
}

/** The fixture wraps onto at least three lines, and every strong character of it round-trips with its caret. */
void expectWrappedTextHitTestsBack(const std::string& text) {
    const std::vector<std::string> mismatches = mismatchesOf(text, facebook::react::WritingDirection::RightToLeft);

    EXPECT_GE(lineCountOf(text), 3U);
    EXPECT_TRUE(mismatches.empty()) << joined(mismatches);
}

TEST(BidiHitTestTest, EveryCharacterOfWrappedRightToLeftTextHitTestsBackToItsOffset) {
    expectWrappedTextHitTestsBack("שלום עולם, זהו טקסט שנשבר בתוך תיבה צרה מאוד.");
}

TEST(BidiHitTestTest, EveryCharacterOfWrappedMixedDirectionTextHitTestsBackToItsOffset) {
    expectWrappedTextHitTestsBack("גרסה 3.1 של React עובדת היטב today, וגם מחר.");
}

} // namespace
} // namespace react_native_linux
