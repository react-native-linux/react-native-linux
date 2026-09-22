#include "ParagraphAttributesSignature.h"

#include <gtest/gtest.h>
#include <string>
#include <utility>

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>

namespace {

using facebook::react::AttributedString;
using facebook::react::ParagraphAttributes;
using react_native_linux::toAttributesSignature;

AttributedString::Fragment textFragment(const std::string& text, float fontSize, bool bold) {
    AttributedString::Fragment fragment;

    fragment.string = text;
    fragment.textAttributes.fontSize = fontSize;

    if (bold) {
        fragment.textAttributes.fontWeight = facebook::react::FontWeight::Bold;
    }

    return fragment;
}

std::string signatureOf(const AttributedString& attributedString) {
    const ParagraphAttributes paragraphAttributes{};

    return toAttributesSignature(attributedString, paragraphAttributes);
}

/**
 * The collision CodeRabbit found on #481: `["ab" bold, "c" plain]` and `["a" bold, "bc" plain]` join to the same
 * text and, before the fix, produced the same signature — one cached measurement served both, and one of them
 * was laid out with the other's style boundaries.
 */
TEST(ParagraphAttributesSignatureTest, TheSameTextSplitDifferentlyDoesNotCollide) {
    AttributedString first;
    first.appendFragment(textFragment("ab", 14.0F, true));
    first.appendFragment(textFragment("c", 14.0F, false));

    AttributedString second;
    second.appendFragment(textFragment("a", 14.0F, true));
    second.appendFragment(textFragment("bc", 14.0F, false));

    EXPECT_EQ(first.getString(), second.getString());
    EXPECT_NE(signatureOf(first), signatureOf(second));
}

/** Every attribute that can move a glyph separates two otherwise identical paragraphs. */
TEST(ParagraphAttributesSignatureTest, EachMeasurementAttributeSeparatesTheSignature) {
    AttributedString base;
    base.appendFragment(textFragment("hello", 14.0F, false));

    AttributedString boldString;
    boldString.appendFragment(textFragment("hello", 14.0F, true));
    EXPECT_NE(signatureOf(base), signatureOf(boldString));

    AttributedString biggerString;
    biggerString.appendFragment(textFragment("hello", 18.0F, false));
    EXPECT_NE(signatureOf(base), signatureOf(biggerString));

    AttributedString trackedString;
    AttributedString::Fragment tracked = textFragment("hello", 14.0F, false);
    tracked.textAttributes.letterSpacing = 2.0F;
    trackedString.appendFragment(std::move(tracked));
    EXPECT_NE(signatureOf(base), signatureOf(trackedString));

    AttributedString familyString;
    AttributedString::Fragment family = textFragment("hello", 14.0F, false);
    family.textAttributes.fontFamily = "Noto Sans";
    familyString.appendFragment(std::move(family));
    EXPECT_NE(signatureOf(base), signatureOf(familyString));

    AttributedString scaledString;
    AttributedString::Fragment scaled = textFragment("hello", 14.0F, false);
    scaled.textAttributes.maxFontSizeMultiplier = 2.0F;
    scaledString.appendFragment(std::move(scaled));
    EXPECT_NE(signatureOf(base), signatureOf(scaledString));
}

/** The paragraph's own line limit and ellipsize mode are part of the key too. */
TEST(ParagraphAttributesSignatureTest, TheParagraphAttributesSeparateTheSignature) {
    AttributedString attributedString;
    attributedString.appendFragment(textFragment("hello", 14.0F, false));

    ParagraphAttributes oneLine{};
    oneLine.maximumNumberOfLines = 1;

    EXPECT_NE(toAttributesSignature(attributedString, ParagraphAttributes{}),
              toAttributesSignature(attributedString, oneLine));
}

/** Colour is deliberately absent: the metrics answer no colour question, so a re-tint reuses the measurement. */
TEST(ParagraphAttributesSignatureTest, ColourDoesNotSeparateTheSignature) {
    AttributedString plain;
    plain.appendFragment(textFragment("hello", 14.0F, false));

    AttributedString coloured;
    AttributedString::Fragment colouredFragment = textFragment("hello", 14.0F, false);
    colouredFragment.textAttributes.foregroundColor = facebook::react::SharedColor(0xFFFF0000);
    coloured.appendFragment(std::move(colouredFragment));

    EXPECT_EQ(signatureOf(plain), signatureOf(coloured));
}

/** An attachment's own box is measured, so its dimensions are part of the key. */
TEST(ParagraphAttributesSignatureTest, AnAttachmentsDimensionsSeparateTheSignature) {
    AttributedString small;
    AttributedString::Fragment smallAttachment;
    smallAttachment.string = AttributedString::Fragment::AttachmentCharacter();
    smallAttachment.parentShadowView.layoutMetrics.frame.size = facebook::react::Size{.width = 10.0F, .height = 20.0F};
    small.appendFragment(std::move(smallAttachment));

    AttributedString large;
    AttributedString::Fragment largeAttachment;
    largeAttachment.string = AttributedString::Fragment::AttachmentCharacter();
    largeAttachment.parentShadowView.layoutMetrics.frame.size = facebook::react::Size{.width = 30.0F, .height = 20.0F};
    large.appendFragment(std::move(largeAttachment));

    EXPECT_TRUE(small.getFragments()[0].isAttachment());
    EXPECT_NE(signatureOf(small), signatureOf(large));
}

/** Every optional attribute separates the signature whether it is absent or set. */
TEST(ParagraphAttributesSignatureTest, TheOptionalAttributesSeparateTheSignatureWhenSet) {
    AttributedString plain;
    plain.appendFragment(textFragment("hello", 14.0F, false));

    const std::string base = signatureOf(plain);

    AttributedString styled;
    AttributedString::Fragment fragment = textFragment("hello", 14.0F, false);
    fragment.textAttributes.fontStyle = facebook::react::FontStyle::Italic;
    fragment.textAttributes.fontVariant = facebook::react::FontVariant::SmallCaps;
    fragment.textAttributes.allowFontScaling = false;
    fragment.textAttributes.textTransform = facebook::react::TextTransform::Uppercase;
    fragment.textAttributes.alignment = facebook::react::TextAlignment::Center;
    fragment.textAttributes.baseWritingDirection = facebook::react::WritingDirection::RightToLeft;
    fragment.textAttributes.lineHeight = 20.0F;
    styled.appendFragment(std::move(fragment));

    EXPECT_NE(base, signatureOf(styled));
}

} // namespace
