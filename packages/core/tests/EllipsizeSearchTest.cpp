#include "EllipsizeSearch.h"

#include <cstddef>
#include <functional>
#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

namespace {

using facebook::react::EllipsizeMode;
using react_native_linux::EllipsizeCandidate;
using react_native_linux::EllipsizePiece;
using react_native_linux::EllipsizePlan;
using react_native_linux::EllipsizeSide;
using react_native_linux::planEllipsize;
using react_native_linux::searchedEllipsizeSide;
using react_native_linux::searchEllipsizePlan;

constexpr char kEllipsis[] = "\xE2\x80\xA6";

/**
 * What the plan would draw: the kept pieces with the ellipsis between them. Every assertion below is on this
 * string, because "which characters survive a given width" is the whole of what the search decides.
 */
std::string composedText(const std::vector<std::string>& fragmentStrings, const EllipsizePlan& plan) {
    std::string composed;

    for (const EllipsizePiece& piece : plan.leadingPieces) {
        composed += fragmentStrings[piece.fragmentIndex].substr(piece.beginUtf8, piece.endUtf8 - piece.beginUtf8);
    }

    composed += kEllipsis;

    for (const EllipsizePiece& piece : plan.trailingPieces) {
        composed += fragmentStrings[piece.fragmentIndex].substr(piece.beginUtf8, piece.endUtf8 - piece.beginUtf8);
    }

    return composed;
}

/**
 * The stand-in for SkParagraph: one byte of text is one unit of width, and a plan fits when what it would draw
 * is no wider than the budget. It counts its own calls, because a search that measured every grapheme instead of
 * bisecting would still return the right answer and would be the thing that makes text slow.
 */
class FakeMeasurer {
public:
    FakeMeasurer(std::vector<std::string> fragmentStrings, size_t widthBudget)
        : fragmentStrings_(std::move(fragmentStrings)), widthBudget_(widthBudget) {}

    bool operator()(const EllipsizePlan& plan) {
        ++measurementCount_;

        return composedText(fragmentStrings_, plan).size() <= widthBudget_;
    }

