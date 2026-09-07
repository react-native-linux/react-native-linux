#include "TextInputSession.h"

#include "InputPipeline.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <string_view>
#include <vector>

namespace {

using react_native_linux::InputEvent;
using react_native_linux::InputEventKind;
using react_native_linux::TextInputContentPurpose;
using react_native_linux::textInputContentPurpose;
using react_native_linux::TextInputCursorRectangle;
using react_native_linux::TextInputSession;
using react_native_linux::TextInputSessionBatch;

constexpr int32_t kFieldAlpha = 11;
constexpr int32_t kFieldBeta = 22;
constexpr uint32_t kFirstCommit = 1;
constexpr uint32_t kSecondCommit = 2;
constexpr uint32_t kStaleSerial = 99;
constexpr char kPreeditText[] = "ni";
constexpr char kSurroundingText[] = "hello";
constexpr char kOtherSurroundingText[] = "hello there";

const TextInputCursorRectangle kCaret{.x = 4, .y = 8, .width = 2, .height = 16};
const TextInputCursorRectangle kMovedCaret{.x = 9, .y = 8, .width = 2, .height = 16};

/** A session with both focuses and one taken batch behind it: the state every enabled-session case starts from. */
TextInputSession makeEnabledSession() {
    TextInputSession session;

    session.enter();
    session.focusField(kFieldAlpha, TextInputContentPurpose::Normal);
    session.takeBatch();

    return session;
}

bool isEmpty(const TextInputSessionBatch& batch) {
    return !batch.disable && !batch.enable && !batch.commit && !batch.surroundingText.has_value() &&
           !batch.cursorRectangle.has_value() && batch.events.empty();
}

TEST(TextInputSessionTest, NeitherFocusAloneEnablesTheSession) {
    TextInputSession keyboardOnly;

    keyboardOnly.enter();

    EXPECT_TRUE(isEmpty(keyboardOnly.takeBatch()));
    EXPECT_FALSE(keyboardOnly.isEnabled());
    EXPECT_TRUE(keyboardOnly.hasKeyboardFocus());

    TextInputSession fieldOnly;

    fieldOnly.focusField(kFieldAlpha, TextInputContentPurpose::Normal);

    EXPECT_TRUE(isEmpty(fieldOnly.takeBatch()));
    EXPECT_FALSE(fieldOnly.isEnabled());
    EXPECT_FALSE(fieldOnly.hasKeyboardFocus());
}

TEST(TextInputSessionTest, BothFocusesEnableItWithTheFieldsContentType) {
    TextInputSession session;

    session.focusField(kFieldAlpha, TextInputContentPurpose::Password);
    session.enter();

    const TextInputSessionBatch batch = session.takeBatch();

    EXPECT_TRUE(batch.enable);
    EXPECT_FALSE(batch.disable);
    EXPECT_TRUE(batch.commit);
    ASSERT_TRUE(batch.contentPurpose.has_value());
    EXPECT_EQ(batch.contentPurpose.value(), TextInputContentPurpose::Password);
    EXPECT_TRUE(session.isEnabled());

    EXPECT_TRUE(isEmpty(session.takeBatch()));
}

TEST(TextInputSessionTest, FocusingANonFieldDisablesTheSessionTheWindowStillHas) {
    TextInputSession session = makeEnabledSession();

    session.blurField();

    const TextInputSessionBatch batch = session.takeBatch();

    EXPECT_TRUE(batch.disable);
    EXPECT_FALSE(batch.enable);
    EXPECT_FALSE(session.isEnabled());
    EXPECT_TRUE(session.hasKeyboardFocus());

    session.blurField();

    EXPECT_TRUE(isEmpty(session.takeBatch()));
}

TEST(TextInputSessionTest, TearingDownTakesTheCompositionOffTheScreenWithIt) {
    TextInputSession session = makeEnabledSession();

    session.recordPreeditString(kPreeditText, 2, 2);
    session.applyDone(kFirstCommit);
    session.blurField();

    const TextInputSessionBatch batch = session.takeBatch();

    ASSERT_EQ(batch.events.size(), 1U);
    EXPECT_EQ(batch.events[0].kind, InputEventKind::ImePreedit);
    EXPECT_TRUE(batch.events[0].text.empty());
}

TEST(TextInputSessionTest, TwoFieldsSwappedInOneFrameAreATeardownAndASetup) {
    TextInputSession session = makeEnabledSession();

    session.setSurroundingText(kSurroundingText, 5, 5);
    session.setCursorRectangle(kCaret);
    session.takeBatch();

    session.focusField(kFieldBeta, TextInputContentPurpose::Email);
    session.setSurroundingText(kOtherSurroundingText, 11, 11);

    const TextInputSessionBatch batch = session.takeBatch();

    EXPECT_TRUE(batch.disable);
    EXPECT_TRUE(batch.enable);
    EXPECT_TRUE(batch.commit);
    ASSERT_TRUE(batch.contentPurpose.has_value());
    EXPECT_EQ(batch.contentPurpose.value(), TextInputContentPurpose::Email);
    ASSERT_TRUE(batch.surroundingText.has_value());
    EXPECT_EQ(batch.surroundingText->text, kOtherSurroundingText);
    EXPECT_FALSE(batch.cursorRectangle.has_value());
}

TEST(TextInputSessionTest, TogglingSecureTextEntryUnderTheCaretIsANewContentType) {
    TextInputSession session = makeEnabledSession();

    session.focusField(kFieldAlpha, TextInputContentPurpose::Password);

    const TextInputSessionBatch batch = session.takeBatch();

    EXPECT_TRUE(batch.disable);
    EXPECT_TRUE(batch.enable);
    ASSERT_TRUE(batch.contentPurpose.has_value());
    EXPECT_EQ(batch.contentPurpose.value(), TextInputContentPurpose::Password);
}

TEST(TextInputSessionTest, FocusingTheSameFieldAgainChangesNothing) {
    TextInputSession session = makeEnabledSession();

    session.setSurroundingText(kSurroundingText, 5, 5);
    session.takeBatch();

    session.focusField(kFieldAlpha, TextInputContentPurpose::Normal);

    EXPECT_TRUE(isEmpty(session.takeBatch()));
}

TEST(TextInputSessionTest, AKeyboardLeaveEndsTheSessionWithoutARequestAndKeepsTheField) {
    TextInputSession session = makeEnabledSession();

    session.setSurroundingText(kSurroundingText, 5, 5);
    session.setCursorRectangle(kCaret);
    session.takeBatch();

    session.recordPreeditString(kPreeditText, 2, 2);
    session.applyDone(kSecondCommit);

    const std::vector<InputEvent> discarded = session.leave();

    ASSERT_EQ(discarded.size(), 1U);
    EXPECT_EQ(discarded[0].kind, InputEventKind::ImePreedit);
    EXPECT_TRUE(discarded[0].text.empty());
    EXPECT_FALSE(session.isEnabled());

    // No `disable`: the compositor already took the session away, and a request from a text input that has not
    // been sent `enter` is ignored.
    EXPECT_TRUE(isEmpty(session.takeBatch()));

    session.enter();

    const TextInputSessionBatch batch = session.takeBatch();

    EXPECT_TRUE(batch.enable);
    EXPECT_FALSE(batch.disable);
    ASSERT_TRUE(batch.surroundingText.has_value());
    EXPECT_EQ(batch.surroundingText->text, kSurroundingText);
    ASSERT_TRUE(batch.cursorRectangle.has_value());
    EXPECT_EQ(batch.cursorRectangle.value(), kCaret);
}

TEST(TextInputSessionTest, ALeaveBeforeTheAppBlursNeedsNoSecondTeardown) {
    TextInputSession session = makeEnabledSession();

    session.leave();
    session.blurField();

    EXPECT_TRUE(isEmpty(session.takeBatch()));

    session.enter();

    EXPECT_TRUE(isEmpty(session.takeBatch()));
}

TEST(TextInputSessionTest, OneFrameOfStateIsOneCommit) {
    TextInputSession session = makeEnabledSession();

    session.setSurroundingText(kSurroundingText, 5, 5);
    session.setCursorRectangle(kCaret);

    const TextInputSessionBatch batch = session.takeBatch();

    EXPECT_TRUE(batch.surroundingText.has_value());
    EXPECT_TRUE(batch.cursorRectangle.has_value());
    EXPECT_TRUE(batch.commit);
}

TEST(TextInputSessionTest, StateThatDidNotChangeIsNotSentAgain) {
    TextInputSession session = makeEnabledSession();

    session.setSurroundingText(kSurroundingText, 5, 5);
    session.setCursorRectangle(kCaret);
    session.takeBatch();

    session.setSurroundingText(kSurroundingText, 5, 5);
    session.setCursorRectangle(kCaret);

    EXPECT_TRUE(isEmpty(session.takeBatch()));

    session.setCursorRectangle(kMovedCaret);

    const TextInputSessionBatch moved = session.takeBatch();

    EXPECT_FALSE(moved.surroundingText.has_value());
    ASSERT_TRUE(moved.cursorRectangle.has_value());
    EXPECT_EQ(moved.cursorRectangle.value(), kMovedCaret);

    session.setSurroundingText(kOtherSurroundingText, 11, 11);

    const TextInputSessionBatch retyped = session.takeBatch();

    ASSERT_TRUE(retyped.surroundingText.has_value());
    EXPECT_EQ(retyped.surroundingText->cursor, 11);
    EXPECT_FALSE(retyped.cursorRectangle.has_value());
}

TEST(TextInputSessionTest, TheTwoEmptyValuesTheProtocolReadsAsNeverAreNotSent) {
    TextInputSession session = makeEnabledSession();

    session.setSurroundingText("", 0, 0);
    session.setCursorRectangle(TextInputCursorRectangle{.x = 4, .y = 8, .width = 0, .height = 16});
    session.setCursorRectangle(TextInputCursorRectangle{.x = 4, .y = 8, .width = 2, .height = 0});

    EXPECT_TRUE(isEmpty(session.takeBatch()));
}

TEST(TextInputSessionTest, AStaleSerialHoldsTheStateBackAndAMatchingOneReleasesIt) {
    TextInputSession session = makeEnabledSession();

    session.setSurroundingText(kSurroundingText, 5, 5);
    session.setCursorRectangle(kCaret);
    session.takeBatch();

    session.recordCommitString(kPreeditText);

    const std::vector<InputEvent> stale = session.applyDone(kStaleSerial);

    // The composition is never gated on the serial: the keystrokes are the user's.
    ASSERT_EQ(stale.size(), 1U);
    EXPECT_EQ(stale[0].kind, InputEventKind::ImeCommit);

    session.setCursorRectangle(kMovedCaret);

    EXPECT_TRUE(isEmpty(session.takeBatch()));

    session.applyDone(kSecondCommit);

    const TextInputSessionBatch released = session.takeBatch();

    ASSERT_TRUE(released.surroundingText.has_value());
    EXPECT_EQ(released.surroundingText->text, kSurroundingText);
    ASSERT_TRUE(released.cursorRectangle.has_value());
    EXPECT_EQ(released.cursorRectangle.value(), kMovedCaret);
}

TEST(TextInputSessionTest, ACompositorThatNeverAnswersLeavesTheStateHeldBackRatherThanResent) {
    TextInputSession session = makeEnabledSession();

    session.setSurroundingText(kSurroundingText, 5, 5);
    session.takeBatch();
    session.applyDone(kStaleSerial);

    session.setCursorRectangle(kCaret);

    EXPECT_TRUE(isEmpty(session.takeBatch()));
    EXPECT_TRUE(isEmpty(session.takeBatch()));
}

TEST(TextInputSessionTest, ATeardownClearsTheHeldBackState) {
    TextInputSession session = makeEnabledSession();

    session.setSurroundingText(kSurroundingText, 5, 5);
    session.takeBatch();
    session.applyDone(kStaleSerial);

    session.focusField(kFieldBeta, TextInputContentPurpose::Normal);
    session.setSurroundingText(kOtherSurroundingText, 11, 11);

    const TextInputSessionBatch batch = session.takeBatch();

    EXPECT_TRUE(batch.disable);
    EXPECT_TRUE(batch.enable);
    ASSERT_TRUE(batch.surroundingText.has_value());
    EXPECT_EQ(batch.surroundingText->text, kOtherSurroundingText);
}

TEST(TextInputSessionTest, CompositionIsBatchedUntilDone) {
    TextInputSession session = makeEnabledSession();

    session.recordDeleteSurroundingText(1, 0);
    session.recordCommitString(kPreeditText);
    session.recordPreeditString(kPreeditText, 1, 1);

    const std::vector<InputEvent> events = session.applyDone(kFirstCommit);

    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[0].kind, InputEventKind::ImeDeleteSurrounding);
    EXPECT_EQ(events[1].kind, InputEventKind::ImeCommit);
    EXPECT_EQ(events[2].kind, InputEventKind::ImePreedit);
}

