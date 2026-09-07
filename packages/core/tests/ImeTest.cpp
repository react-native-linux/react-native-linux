#include "InputPipeline.h"
#include "TextInputV3State.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <react/renderer/graphics/Point.h>
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

constexpr int32_t kNoCursor = -1;

/**
 * "ni" composed into "you" in pinyin: the shortest sequence that carries a pre-edit, a commit that replaces it,
 * and the empty pre-edit that ends the composition.
 */
constexpr char kPreeditText[] = "ni";
constexpr char kCommitText[] = "\xE4\xBD\xA0";

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

TEST(TextInputV3StateTest, NothingIsAppliedUntilDone) {
    TextInputV3State state;

    state.recordDeleteSurroundingText(3, 1);
    state.recordCommitString(kCommitText);
    state.recordPreeditString(kPreeditText, 1, 2);

    const std::vector<InputEvent> events = state.applyDone();

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
    TextInputV3State state;

    state.recordPreeditString("n", 1, 1);
    state.recordPreeditString(kPreeditText, 2, 2);

    const std::vector<InputEvent> events = state.applyDone();

    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].text, kPreeditText);
    EXPECT_EQ(events[0].preeditCursorBegin, 2);
}

TEST(TextInputV3StateTest, ACommitWithNoPreeditIsTheOnlyEvent) {
    TextInputV3State state;

    state.recordCommitString(kCommitText);

    const std::vector<InputEvent> events = state.applyDone();

    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].kind, InputEventKind::ImeCommit);
}

TEST(TextInputV3StateTest, CommittingAPreeditEndsTheCompositionWithAnEmptyOne) {
    TextInputV3State state;

    state.recordPreeditString(kPreeditText, 2, 2);
    state.applyDone();

    state.recordCommitString(kCommitText);

    const std::vector<InputEvent> events = state.applyDone();

    ASSERT_EQ(events.size(), 2U);
    EXPECT_EQ(events[0].kind, InputEventKind::ImeCommit);
    EXPECT_EQ(events[1].kind, InputEventKind::ImePreedit);
    EXPECT_TRUE(events[1].text.empty());
}

TEST(TextInputV3StateTest, ADoneThatChangesNothingProducesNoEvents) {
    TextInputV3State state;

    EXPECT_TRUE(state.applyDone().empty());
}

TEST(TextInputV3StateTest, DeletionOnEitherSideOfTheCursorIsOneEvent) {
    TextInputV3State state;

    state.recordDeleteSurroundingText(0, 2);

    const std::vector<InputEvent> afterOnly = state.applyDone();

    ASSERT_EQ(afterOnly.size(), 1U);
    EXPECT_EQ(afterOnly[0].kind, InputEventKind::ImeDeleteSurrounding);
    EXPECT_EQ(afterOnly[0].deleteBeforeLength, 0U);
    EXPECT_EQ(afterOnly[0].deleteAfterLength, 2U);
}

TEST(TextInputV3StateTest, MovingTheCursorInsideAnUnchangedPreeditIsStillAnEvent) {
    TextInputV3State state;

    state.recordPreeditString(kPreeditText, 0, 0);
    state.applyDone();

    state.recordPreeditString(kPreeditText, 1, 1);

    const std::vector<InputEvent> moved = state.applyDone();

    ASSERT_EQ(moved.size(), 1U);
    EXPECT_EQ(moved[0].preeditCursorBegin, 1);

    state.recordPreeditString(kPreeditText, 1, 2);

    const std::vector<InputEvent> highlighted = state.applyDone();

    ASSERT_EQ(highlighted.size(), 1U);
    EXPECT_EQ(highlighted[0].preeditCursorEnd, 2);

    state.recordPreeditString(kPreeditText, 1, 2);

    EXPECT_TRUE(state.applyDone().empty());
}

TEST(TextInputV3StateTest, AHiddenCursorSurvivesAsTheNegativePair) {
    TextInputV3State state;

    state.recordPreeditString(kPreeditText, kNoCursor, kNoCursor);

    const std::vector<InputEvent> events = state.applyDone();

    ASSERT_EQ(events.size(), 1U);
    EXPECT_EQ(events[0].preeditCursorBegin, kNoCursor);
    EXPECT_EQ(events[0].preeditCursorEnd, kNoCursor);
}

TEST(TextInputV3StateTest, AResetDiscardsTheCompositionOnScreenAndReportsIt) {
    TextInputV3State state;

    state.recordPreeditString(kPreeditText, 2, 2);
    state.applyDone();

    const std::vector<InputEvent> discarded = state.reset();

    ASSERT_EQ(discarded.size(), 1U);
    EXPECT_EQ(discarded[0].kind, InputEventKind::ImePreedit);
    EXPECT_TRUE(discarded[0].text.empty());

    EXPECT_TRUE(state.reset().empty());
}

TEST(TextInputV3StateTest, AResetDropsThePendingBatchToo) {
    TextInputV3State state;

    state.recordPreeditString(kPreeditText, 2, 2);
    state.recordCommitString(kCommitText);
    state.reset();

    EXPECT_TRUE(state.applyDone().empty());
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
