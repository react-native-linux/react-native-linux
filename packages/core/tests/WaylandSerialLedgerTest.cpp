#include "WaylandSerialLedger.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <limits>
#include <optional>

namespace {

using react_native_linux::nameOfSerialKind;
using react_native_linux::WaylandSerialKind;
using react_native_linux::WaylandSerialLedger;

constexpr bool kPressed = true;
constexpr bool kReleased = false;

TEST(WaylandSerialLedgerTest, EveryKindReadsAsZeroBeforeAnythingIsRecorded) {
    const WaylandSerialLedger ledger;

    EXPECT_EQ(ledger.serial(WaylandSerialKind::PointerEnter), 0U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::PointerButtonPress), 0U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::KeyboardEnter), 0U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::KeyboardKeyPress), 0U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::TouchDown), 0U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::Configure), 0U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::InteractiveMove), 0U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::Selection), 0U);
}

TEST(WaylandSerialLedgerTest, PointerEnterIsItsOwnKind) {
    WaylandSerialLedger ledger;

    ledger.recordPointerEnter(7);

    EXPECT_EQ(ledger.serial(WaylandSerialKind::PointerEnter), 7U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::PointerButtonPress), 0U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::Selection), 0U);
}

TEST(WaylandSerialLedgerTest, KeyboardEnterIsItsOwnKind) {
    WaylandSerialLedger ledger;

    ledger.recordKeyboardEnter(9);

    EXPECT_EQ(ledger.serial(WaylandSerialKind::KeyboardEnter), 9U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::KeyboardKeyPress), 0U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::Selection), 0U);
}

TEST(WaylandSerialLedgerTest, PointerButtonPressUpdatesTheMergedKindsAndAReleaseDoesNotChangeThem) {
    WaylandSerialLedger ledger;

    ledger.recordPointerButton(11, kPressed);
    ASSERT_EQ(ledger.serial(WaylandSerialKind::PointerButtonPress), 11U);
    ASSERT_EQ(ledger.serial(WaylandSerialKind::InteractiveMove), 11U);
    ASSERT_EQ(ledger.serial(WaylandSerialKind::Selection), 11U);

    ledger.recordPointerButton(99, kReleased);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::PointerButtonPress), 11U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::InteractiveMove), 11U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::Selection), 11U);
}

TEST(WaylandSerialLedgerTest, KeyboardKeyPressUpdatesSelectionButNotInteractiveMoveAndAReleaseDoesNotChangeIt) {
    WaylandSerialLedger ledger;

    ledger.recordKeyboardKey(13, kPressed);
    ASSERT_EQ(ledger.serial(WaylandSerialKind::KeyboardKeyPress), 13U);
    ASSERT_EQ(ledger.serial(WaylandSerialKind::Selection), 13U);
    ASSERT_EQ(ledger.serial(WaylandSerialKind::InteractiveMove), 0U);

    ledger.recordKeyboardKey(77, kReleased);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::KeyboardKeyPress), 13U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::Selection), 13U);
}

TEST(WaylandSerialLedgerTest, TouchDownUpdatesItsOwnAndBothMergedKinds) {
    WaylandSerialLedger ledger;

    ledger.recordTouchDown(21);

    EXPECT_EQ(ledger.serial(WaylandSerialKind::TouchDown), 21U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::InteractiveMove), 21U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::Selection), 21U);
}

TEST(WaylandSerialLedgerTest, ConfigureIsIndependentOfEveryInputKind) {
    WaylandSerialLedger ledger;

    ledger.recordPointerButton(1, kPressed);
    ledger.recordConfigure(42);

    EXPECT_EQ(ledger.serial(WaylandSerialKind::Configure), 42U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::Selection), 1U);
}

// GPUI's second rule: the merged kinds track arrival order, not numeric magnitude, so an unsigned rollover does
// not make a fresh press look older than a stale one still sitting in the ledger.
TEST(WaylandSerialLedgerTest, TheCounterWrapsWithoutRegressing) {
    WaylandSerialLedger ledger;

    ledger.recordPointerButton(std::numeric_limits<uint32_t>::max(), kPressed);
    ledger.recordPointerButton(1, kPressed);

    EXPECT_EQ(ledger.serial(WaylandSerialKind::PointerButtonPress), 1U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::InteractiveMove), 1U);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::Selection), 1U);
}

// zed#52170: a click after a keystroke must not resurrect the keystroke's serial for the selection request, and
// a keystroke after a click must not resurrect the click's — arrival order, most recent record wins, regardless
// of which kind recorded it.
TEST(WaylandSerialLedgerTest, SelectionTracksWhicheverInputKindRecordedMostRecently) {
    WaylandSerialLedger ledger;

    ledger.recordKeyboardKey(5, kPressed);
    ledger.recordPointerButton(6, kPressed);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::Selection), 6U);

    ledger.recordKeyboardKey(7, kPressed);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::Selection), 7U);

    ledger.recordTouchDown(8);
    EXPECT_EQ(ledger.serial(WaylandSerialKind::Selection), 8U);
}

TEST(WaylandSerialLedgerTest, RequestSerialAnswersWithTheRecordedValue) {
    WaylandSerialLedger ledger;

    ledger.recordConfigure(4);

    const std::optional<uint32_t> answer = ledger.requestSerial(WaylandSerialKind::Configure);

    ASSERT_TRUE(answer.has_value());
    EXPECT_EQ(*answer, 4U);
}

TEST(WaylandSerialLedgerTest, RequestSerialRefusesAKindThatHasNeverRecorded) {
    const WaylandSerialLedger ledger;

    testing::internal::CaptureStderr();
    const std::optional<uint32_t> answer = ledger.requestSerial(WaylandSerialKind::Selection);
    const std::string logged = testing::internal::GetCapturedStderr();

    EXPECT_FALSE(answer.has_value());
    EXPECT_NE(logged.find("wayland-serial-ledger"), std::string::npos);
    EXPECT_NE(logged.find("selection"), std::string::npos);
}

TEST(WaylandSerialLedgerTest, EveryKindHasAName) {
    EXPECT_EQ(nameOfSerialKind(WaylandSerialKind::PointerEnter), "pointer enter");
    EXPECT_EQ(nameOfSerialKind(WaylandSerialKind::PointerButtonPress), "pointer button press");
    EXPECT_EQ(nameOfSerialKind(WaylandSerialKind::KeyboardEnter), "keyboard enter");
    EXPECT_EQ(nameOfSerialKind(WaylandSerialKind::KeyboardKeyPress), "keyboard key press");
    EXPECT_EQ(nameOfSerialKind(WaylandSerialKind::TouchDown), "touch down");
    EXPECT_EQ(nameOfSerialKind(WaylandSerialKind::Configure), "configure");
    EXPECT_EQ(nameOfSerialKind(WaylandSerialKind::InteractiveMove), "interactive move");
    EXPECT_EQ(nameOfSerialKind(WaylandSerialKind::Selection), "selection");
}

} // namespace
