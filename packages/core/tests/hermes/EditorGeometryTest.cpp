#include "TextGeometry.h"

#include <gtest/gtest.h>

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>

namespace react_native_linux {
namespace {

constexpr float kBoxWidth = 320.0F;

// #632: SkParagraph::getLongestLine is the lowest float for a paragraph with no lines, and an empty field's
// onContentSizeChange reported it as its width.
TEST(EditorGeometryTest, AnEmptyFieldHasNoContentWidth) {
    facebook::react::AttributedString attributedString;
    facebook::react::AttributedString::Fragment fragment;

    fragment.textAttributes = facebook::react::TextAttributes::defaultTextAttributes();
    fragment.textAttributes.fontSize = 18;
    attributedString.appendFragment(std::move(fragment));

    for (const bool isMultiline : {false, true}) {
        const EditorGeometry geometry =
            measureEditorGeometry(attributedString, facebook::react::ParagraphAttributes{}, kBoxWidth,
                                  EditorGeometryRequest{.isMultiline = isMultiline});

        EXPECT_EQ(geometry.contentWidth, 0.0F) << "multiline=" << isMultiline;
    }
}

} // namespace
} // namespace react_native_linux