    size_t measurementCount() const { return measurementCount_; }

private:
    std::vector<std::string> fragmentStrings_;
    size_t widthBudget_;
    size_t measurementCount_{0};
};

/**
 * The plan a box of `widthBudget` units would truncate to. Every case but the one about the bisection itself
 * goes through this, because how the measurer is wired is not what any of them are about.
 */
EllipsizePlan searchWithin(EllipsizeSide side, const std::vector<std::string>& fragmentStrings,
                           const std::vector<size_t>& graphemeStarts, size_t widthBudget) {
    FakeMeasurer measurer{fragmentStrings, widthBudget};

    return searchEllipsizePlan(side, fragmentStrings, graphemeStarts, std::ref(measurer));
}

std::vector<size_t> asciiGraphemeStarts(size_t length) {
    std::vector<size_t> starts;

    for (size_t index = 0; index <= length; ++index) {
        starts.push_back(index);
    }

    return starts;
}

/**
 * A paragraph the search is allowed to rebuild: one line, no attachment, not a field.
 */
EllipsizeCandidate truncatedParagraph(EllipsizeMode ellipsizeMode) {
    return EllipsizeCandidate{.ellipsizeMode = ellipsizeMode,
                              .maximumNumberOfLines = 1,
                              .hasInlineAttachment = false,
                              .isEditorField = false};
}

TEST(EllipsizeSearchTest, HeadAndMiddleAreTheModesTheSearchAnswers) {
    EXPECT_EQ(searchedEllipsizeSide(truncatedParagraph(EllipsizeMode::Head)), EllipsizeSide::Head);
    EXPECT_EQ(searchedEllipsizeSide(truncatedParagraph(EllipsizeMode::Middle)), EllipsizeSide::Middle);
}

TEST(EllipsizeSearchTest, TailAndClipAreLeftToSkParagraphAndToTheLineLimit) {
    EXPECT_FALSE(searchedEllipsizeSide(truncatedParagraph(EllipsizeMode::Tail)).has_value());
    EXPECT_FALSE(searchedEllipsizeSide(truncatedParagraph(EllipsizeMode::Clip)).has_value());
}

TEST(EllipsizeSearchTest, WithNoLineLimitThereIsNothingToTruncateTo) {
    EllipsizeCandidate candidate = truncatedParagraph(EllipsizeMode::Head);

    candidate.maximumNumberOfLines = 0;

    EXPECT_FALSE(searchedEllipsizeSide(candidate).has_value());
}

TEST(EllipsizeSearchTest, AParagraphWithAnInlineAttachmentIsNeverRebuilt) {
    EllipsizeCandidate candidate = truncatedParagraph(EllipsizeMode::Middle);

    candidate.hasInlineAttachment = true;

    EXPECT_FALSE(searchedEllipsizeSide(candidate).has_value());
}

// A <TextInput> is a window onto its whole text: its caret, selection, composing run and hit testing are UTF-16
// offsets into the string React gave us, and a searched cut rebuilds that string, so every one of those offsets
// would address a different character than the one on screen. A field scrolls instead of truncating, so it
// measures and hit-tests its full text whatever `ellipsizeMode` and `numberOfLines` say.
TEST(EllipsizeSearchTest, AnEditorFieldIsNeverTruncatedByTheSearchWhateverItsModeSays) {
    EllipsizeCandidate field = truncatedParagraph(EllipsizeMode::Head);

    field.isEditorField = true;

    EXPECT_FALSE(searchedEllipsizeSide(field).has_value());

    field.ellipsizeMode = EllipsizeMode::Middle;

    EXPECT_FALSE(searchedEllipsizeSide(field).has_value());
}

TEST(EllipsizeSearchTest, HeadKeepsTheEndOfTheTextBehindTheEllipsis) {
    const std::vector<std::string> fragments{"abcdefghij"};
    const EllipsizePlan plan = searchWithin(EllipsizeSide::Head, fragments, asciiGraphemeStarts(10), 8);

    EXPECT_EQ(composedText(fragments, plan), std::string{kEllipsis} + "fghij");
    EXPECT_TRUE(plan.leadingPieces.empty());
}

TEST(EllipsizeSearchTest, MiddleKeepsBothEndsAndGivesTheOddGraphemeToTheFront) {
    const std::vector<std::string> fragments{"abcdefghij"};
    const EllipsizePlan plan = searchWithin(EllipsizeSide::Middle, fragments, asciiGraphemeStarts(10), 8);

    EXPECT_EQ(composedText(fragments, plan), "abc" + std::string{kEllipsis} + "ij");
}

TEST(EllipsizeSearchTest, TheSearchBisectsRatherThanWalkingTheText) {
    const std::vector<std::string> fragments{std::string(1000, 'a')};
    FakeMeasurer measurer{fragments, 103};

    const EllipsizePlan plan =
        searchEllipsizePlan(EllipsizeSide::Head, fragments, asciiGraphemeStarts(1000), std::ref(measurer));

    EXPECT_EQ(composedText(fragments, plan), std::string{kEllipsis} + std::string(100, 'a'));
    EXPECT_LE(measurer.measurementCount(), 10U);
}

TEST(EllipsizeSearchTest, ACutNeverSplitsAMultiByteGraphemeToUseUpTheLastBytesOfTheBudget) {
    // "a👍b👍c": 11 bytes, five graphemes, each thumbs-up four bytes wide. A budget of eight leaves five bytes
    // for the text, which is exactly "👍c" — a search cutting on bytes would take the last five bytes of the
    // string instead and split the second thumbs-up down the middle.
    const std::vector<std::string> fragments{"a\xF0\x9F\x91\x8D"
                                             "b\xF0\x9F\x91\x8D"
                                             "c"};
    const std::vector<size_t> graphemeStarts{0, 1, 5, 6, 10, 11};
    const EllipsizePlan plan = searchWithin(EllipsizeSide::Head, fragments, graphemeStarts, 8);

    EXPECT_EQ(composedText(fragments, plan), std::string{kEllipsis} + "\xF0\x9F\x91\x8D"
                                                                      "c");
    ASSERT_EQ(plan.trailingPieces.size(), 1U);
    EXPECT_EQ(plan.trailingPieces.front().beginUtf8, 6U);
}

TEST(EllipsizeSearchTest, MiddleCutsBothEndsOnGraphemeBoundariesToo) {
    const std::vector<std::string> fragments{"a\xF0\x9F\x91\x8D"
                                             "b\xF0\x9F\x91\x8D"
                                             "c"};
    const std::vector<size_t> graphemeStarts{0, 1, 5, 6, 10, 11};
    const EllipsizePlan plan = searchWithin(EllipsizeSide::Middle, fragments, graphemeStarts, 6);

    EXPECT_EQ(composedText(fragments, plan), "a" + std::string{kEllipsis} + "c");
}

TEST(EllipsizeSearchTest, PiecesCarryTheFragmentTheyWereCutFromAndOffsetsWithinIt) {
    const std::vector<std::string> fragments{"one ", "two ", "three"};
    const EllipsizePlan plan = searchWithin(EllipsizeSide::Middle, fragments, asciiGraphemeStarts(13), 9);

    EXPECT_EQ(composedText(fragments, plan), "one" + std::string{kEllipsis} + "ree");
    ASSERT_EQ(plan.leadingPieces.size(), 1U);
    EXPECT_EQ(plan.leadingPieces.front().fragmentIndex, 0U);
    EXPECT_EQ(plan.leadingPieces.front().beginUtf8, 0U);
    EXPECT_EQ(plan.leadingPieces.front().endUtf8, 3U);
    ASSERT_EQ(plan.trailingPieces.size(), 1U);
    EXPECT_EQ(plan.trailingPieces.front().fragmentIndex, 2U);
    EXPECT_EQ(plan.trailingPieces.front().beginUtf8, 2U);
    EXPECT_EQ(plan.trailingPieces.front().endUtf8, 5U);
}

TEST(EllipsizeSearchTest, AKeptSpanThatCrossesAFragmentBoundaryBecomesOnePiecePerFragment) {
    const std::vector<std::string> fragments{"one ", "two ", "three"};
    const EllipsizePlan plan = planEllipsize(EllipsizeSide::Head, fragments, asciiGraphemeStarts(13), 7);

    EXPECT_EQ(composedText(fragments, plan), std::string{kEllipsis} + "o three");
    ASSERT_EQ(plan.trailingPieces.size(), 2U);
    EXPECT_EQ(plan.trailingPieces.front().fragmentIndex, 1U);
    EXPECT_EQ(plan.trailingPieces.front().beginUtf8, 2U);
    EXPECT_EQ(plan.trailingPieces.back().fragmentIndex, 2U);
    EXPECT_EQ(plan.trailingPieces.back().endUtf8, 5U);
}

TEST(EllipsizeSearchTest, TheEllipsisBelongsToTheFragmentHoldingTheFirstRemovedByte) {
    const std::vector<std::string> fragments{"one ", "two ", "three"};

    EXPECT_EQ(planEllipsize(EllipsizeSide::Head, fragments, asciiGraphemeStarts(13), 7).ellipsisFragmentIndex, 0U);
    // Middle keeps four leading graphemes — the whole first fragment — so the first byte it removes is the
    // second fragment's, and the ellipsis is drawn in the second fragment's style.
    EXPECT_EQ(planEllipsize(EllipsizeSide::Middle, fragments, asciiGraphemeStarts(13), 7).ellipsisFragmentIndex, 1U);
}

TEST(EllipsizeSearchTest, TheSearchIsOverTheLogicalOrderOfTheTextAndNotItsVisualOrder) {
    // "שלום world" — the Hebrew word is four two-byte letters, and `head` removes the ones that come first in
    // memory, which is the right-hand end of the run on screen. Bidi reordering is SkParagraph's, and the
    // paragraph direction is hardcoded left-to-right; see *Fidelity limits* in docs/cpp-toolchain.md.
    const std::vector<std::string> fragments{"\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D world"};
    const std::vector<size_t> graphemeStarts{0, 2, 4, 6, 8, 9, 10, 11, 12, 13, 14};
    const EllipsizePlan plan = searchWithin(EllipsizeSide::Head, fragments, graphemeStarts, 8);

    EXPECT_EQ(composedText(fragments, plan), std::string{kEllipsis} + "world");
}

TEST(EllipsizeSearchTest, ABoxTooNarrowForOneGraphemeKeepsTheEllipsisAndNothingElse) {
    const std::vector<std::string> fragments{"abcdefghij"};
    const EllipsizePlan plan = searchWithin(EllipsizeSide::Middle, fragments, asciiGraphemeStarts(10), 3);

    EXPECT_EQ(composedText(fragments, plan), kEllipsis);
    EXPECT_TRUE(plan.leadingPieces.empty());
    EXPECT_TRUE(plan.trailingPieces.empty());
}

TEST(EllipsizeSearchTest, AWidthEverythingFitsInKeepsEveryGrapheme) {
    const std::vector<std::string> fragments{"abcdefghij"};
    const EllipsizePlan plan = searchWithin(EllipsizeSide::Head, fragments, asciiGraphemeStarts(10), 100);

    EXPECT_EQ(composedText(fragments, plan), std::string{kEllipsis} + "abcdefghij");
}

TEST(EllipsizeSearchTest, MoreGraphemesThanTheTextHasAreClampedToTheWholeText) {
    const std::vector<std::string> fragments{"abcdefghij"};
    const EllipsizePlan plan = planEllipsize(EllipsizeSide::Middle, fragments, asciiGraphemeStarts(10), 99);

    EXPECT_EQ(composedText(fragments, plan), "abcde" + std::string{kEllipsis} + "fghij");
}

TEST(EllipsizeSearchTest, AnEmptyTextIsAPlanWithNoPiecesAndIsNeverMeasured) {
    const std::vector<std::string> fragments{};
    FakeMeasurer measurer{fragments, 100};

    const EllipsizePlan plan =
        searchEllipsizePlan(EllipsizeSide::Head, fragments, std::vector<size_t>{0}, std::ref(measurer));

    EXPECT_EQ(composedText(fragments, plan), kEllipsis);
    EXPECT_EQ(plan.ellipsisFragmentIndex, 0U);
    EXPECT_EQ(measurer.measurementCount(), 0U);
}

TEST(EllipsizeSearchTest, TextWithNoFragmentsBehindItStillNamesAFragmentForTheEllipsis) {
    const EllipsizePlan plan = planEllipsize(EllipsizeSide::Head, {}, asciiGraphemeStarts(4), 2);

    EXPECT_EQ(plan.ellipsisFragmentIndex, 0U);
    EXPECT_TRUE(plan.trailingPieces.empty());
}

} // namespace