TEST(TextInputSessionTest, EnteringDiscardsWhateverTheCompositorForgot) {
    TextInputSession session = makeEnabledSession();

    session.recordPreeditString(kPreeditText, 2, 2);
    session.applyDone(kFirstCommit);

    session.enter();

    EXPECT_TRUE(session.applyDone(kFirstCommit).empty());
}

struct ContentPurposeCase {
    std::string_view keyboardType;
    TextInputContentPurpose purpose;
    bool secureTextEntry;
};

TEST(TextInputSessionTest, TheContentPurposeIsTheClosestOneTheFieldsPropsName) {
    const std::vector<ContentPurposeCase> cases{
        {.keyboardType = "", .purpose = TextInputContentPurpose::Normal, .secureTextEntry = false},
        {.keyboardType = "default", .purpose = TextInputContentPurpose::Normal, .secureTextEntry = false},
        {.keyboardType = "email-address", .purpose = TextInputContentPurpose::Email, .secureTextEntry = false},
        {.keyboardType = "url", .purpose = TextInputContentPurpose::Url, .secureTextEntry = false},
        {.keyboardType = "phone-pad", .purpose = TextInputContentPurpose::Phone, .secureTextEntry = false},
        {.keyboardType = "name-phone-pad", .purpose = TextInputContentPurpose::Phone, .secureTextEntry = false},
        {.keyboardType = "number-pad", .purpose = TextInputContentPurpose::Digits, .secureTextEntry = false},
        {.keyboardType = "numeric", .purpose = TextInputContentPurpose::Number, .secureTextEntry = false},
        {.keyboardType = "decimal-pad", .purpose = TextInputContentPurpose::Number, .secureTextEntry = false},
        {.keyboardType = "email-address", .purpose = TextInputContentPurpose::Password, .secureTextEntry = true},
    };

    for (const ContentPurposeCase& testCase : cases) {
        EXPECT_EQ(textInputContentPurpose(testCase.secureTextEntry, testCase.keyboardType), testCase.purpose)
            << "keyboardType " << testCase.keyboardType;
    }
}

} // namespace
