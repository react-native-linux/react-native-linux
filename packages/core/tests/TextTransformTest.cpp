#include "TextTransform.h"

#include <react/renderer/attributedstring/primitives.h>

#include <gtest/gtest.h>

namespace {

using facebook::react::TextTransform;
using react_native_linux::applyTextTransform;

TEST(TextTransformTest, NoneAndUnsetLeaveTheStringExactlyAsReactSentIt) {
    EXPECT_EQ(applyTextTransform("Mixed Case café", TextTransform::None), "Mixed Case café");
    EXPECT_EQ(applyTextTransform("Mixed Case café", TextTransform::Unset), "Mixed Case café");
}

TEST(TextTransformTest, UppercaseCoversAsciiAndTheLatin1SupplementLettersTheVendoredFontCarries) {
    EXPECT_EQ(applyTextTransform("hello", TextTransform::Uppercase), "HELLO");
    EXPECT_EQ(applyTextTransform("café", TextTransform::Uppercase), "CAFÉ");
    EXPECT_EQ(applyTextTransform("naïve über", TextTransform::Uppercase), "NAÏVE ÜBER");
}

TEST(TextTransformTest, LowercaseCoversAsciiAndTheLatin1SupplementLettersTheVendoredFontCarries) {
    EXPECT_EQ(applyTextTransform("HELLO", TextTransform::Lowercase), "hello");
    EXPECT_EQ(applyTextTransform("CAFÉ", TextTransform::Lowercase), "café");
    EXPECT_EQ(applyTextTransform("NAÏVE ÜBER", TextTransform::Lowercase), "naïve über");
}

TEST(TextTransformTest, CaseMappingLeavesTheMultiplicationAndDivisionSignsAlone) {
    // U+00D7 (×) sits inside the range `Lowercase` would otherwise read as an uppercase Latin-1 letter, and
    // U+00F7 (÷) sits inside the range `Uppercase` would otherwise read as a lowercase one; neither is a letter.
    EXPECT_EQ(applyTextTransform("3×4", TextTransform::Lowercase), "3×4");
    EXPECT_EQ(applyTextTransform("8÷2", TextTransform::Uppercase), "8÷2");
}

TEST(TextTransformTest, CaseMappingLeavesAsciiPunctuationJustOutsideTheLetterRangesAlone) {
    // '{' (0x7B) is just past 'z' and '[' (0x5B) is just past 'Z' — neither is a letter, and both sit right at
    // the edge of the ASCII ranges `mapCase` checks.
    EXPECT_EQ(applyTextTransform("a{b", TextTransform::Uppercase), "A{B");
    EXPECT_EQ(applyTextTransform("A[B", TextTransform::Lowercase), "a[b");
}

TEST(TextTransformTest, CaseMappingLeavesYWithDiaeresisAloneBecauseItHasNoLatin1UppercasePair) {
    // U+00FF ("ÿ") is the one Latin-1 Supplement lowercase letter with no matching uppercase codepoint in the
    // same block (its uppercase, U+0178, is Latin Extended-A) — the boundary the `<= 0xDE` check above draws.
    EXPECT_EQ(applyTextTransform("naïve ÿ", TextTransform::Uppercase), "NAÏVE ÿ");
}

TEST(TextTransformTest, CapitalizeTreatsEveryAsciiWhitespaceCharacterAsAWordBoundary) {
    EXPECT_EQ(applyTextTransform("a\tb\nc\vd\fe\rf", TextTransform::Capitalize), "A\tB\nC\vD\fE\rF");
}

TEST(TextTransformTest, CaseMappingLeavesATwoByteSequenceOutsideTheLatin1SupplementAlone) {
    // Cyrillic sits in the same two-byte width as the Latin-1 Supplement but under a different lead byte
    // (U+0416, "Ж", is 0xD0 0x96), so this is the two-byte branch the Latin-1 tests above cannot reach.
    EXPECT_EQ(applyTextTransform("Ж", TextTransform::Lowercase), "Ж");
    EXPECT_EQ(applyTextTransform("ж", TextTransform::Uppercase), "ж");
}

TEST(TextTransformTest, CaseMappingCopiesThreeAndFourByteSequencesThroughUnchanged) {
    // U+20AC ("€") is three bytes and U+1F600 ("😀") is four; both are outside every range this function maps,
    // so both prove the transform never corrupts a multi-byte sequence it does not understand.
    EXPECT_EQ(applyTextTransform("10€", TextTransform::Uppercase), "10€");
    EXPECT_EQ(applyTextTransform("😀 hello", TextTransform::Uppercase), "😀 HELLO");
}

TEST(TextTransformTest, MalformedUtf8MakesProgressOneByteAtATimeInsteadOfLoopingForever) {
    // 0xFF cannot lead any valid UTF-8 sequence. `applyTextTransform` still has to terminate and still has to
    // leave whatever it does not understand alone, on genuinely malformed input as much as on well-formed input
    // outside its mapped ranges.
    const std::string malformed = "a\xFFz";

    EXPECT_EQ(applyTextTransform(malformed, TextTransform::Uppercase), "A\xFFZ");
}

TEST(TextTransformTest, CapitalizeUppercasesTheFirstLetterOfEveryWhitespaceSeparatedWord) {
    EXPECT_EQ(applyTextTransform("the quick fox", TextTransform::Capitalize), "The Quick Fox");
    EXPECT_EQ(applyTextTransform("  leading space", TextTransform::Capitalize), "  Leading Space");
    EXPECT_EQ(applyTextTransform("café au lait", TextTransform::Capitalize), "Café Au Lait");
}

// react/react-native#34117: a hyphen is not a word boundary for React Native's `capitalize`, unlike CSS's.
TEST(TextTransformTest, CapitalizeDoesNotTreatAHyphenAsAWordBoundary) {
    EXPECT_EQ(applyTextTransform("multi-word-value", TextTransform::Capitalize), "Multi-word-value");
}

// react/react-native#34117: React Native lowercases the rest of each word; CSS leaves an already-uppercase
// remainder alone. "I am PSYCHED" becomes "I Am Psyched", not "I Am PSYCHED".
TEST(TextTransformTest, CapitalizeLowercasesTheRestOfEachWordLikeReactNative) {
    EXPECT_EQ(applyTextTransform("i am PSYCHED", TextTransform::Capitalize), "I Am Psyched");
    EXPECT_EQ(applyTextTransform("MULTI-WORD-VALUE", TextTransform::Capitalize), "Multi-word-value");
}

TEST(TextTransformTest, CapitalizeOnAnEmptyOrAllWhitespaceStringChangesNothing) {
    EXPECT_EQ(applyTextTransform("", TextTransform::Capitalize), "");
    EXPECT_EQ(applyTextTransform("   ", TextTransform::Capitalize), "   ");
}

TEST(TextTransformTest, TextTransformAppliesBeforeAnyWrapDecisionByChangingTheStringItself) {
    // This is the arithmetic half of issue #250's "applied before wrap": the transformed string is what a wrap
    // decision downstream ever sees, because nothing after this call sees the original. Capitalize grows no
    // byte here, but uppercase can (a lowercase Latin-1 letter and its uppercase form are always the same UTF-8
    // width in the covered range, so growth is not from case mapping — this asserts the general contract that
    // the output is what gets measured, not a claim about length).
    const std::string original = "wraps at the same width";
    const std::string transformed = applyTextTransform(original, TextTransform::Uppercase);

    EXPECT_EQ(transformed, "WRAPS AT THE SAME WIDTH");
    EXPECT_EQ(transformed.size(), original.size());
}

} // namespace
