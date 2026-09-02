#include "InputPipeline.h"
#include "TextInputV3State.h"

#include <gtest/gtest.h>

#include <react/renderer/graphics/Point.h>

#include <cstdint>
#include <string>
#include <vector>

namespace {

using facebook::react::Point;
using react_native_linux::deliverImeEvent;
using react_native_linux::ImeSink;
using react_native_linux::InputEvent;
using react_native_linux::InputEventKind;
using react_native_linux::PointerDispatch;
using react_native_linux::PointerRouter;
using react_native_linux::TextInputV3State;

constexpr uint32_t kFirstSerial = 0;
constexpr uint32_t kSecondSerial = 1;
constexpr uint32_t kThirdSerial = 2;
constexpr int32_t kNoCursor = -1;

/**
 * "ni" composed into "you" in pinyin: the shortest sequence that carries a pre-edit, a commit that replaces it,
 * and the empty pre-edit that ends the composition.
 */
constexpr char kPreeditText[] = "ni";
constexpr char kCommitText[] = "\xE4\xBD\xA0";

TextInputV3State makeEnabledState() {
    TextInputV3State state;

    state.enter();
    state.enable();

    return state;
}

class RecordingImeSink final : public ImeSink {
public:
    void onImePreedit(const std::string& text, int32_t cursorBegin, int32_t cursorEnd) override {
        calls.push_back("preedit " + text + " " + std::to_string(cursorBegin) + " " + std::to_string(cursorEnd));
    }

    void onImeCommit(const std::string& text) override { calls.push_back("commit " + text); }

    void onImeDeleteSurrounding(uint32_t beforeLength, uint32_t afterLength) override {
        calls.push_back("delete " + std::to_string(beforeLength) + " " + std::to_string(afterLength));
    }

    std::vector<std::string> calls;
};

TEST(TextInputV3StateTest, FocusIsWhatMakesEnablingMeaningful) {
    TextInputV3State state;

    EXPECT_FALSE(state.isFocused());
    EXPECT_FALSE(state.isEnabled());

    state.enter();

    EXPECT_TRUE(state.isFocused());
    EXPECT_FALSE(state.isEnabled());

    state.enable();

    EXPECT_TRUE(state.isEnabled());

    state.disable();

    EXPECT_FALSE(state.isEnabled());
}

TEST(TextInputV3StateTest, NothingIsAppliedUntilDone) {
    TextInputV3State state = makeEnabledState();

    state.recordDeleteSurroundingText(3, 1);
    state.recordCommitString(kCommitText);
    state.recordPreeditString(kPreeditText, 1, 2);

    const std::vector<InputEvent> events = state.applyDone(kFirstSerial);

    ASSERT_EQ(events.size(), 3U);
    EXPECT_EQ(events[0].kind, InputEventKind::ImeDeleteSurrounding);
    EXPECT_EQ(events[0].deleteBeforeLength, 3U);
    EXPECT_EQ(events[0].deleteAfterLength, 1U);
    EXPECT_EQ(events[1].kind, InputEventKind::ImeCommit);
    EXPECT_EQ(events[1].text, kCommitText);
    EXPECT_EQ(events[2].kind, InputEventKind::ImePreedit);
    EXPECT_EQ(events[2].text, kPreeditText);
    EXPECT_EQ(events[2].preeditCursorBegin, 1);
    EXPECT_EQ(events[2].preeditCursorEnd, 2);
}

TEST(TextInputV3StateTest, TheLastPreeditOfABatchIsTheOneThatSurvives) {
    TextInputV3State state = makeEnabledState();

    state.recordPreeditString("n", 1, 1);
    state.recordPreeditString(kPreeditText, 2, 2);

    const std::vector<InputEvent> events = state.applyDone(kFirstSerial);

    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].text, kPreeditText);
    EXPECT_EQ(events[0].preeditCursorBegin, 2);
}

