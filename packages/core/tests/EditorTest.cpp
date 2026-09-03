#include "Clipboard.h"
#include "EditorModel.h"

#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <vector>

namespace {

using react_native_linux::CaretMotion;
using react_native_linux::clipboardText;
using react_native_linux::EditorModel;
using react_native_linux::EditorSelection;
using react_native_linux::followedScrollOffset;
using react_native_linux::segmentUtf8CodePoints;
using react_native_linux::setClipboardText;
using react_native_linux::TextSegments;
using react_native_linux::utf16LengthOfUtf8;
using react_native_linux::utf8OffsetForUtf16Index;

// "é" as U+00E9, two UTF-8 bytes, which is what makes a caret that steps by bytes visibly wrong.
constexpr char kEAcute[] = "\xC3\xA9";
// "e" followed by U+0301 COMBINING ACUTE ACCENT: one grapheme cluster, two code points, three bytes. ICU treats
// it as one caret step and the code-point fallback treats it as two, which is the documented difference.
constexpr char kCombiningEAcute[] = "e\xCC\x81";
// U+1F600 GRINNING FACE: four UTF-8 bytes and two UTF-16 code units, which is where `maxLength` and SkParagraph's
// index space disagree with everything else.
constexpr char kGrinningFace[] = "\xF0\x9F\x98\x80";

/**
 * A model holding `text` with the caret at the end, which is where a field that was just given a value puts it.
 */
EditorModel modelWith(const std::string& text) {
    EditorModel model;

    model.setText(text);
    model.setSelectionRange(text.size(), text.size());

    return model;
}

/**
 * A segmenter that treats every combining sequence as one grapheme, which is what ICU does and what the
 * code-point fallback cannot. It is a fixture rather than a reimplementation: the boundaries are given, and what
 * is under test is that the model steps through them.
 */
TextSegments graphemeAwareSegmenter(const std::string& text) {
    TextSegments segments = segmentUtf8CodePoints(text);
    std::vector<size_t> merged;

    for (const size_t start : segments.graphemeStarts) {
        const bool isCombiningMark = start + 1 < text.size() && static_cast<unsigned char>(text[start]) == 0xCCU;

        if (!isCombiningMark) {
            merged.push_back(start);
        }
    }

    segments.graphemeStarts = merged;

    return segments;
}

// Issue #53, case 3. A field is a window onto content allowed to be longer than it is, and the caret drags that
// window: this is the rule for both axes, so a single-line field's horizontal scroll and a multiline field's
// vertical one cannot drift apart. `boxLength` here is 100 and the content is 300.

TEST(FollowedScrollOffsetTest, ContentThatFitsIsNeverScrolled) {
    EXPECT_FLOAT_EQ(followedScrollOffset(0.0F, 10.0F, 20.0F, 100.0F, 60.0F), 0.0F);

    // Including when the window was somehow already moved: there is nowhere for it to be but the start.
    EXPECT_FLOAT_EQ(followedScrollOffset(40.0F, 10.0F, 20.0F, 100.0F, 60.0F), 0.0F);
}

TEST(FollowedScrollOffsetTest, ACaretInsideTheWindowLeavesItWhereItWas) {
    EXPECT_FLOAT_EQ(followedScrollOffset(50.0F, 60.0F, 70.0F, 100.0F, 300.0F), 50.0F);
}

TEST(FollowedScrollOffsetTest, ACaretPastTheFarEdgeBringsItBackByExactlyItsOwnOvershoot) {
    // The caret ends at 180 with a 100-point window at 50, so the window moves to 80 and not one point further.
    EXPECT_FLOAT_EQ(followedScrollOffset(50.0F, 170.0F, 180.0F, 100.0F, 300.0F), 80.0F);
}

TEST(FollowedScrollOffsetTest, ACaretBeforeTheNearEdgeMovesTheWindowBackToIt) {
    EXPECT_FLOAT_EQ(followedScrollOffset(120.0F, 40.0F, 50.0F, 100.0F, 300.0F), 40.0F);
}

TEST(FollowedScrollOffsetTest, TheWindowNeverPassesTheEndOfTheContent) {
    EXPECT_FLOAT_EQ(followedScrollOffset(0.0F, 295.0F, 305.0F, 100.0F, 300.0F), 200.0F);
}

TEST(EditorModelTest, InsertsAtTheCaretAndMovesItToTheEnd) {
    EditorModel model;

    EXPECT_TRUE(model.insertText("abc"));
    EXPECT_EQ(model.text(), "abc");
    EXPECT_EQ(model.selection().caretByte, 3U);
    EXPECT_FALSE(model.hasSelection());
    EXPECT_EQ(model.mostRecentEventCount(), 1);
}

TEST(EditorModelTest, InsertingNothingWithNoSelectionChangesNothing) {
    EditorModel model = modelWith("abc");

    EXPECT_FALSE(model.insertText({}));
    EXPECT_EQ(model.mostRecentEventCount(), 0);
}

TEST(EditorModelTest, InsertsInTheMiddleAfterMovingTheCaret) {
    EditorModel model = modelWith("ac");

    model.moveCaret(CaretMotion::LineStart, false);
    model.moveCaret(CaretMotion::Right, false);
    EXPECT_EQ(model.selection().caretByte, 1U);

    EXPECT_TRUE(model.insertText("b"));
    EXPECT_EQ(model.text(), "abc");
    EXPECT_EQ(model.selection().caretByte, 2U);
}

TEST(EditorModelTest, ReplacesTheSelectionWithTheInsertedText) {
    EditorModel model = modelWith("hello world");

    model.setSelectionRange(6, 11);

    EXPECT_TRUE(model.insertText("there"));
    EXPECT_EQ(model.text(), "hello there");
    EXPECT_FALSE(model.hasSelection());
}

TEST(EditorModelTest, DeletesBackwardsOneGraphemeAndStopsAtTheStart) {
    EditorModel model = modelWith(std::string("a") + kEAcute);

    EXPECT_TRUE(model.deleteBackward());
    EXPECT_EQ(model.text(), "a");
    EXPECT_TRUE(model.deleteBackward());
    EXPECT_EQ(model.text(), "");

    // react-native-macos#480: backspace past an empty buffer must be a no-op, not a crash.
    EXPECT_FALSE(model.deleteBackward());
    EXPECT_EQ(model.text(), "");
}

TEST(EditorModelTest, DeletesForwardsOneGraphemeAndStopsAtTheEnd) {
    EditorModel model = modelWith(std::string(kEAcute) + "b");

    model.moveCaret(CaretMotion::LineStart, false);

    EXPECT_TRUE(model.deleteForward());
    EXPECT_EQ(model.text(), "b");
    EXPECT_TRUE(model.deleteForward());
    EXPECT_EQ(model.text(), "");
    EXPECT_FALSE(model.deleteForward());
}

TEST(EditorModelTest, DeleteRemovesTheSelectionInsteadOfOneCharacter) {
    EditorModel model = modelWith("abcdef");

    model.setSelectionRange(1, 4);

    EXPECT_TRUE(model.deleteBackward());
    EXPECT_EQ(model.text(), "aef");

    model.setSelectionRange(1, 2);

    EXPECT_TRUE(model.deleteForward());
    EXPECT_EQ(model.text(), "af");
}

TEST(EditorModelTest, MovesByCodePointWithoutASegmenter) {
    EditorModel model = modelWith(kEAcute);

    EXPECT_TRUE(model.moveCaret(CaretMotion::Left, false));
    EXPECT_EQ(model.selection().caretByte, 0U);
    EXPECT_FALSE(model.moveCaret(CaretMotion::Left, false));

    EXPECT_TRUE(model.moveCaret(CaretMotion::Right, false));
    EXPECT_EQ(model.selection().caretByte, 2U);
    EXPECT_FALSE(model.moveCaret(CaretMotion::Right, false));
}

TEST(EditorModelTest, MovesOverACombiningSequenceInOneStepWithASegmenter) {
    EditorModel model;

    model.setSegmenter(graphemeAwareSegmenter);
    model.setText(kCombiningEAcute);
    model.moveCaret(CaretMotion::LineStart, false);

    EXPECT_TRUE(model.moveCaret(CaretMotion::Right, false));
    EXPECT_EQ(model.selection().caretByte, 3U);
}

TEST(EditorModelTest, ExtendsTheSelectionWithShiftAndCollapsesItWithoutShift) {
    EditorModel model = modelWith("abcd");

    EXPECT_TRUE(model.moveCaret(CaretMotion::Left, true));
    EXPECT_TRUE(model.moveCaret(CaretMotion::Left, true));
    EXPECT_EQ(model.selectionBeginByte(), 2U);
    EXPECT_EQ(model.selectionEndByte(), 4U);
    EXPECT_EQ(model.selectedText(), "cd");

    // A left arrow with a selection collapses to its start rather than stepping from the caret.
    EXPECT_TRUE(model.moveCaret(CaretMotion::Left, false));
    EXPECT_FALSE(model.hasSelection());
    EXPECT_EQ(model.selection().caretByte, 2U);
}

TEST(EditorModelTest, ARightArrowCollapsesASelectionToItsEnd) {
    EditorModel model = modelWith("abcd");

    model.setSelectionRange(1, 3);

    EXPECT_TRUE(model.moveCaret(CaretMotion::Right, false));
    EXPECT_EQ(model.selection().caretByte, 3U);
    EXPECT_FALSE(model.hasSelection());
}

TEST(EditorModelTest, ExtendingToWhereTheCaretAlreadyIsChangesNothing) {
    EditorModel model = modelWith("ab");

    EXPECT_FALSE(model.moveCaret(CaretMotion::Right, true));
    EXPECT_FALSE(model.moveCaret(CaretMotion::LineEnd, true));
}

TEST(EditorModelTest, MovesByWordAndStopsAtBothEnds) {
    EditorModel model = modelWith("hello brave world");

    EXPECT_TRUE(model.moveCaret(CaretMotion::WordLeft, false));
    EXPECT_EQ(model.selection().caretByte, 12U);
    EXPECT_TRUE(model.moveCaret(CaretMotion::WordLeft, false));
    EXPECT_EQ(model.selection().caretByte, 6U);
    EXPECT_TRUE(model.moveCaret(CaretMotion::WordLeft, false));
    EXPECT_EQ(model.selection().caretByte, 0U);
    EXPECT_FALSE(model.moveCaret(CaretMotion::WordLeft, false));

    EXPECT_TRUE(model.moveCaret(CaretMotion::WordRight, false));
    EXPECT_EQ(model.selection().caretByte, 5U);
    EXPECT_TRUE(model.moveCaret(CaretMotion::WordRight, false));
    EXPECT_EQ(model.selection().caretByte, 11U);
    EXPECT_TRUE(model.moveCaret(CaretMotion::WordRight, false));
    EXPECT_EQ(model.selection().caretByte, 17U);
    EXPECT_FALSE(model.moveCaret(CaretMotion::WordRight, false));
}

TEST(EditorModelTest, WordMotionInWhitespaceOnlyTextLandsAtTheEnds) {
    EditorModel model = modelWith("   ");

    EXPECT_TRUE(model.moveCaret(CaretMotion::WordLeft, false));
    EXPECT_EQ(model.selection().caretByte, 0U);
    EXPECT_TRUE(model.moveCaret(CaretMotion::WordRight, false));
    EXPECT_EQ(model.selection().caretByte, 3U);
}

TEST(EditorModelTest, HomeAndEndMoveWithinTheLineOfAMultilineField) {
    EditorModel model;

    model.setMultiline(true);
    model.setText("first\nsecond");
    model.setSelectionRange(12, 12);

    EXPECT_TRUE(model.moveCaret(CaretMotion::LineStart, false));
    EXPECT_EQ(model.selection().caretByte, 6U);
    EXPECT_FALSE(model.moveCaret(CaretMotion::LineStart, false));

    model.setSelectionRange(3, 3);

    EXPECT_TRUE(model.moveCaret(CaretMotion::LineEnd, false));
    EXPECT_EQ(model.selection().caretByte, 5U);
    EXPECT_TRUE(model.moveCaret(CaretMotion::LineStart, false));
    EXPECT_EQ(model.selection().caretByte, 0U);

    // End with a selection open collapses it even though the caret is already at the line's end.
    model.setSelectionRange(0, 5);

    EXPECT_TRUE(model.moveCaret(CaretMotion::LineEnd, false));
    EXPECT_FALSE(model.hasSelection());
    EXPECT_EQ(model.selection().caretByte, 5U);
}

TEST(EditorModelTest, SelectAllCoversTheWholeBufferAndDoesNothingWhenEmpty) {
    EditorModel model = modelWith("abc");

    EXPECT_TRUE(model.selectAll());
    EXPECT_EQ(model.selectedText(), "abc");
    EXPECT_FALSE(model.selectAll());

    EditorModel empty;

    EXPECT_FALSE(empty.selectAll());
}

TEST(EditorModelTest, ASelectionRangeIsClampedToTheBuffer) {
    EditorModel model;

    model.setText("abc");

    EXPECT_TRUE(model.setSelectionRange(99, 99));
    EXPECT_EQ(model.selection().anchorByte, 3U);
    EXPECT_EQ(model.selection().caretByte, 3U);
}

TEST(EditorModelTest, ASingleLineFieldCollapsesPastedNewlinesIntoSpaces) {
    EditorModel model;

    EXPECT_TRUE(model.insertText("one\r\ntwo\nthree\rfour"));
    EXPECT_EQ(model.text(), "one two three four");
}

TEST(EditorModelTest, ASingleLineFieldTurnsATrailingBareCarriageReturnIntoASpace) {
    EditorModel model;

    EXPECT_TRUE(model.insertText("a\r"));
    EXPECT_EQ(model.text(), "a ");
}

TEST(EditorModelTest, AMultilineFieldKeepsPastedNewlines) {
    EditorModel model;

    model.setMultiline(true);

    EXPECT_TRUE(model.insertText("one\ntwo"));
    EXPECT_EQ(model.text(), "one\ntwo");
}

TEST(EditorModelTest, MaxLengthTruncatesAnInsertionAndThenRefusesIt) {
    EditorModel model;

    model.setMaximumLength(5);

    EXPECT_TRUE(model.insertText("abcdefgh"));
    EXPECT_EQ(model.text(), "abcde");
    EXPECT_FALSE(model.insertText("i"));

    model.setSelectionRange(0, 2);

    EXPECT_TRUE(model.insertText("xy"));
    EXPECT_EQ(model.text(), "xycde");
}

TEST(EditorModelTest, MaxLengthCountsUtf16CodeUnitsSoAnEmojiCostsTwo) {
    EditorModel model;

    model.setMaximumLength(2);

    EXPECT_TRUE(model.insertText(std::string(kGrinningFace) + "a"));
    EXPECT_EQ(model.text(), kGrinningFace);
}

TEST(EditorModelTest, SecureTextEntryMasksEveryGraphemeAndNeverTheBuffer) {
    EditorModel model = modelWith("ab");

    model.setSecure(true);

    EXPECT_EQ(model.text(), "ab");
    EXPECT_EQ(model.displayText(), "\xE2\x80\xA2\xE2\x80\xA2");
    EXPECT_EQ(model.displayOffsetForByte(1), 3U);
    EXPECT_EQ(model.byteForDisplayOffset(3), 1U);
    EXPECT_EQ(model.byteForDisplayOffset(99), 2U);

    model.setSecure(false);

    EXPECT_EQ(model.displayText(), "ab");
    EXPECT_EQ(model.displayOffsetForByte(1), 1U);
    EXPECT_EQ(model.byteForDisplayOffset(1), 1U);
}

TEST(EditorModelTest, AnEmptySecureFieldMasksNothing) {
    EditorModel model;

    model.setSecure(true);

    EXPECT_EQ(model.displayText(), "");
}

TEST(EditorModelTest, APreeditIsInsertedAtTheCaretAndReplacedByTheNextOne) {
    EditorModel model = modelWith("ab");

    EXPECT_TRUE(model.applyPreedit("n", 1, 1));
    EXPECT_EQ(model.text(), "abn");
    EXPECT_TRUE(model.isComposing());
    EXPECT_EQ(model.compositionBeginByte(), 2U);
    EXPECT_EQ(model.compositionEndByte(), 3U);
    EXPECT_EQ(model.selection().caretByte, 3U);

    EXPECT_TRUE(model.applyPreedit("ni", 2, 2));
    EXPECT_EQ(model.text(), "abni");
    EXPECT_EQ(model.compositionEndByte(), 4U);

    // A pre-edit is not an edit: React's value has not changed until the commit arrives.
    EXPECT_EQ(model.mostRecentEventCount(), 0);
}

TEST(EditorModelTest, AHiddenPreeditCursorPutsTheCaretAtTheEndOfTheRun) {
    EditorModel model;

    EXPECT_TRUE(model.applyPreedit("nihao", -1, -1));
    EXPECT_EQ(model.selection().caretByte, 5U);
    EXPECT_FALSE(model.hasSelection());
}

TEST(EditorModelTest, APreeditCursorPairSelectsInsideTheComposingRun) {
    EditorModel model;

    EXPECT_TRUE(model.applyPreedit("nihao", 1, 3));
    EXPECT_EQ(model.selectionBeginByte(), 1U);
    EXPECT_EQ(model.selectionEndByte(), 3U);
}

TEST(EditorModelTest, APreeditCursorPastTheRunIsClampedToIt) {
    EditorModel model;

    EXPECT_TRUE(model.applyPreedit("ni", 99, 99));
    EXPECT_EQ(model.selection().caretByte, 2U);
}

TEST(EditorModelTest, AnEmptyPreeditEndsTheCompositionAndOtherwiseDoesNothing) {
    EditorModel model;

    EXPECT_FALSE(model.applyPreedit({}, 0, 0));

    EXPECT_TRUE(model.applyPreedit("ni", 2, 2));
    EXPECT_TRUE(model.applyPreedit({}, 0, 0));
    EXPECT_EQ(model.text(), "");
    EXPECT_FALSE(model.isComposing());
}

TEST(EditorModelTest, ACommitReplacesTheComposingRunAndCountsAsAnEdit) {
    EditorModel model;

    model.applyPreedit("nihao", 5, 5);

    EXPECT_TRUE(model.insertText("\xE4\xBD\xA0\xE5\xA5\xBD"));
    EXPECT_EQ(model.text(), "\xE4\xBD\xA0\xE5\xA5\xBD");
    EXPECT_FALSE(model.isComposing());
    EXPECT_EQ(model.mostRecentEventCount(), 1);
}

TEST(EditorModelTest, DeleteSurroundingRemovesBytesOnBothSidesOfTheCaret) {
    EditorModel model = modelWith("abcdef");

    model.setSelectionRange(3, 3);

    EXPECT_TRUE(model.deleteSurrounding(2, 1));
    EXPECT_EQ(model.text(), "aef");
    EXPECT_EQ(model.selection().caretByte, 1U);
    EXPECT_EQ(model.mostRecentEventCount(), 1);
}

TEST(EditorModelTest, DeleteSurroundingSnapsOutwardsToGraphemeBoundaries) {
    EditorModel model = modelWith(std::string(kEAcute) + kEAcute);

    model.setSelectionRange(2, 2);

    // One byte after the caret is half of a two-byte sequence; the deletion has to take the whole of it.
    EXPECT_TRUE(model.deleteSurrounding(1, 1));
    EXPECT_EQ(model.text(), "");
}

TEST(EditorModelTest, DeleteSurroundingOfNothingChangesNothing) {
    EditorModel model = modelWith("abc");

    model.setSelectionRange(0, 0);

    EXPECT_FALSE(model.deleteSurrounding(0, 0));
    EXPECT_EQ(model.mostRecentEventCount(), 0);
}

TEST(EditorModelTest, DeleteSurroundingOfNothingStillEndsAnOpenComposition) {
    EditorModel model;

    model.applyPreedit("ni", 2, 2);

    EXPECT_TRUE(model.deleteSurrounding(0, 0));
    EXPECT_EQ(model.text(), "");
    EXPECT_FALSE(model.isComposing());
    EXPECT_EQ(model.mostRecentEventCount(), 1);
}

TEST(EditorModelTest, ATypedKeyDuringACompositionReplacesTheComposingRun) {
    EditorModel model = modelWith("ab");

    model.applyPreedit("ni", 2, 2);

    EXPECT_TRUE(model.insertText("x"));
    EXPECT_EQ(model.text(), "abx");
    EXPECT_FALSE(model.isComposing());
}

TEST(EditorModelTest, AControlledValueIsAdoptedWhenTheEventCountMatches) {
    EditorModel model;

    EXPECT_TRUE(model.reconcileProps("from react", 0));
    EXPECT_EQ(model.text(), "from react");
    EXPECT_EQ(model.mostRecentEventCount(), 0);

    // Applying the same value twice is not a change, which is what stops the caret from jumping on every render.
    EXPECT_FALSE(model.reconcileProps("from react", 0));
}

TEST(EditorModelTest, AControlledValueIsIgnoredWhileAnEditIsInFlight) {
    EditorModel model;

    model.insertText("abc");

    EXPECT_FALSE(model.reconcileProps("stale", 0));
    EXPECT_EQ(model.text(), "abc");

    // react-native-macos#2066: once React has caught up, the override wins.
    EXPECT_TRUE(model.reconcileProps("fresh", model.mostRecentEventCount()));
    EXPECT_EQ(model.text(), "fresh");
}

TEST(EditorModelTest, AnEmptyControlledValueClearsTheField) {
    EditorModel model = modelWith("abc");

    model.setSelectionRange(3, 3);

    EXPECT_TRUE(model.reconcileProps({}, 0));
    EXPECT_EQ(model.text(), "");
    EXPECT_EQ(model.selection().caretByte, 0U);
}

TEST(EditorModelTest, ReplacingTheBufferClampsTheSelectionAndEndsAComposition) {
    EditorModel model = modelWith("abcdef");

    model.setSelectionRange(2, 6);

    EXPECT_TRUE(model.setText("ab"));
    EXPECT_EQ(model.selectionBeginByte(), 2U);
    EXPECT_EQ(model.selectionEndByte(), 2U);

    model.applyPreedit("ni", 2, 2);

    EXPECT_TRUE(model.setText("abni"));
    EXPECT_FALSE(model.isComposing());
}

TEST(EditorModelTest, ASegmenterThatOmitsTheEndsIsRepaired) {
    EditorModel model;

    model.setSegmenter([](const std::string& /*text*/) { return TextSegments{}; });
    model.setText("abc");
    model.setSelectionRange(3, 3);

    EXPECT_TRUE(model.moveCaret(CaretMotion::Left, false));
    EXPECT_EQ(model.selection().caretByte, 0U);
    EXPECT_TRUE(model.moveCaret(CaretMotion::Right, false));
    EXPECT_EQ(model.selection().caretByte, 3U);
}

TEST(EditorModelTest, AResegmentRepairsBoundariesMissingFromACustomSegmenter) {
    EditorModel model;

    model.setSegmenter(
        [](const std::string& /*text*/) { return TextSegments{.graphemeStarts = {1}, .wordStarts = {1}}; });
    model.setSecure(true);

    EXPECT_TRUE(model.insertText("ab"));
    EXPECT_EQ(model.text(), "ab");

    // Only reachable if the missing front boundary of `graphemeStarts` was repaired to 0: otherwise there is
    // nothing to count a grapheme between and this field would mask nothing.
    EXPECT_EQ(model.displayText(), "\xE2\x80\xA2\xE2\x80\xA2");

    EXPECT_TRUE(model.moveCaret(CaretMotion::WordLeft, false));
    EXPECT_EQ(model.selection().caretByte, 1U);
    EXPECT_TRUE(model.moveCaret(CaretMotion::WordRight, false));
    EXPECT_EQ(model.selection().caretByte, 2U);
}

TEST(SegmentUtf8CodePointsTest, ReportsCodePointStartsAndWhitespaceTransitions) {
    const TextSegments empty = segmentUtf8CodePoints({});

    EXPECT_EQ(empty.graphemeStarts, std::vector<size_t>{0U});
    EXPECT_EQ(empty.wordStarts, std::vector<size_t>{0U});

    const TextSegments segments = segmentUtf8CodePoints(std::string("a ") + kEAcute);

    EXPECT_EQ(segments.graphemeStarts, (std::vector<size_t>{0U, 1U, 2U, 4U}));
    EXPECT_EQ(segments.wordStarts, (std::vector<size_t>{0U, 1U, 2U, 4U}));
}

TEST(Utf16ConversionTest, CountsCodeUnitsAndInvertsBackToBytes) {
    const std::string text = std::string("a") + kGrinningFace + "b";

    EXPECT_EQ(utf16LengthOfUtf8(text, 0), 0U);
    EXPECT_EQ(utf16LengthOfUtf8(text, 1), 1U);
    EXPECT_EQ(utf16LengthOfUtf8(text, 5), 3U);
    EXPECT_EQ(utf16LengthOfUtf8(text, 999), 4U);

    EXPECT_EQ(utf8OffsetForUtf16Index(text, 0), 0U);
    EXPECT_EQ(utf8OffsetForUtf16Index(text, 1), 1U);
    EXPECT_EQ(utf8OffsetForUtf16Index(text, 3), 5U);
    EXPECT_EQ(utf8OffsetForUtf16Index(text, 999), text.size());
}

TEST(ClipboardTest, RoundTripsTheTextItWasGiven) {
    setClipboardText("copied");

    EXPECT_EQ(clipboardText(), "copied");

    setClipboardText({});

    EXPECT_EQ(clipboardText(), "");
}

#pragma mark - the parity matrix rows #53 leaves open (#53)

/**
 * Issue #53, case 6, the Windows half: clipboard text arrives CRLF, so a paste that left bare carriage returns in
 * the buffer would put react-native-macos#2303's garbage on every Windows-shaped paste. A multiline field keeps
 * line endings as bare newlines; a single-line field turns each pair into exactly one space.
 */
TEST(EditorModelTest, AMultilinePasteNormalizesWindowsLineEndings) {
    EditorModel model;

    model.setMultiline(true);
    model.insertText("a\r\nb\r\nc");

    EXPECT_EQ(model.text(), "a\nb\nc");
}

TEST(EditorModelTest, ASingleLinePasteTurnsAWindowsLineEndingIntoOneSpace) {
    EditorModel model;

    model.insertText("a\r\nb");

    EXPECT_EQ(model.text(), "a b");
}

/**
 * Issue #53, case 2, the whole of react-native-macos#2066 in one sequence: the user types uncontrolled, a stale
 * controlled update is ignored, the caught-up override wins exactly once, and after the user types again a
 * repeat of the old update still cannot clobber what the user wrote.
 */
TEST(EditorModelTest, TheControlledOverrideWinsOnceAndLaterTypingIsSafeFromStaleUpdates) {
    EditorModel model;

    model.insertText("abc");
    EXPECT_EQ(model.mostRecentEventCount(), 1);

    EXPECT_FALSE(model.reconcileProps("stale", 0));
    EXPECT_EQ(model.text(), "abc");

    EXPECT_TRUE(model.reconcileProps("fresh", 1));
    EXPECT_EQ(model.text(), "fresh");

    // The override keeps the caret where the user left it rather than jumping to the end - a controlled update
    // that moved the caret on every render is react-native-macos#2066 from the other side. It was 3 in "abc" and
    // clamps to 3 inside "fresh".
    EXPECT_EQ(model.selection().caretByte, 3U);

    model.insertText("d");
    EXPECT_EQ(model.text(), "fredsh");

    EXPECT_FALSE(model.reconcileProps("stale again", 1));
    EXPECT_EQ(model.text(), "fredsh");
}

/**
 * Issue #53, cases 1 and 7 composed: a controlled update into a secure field replaces the buffer the change
 * events carry and never the masked display, which is what makes react-native-macos#423 impossible.
 */
TEST(EditorModelTest, AControlledValueIntoASecureFieldKeepsTheBufferAndMasksTheDisplay) {
    EditorModel model;

    model.setSecure(true);

    EXPECT_TRUE(model.reconcileProps("secret", 0));

    EXPECT_EQ(model.text(), "secret");
    EXPECT_EQ(model.displayText(), "\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2\xE2\x80\xA2");
}

} // namespace