TEST(TextInputV3StateTest, ACommitWithNoPreeditIsTheOnlyEvent) {
    TextInputV3State state = makeEnabledState();

    state.recordCommitString(kCommitText);

    const std::vector<InputEvent> events = state.applyDone(kFirstSerial);

    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].kind, InputEventKind::ImeCommit);
}

TEST(TextInputV3StateTest, CommittingAPreeditEndsTheCompositionWithAnEmptyOne) {
    TextInputV3State state = makeEnabledState();

    state.recordPreeditString(kPreeditText, 2, 2);
    state.applyDone(kFirstSerial);

    state.recordCommitString(kCommitText);

    const std::vector<InputEvent> events = state.applyDone(kFirstSerial);

    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].kind, InputEventKind::ImeCommit);
    EXPECT_EQ(events[1].kind, InputEventKind::ImePreedit);
    EXPECT_TRUE(events[1].text.empty());
}

TEST(TextInputV3StateTest, ADoneThatChangesNothingProducesNoEvents) {
    TextInputV3State state = makeEnabledState();

    EXPECT_TRUE(state.applyDone(kFirstSerial).empty());
}

TEST(TextInputV3StateTest, DeletionOnEitherSideOfTheCursorIsOneEvent) {
    TextInputV3State state = makeEnabledState();

    state.recordDeleteSurroundingText(0, 2);

    const std::vector<InputEvent> afterOnly = state.applyDone(kFirstSerial);

    ASSERT_EQ(afterOnly.size(), 1U);
    EXPECT_EQ(afterOnly[0].kind, InputEventKind::ImeDeleteSurrounding);
    EXPECT_EQ(afterOnly[0].deleteBeforeLength, 0U);
    EXPECT_EQ(afterOnly[0].deleteAfterLength, 2U);
}

TEST(TextInputV3StateTest, MovingTheCursorInsideAnUnchangedPreeditIsStillAnEvent) {
    TextInputV3State state = makeEnabledState();

    state.recordPreeditString(kPreeditText, 0, 0);
    state.applyDone(kFirstSerial);

    state.recordPreeditString(kPreeditText, 1, 1);

    const std::vector<InputEvent> moved = state.applyDone(kFirstSerial);

    ASSERT_EQ(moved.size(), 1U);
    EXPECT_EQ(moved[0].preeditCursorBegin, 1);

    state.recordPreeditString(kPreeditText, 1, 2);

    const std::vector<InputEvent> highlighted = state.applyDone(kFirstSerial);

    ASSERT_EQ(highlighted.size(), 1U);
    EXPECT_EQ(highlighted[0].preeditCursorEnd, 2);

    state.recordPreeditString(kPreeditText, 1, 2);

    EXPECT_TRUE(state.applyDone(kFirstSerial).empty());
}

TEST(TextInputV3StateTest, AHiddenCursorSurvivesAsTheNegativePair) {
    TextInputV3State state = makeEnabledState();

    state.recordPreeditString(kPreeditText, kNoCursor, kNoCursor);

    const std::vector<InputEvent> events = state.applyDone(kFirstSerial);

    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].preeditCursorBegin, kNoCursor);
    EXPECT_EQ(events[0].preeditCursorEnd, kNoCursor);
}

TEST(TextInputV3StateTest, LosingFocusDiscardsTheCompositionOnScreen) {
    TextInputV3State state = makeEnabledState();

    state.recordPreeditString(kPreeditText, 2, 2);
    state.applyDone(kFirstSerial);

    const std::vector<InputEvent> discarded = state.leave();

    ASSERT_EQ(discarded.size(), 1U);
    EXPECT_EQ(discarded[0].kind, InputEventKind::ImePreedit);
    EXPECT_TRUE(discarded[0].text.empty());
    EXPECT_FALSE(state.isFocused());
    EXPECT_FALSE(state.isEnabled());

    EXPECT_TRUE(state.leave().empty());
}

TEST(TextInputV3StateTest, FocusInvalidatesTheCompositionWithoutReportingIt) {
    TextInputV3State state = makeEnabledState();

    state.recordPreeditString(kPreeditText, 2, 2);
    state.applyDone(kFirstSerial);

    state.enter();

    EXPECT_TRUE(state.applyDone(kFirstSerial).empty());
}

TEST(TextInputV3StateTest, EnablingInvalidatesTheCompositionToo) {
    TextInputV3State state = makeEnabledState();

    state.recordPreeditString(kPreeditText, 2, 2);
    state.applyDone(kFirstSerial);

    state.enable();

    EXPECT_TRUE(state.applyDone(kFirstSerial).empty());
}

TEST(TextInputV3StateTest, ASerialThatIsNotOurCommitCountHoldsBackTheNextStateRequest) {
    TextInputV3State state = makeEnabledState();

    state.recordCommitRequest();
    state.recordCommitRequest();

    state.recordCommitString(kCommitText);

    const std::vector<InputEvent> stale = state.applyDone(kSecondSerial);

    ASSERT_EQ(stale.size(), 1U);
    EXPECT_TRUE(state.needsStateResend());

    state.applyDone(kThirdSerial);

    EXPECT_FALSE(state.needsStateResend());
}

TEST(TextInputV3StateTest, DisablingClearsTheHeldBackStateRequest) {
    TextInputV3State state = makeEnabledState();

    state.recordCommitRequest();
    state.applyDone(kFirstSerial);

    EXPECT_TRUE(state.needsStateResend());

    state.disable();

    EXPECT_FALSE(state.needsStateResend());
}

TEST(TextInputV3StateTest, FocusChangeClearsTheHeldBackStateRequest) {
    TextInputV3State state = makeEnabledState();

    state.recordCommitRequest();
    state.applyDone(kFirstSerial);

    EXPECT_TRUE(state.needsStateResend());

    state.enter();

    EXPECT_FALSE(state.needsStateResend());

    state.recordCommitRequest();
    state.applyDone(kFirstSerial);

    EXPECT_TRUE(state.needsStateResend());

    state.leave();

    EXPECT_FALSE(state.needsStateResend());
}

TEST(ImeSinkTest, EachCompositionEventReachesItsOwnCall) {
    RecordingImeSink sink;

    const InputEvent deleted{
        .kind = InputEventKind::ImeDeleteSurrounding, .deleteBeforeLength = 3, .deleteAfterLength = 1};
    const InputEvent committed{.kind = InputEventKind::ImeCommit, .text = kCommitText};
    const InputEvent composing{
        .kind = InputEventKind::ImePreedit, .text = kPreeditText, .preeditCursorBegin = 1, .preeditCursorEnd = 2};

    deliverImeEvent(deleted, sink);
    deliverImeEvent(committed, sink);
    deliverImeEvent(composing, sink);

    const std::vector<std::string> expected{"delete 3 1", std::string("commit ") + kCommitText,
                                            std::string("preedit ") + kPreeditText + " 1 2"};

    EXPECT_EQ(sink.calls, expected);
}

TEST(ImeSinkTest, EverythingThatIsNotCompositionIsIgnored) {
    RecordingImeSink sink;

    deliverImeEvent(InputEvent{.kind = InputEventKind::KeyPress, .key = std::string("a")}, sink);

    EXPECT_TRUE(sink.calls.empty());
}

TEST(ImeSinkTest, CompositionProducesNoPointerDispatches) {
    PointerRouter router;
    const std::vector<InputEventKind> imeKinds{InputEventKind::ImePreedit, InputEventKind::ImeCommit,
                                               InputEventKind::ImeDeleteSurrounding};

    for (const InputEventKind kind : imeKinds) {
        const InputEvent event{.kind = kind, .text = kCommitText};
        const std::vector<PointerDispatch> dispatches = router.route(event, 0, Point{.x = 0, .y = 0});

        EXPECT_TRUE(dispatches.empty());
    }
}

} // namespace
