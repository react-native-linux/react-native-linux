#include "AppState.h"

#include <atomic>
#include <gtest/gtest.h>
#include <latch>
#include <string>
#include <thread>
#include <vector>

namespace {

using react_native_linux::AppStateModel;
using react_native_linux::appStateOf;
using react_native_linux::AppStateSnapshot;
using react_native_linux::AppStatus;
using react_native_linux::nameOfAppStatus;
using react_native_linux::ToplevelState;

constexpr AppStateSnapshot kActive{.status = AppStatus::Active, .isFocused = true};
constexpr AppStateSnapshot kInactive{.status = AppStatus::Inactive, .isFocused = false};

TEST(AppStateTest, AnActivatedWindowIsActiveAndFocused) {
    EXPECT_EQ(appStateOf(ToplevelState{.activated = true}), kActive);
}

TEST(AppStateTest, ADeactivatedWindowIsInactiveAndBlurred) { EXPECT_EQ(appStateOf(ToplevelState{}), kInactive); }

TEST(AppStateTest, ASuspendedWindowIsBackgroundWhateverItsActivation) {
    EXPECT_EQ(appStateOf(ToplevelState{.suspended = true}),
              (AppStateSnapshot{.status = AppStatus::Background, .isFocused = false}));
    EXPECT_EQ(appStateOf(ToplevelState{.activated = true, .suspended = true}),
              (AppStateSnapshot{.status = AppStatus::Background, .isFocused = true}));
}

TEST(AppStateTest, OtherWindowStatesDoNotChangeTheAnswer) {
    EXPECT_EQ(appStateOf(ToplevelState{.activated = true, .maximized = true, .fullscreen = true}), kActive);
}

TEST(AppStateTest, StatusNamesAreAppStateStatusValues) {
    EXPECT_EQ(nameOfAppStatus(AppStatus::Active), "active");
    EXPECT_EQ(nameOfAppStatus(AppStatus::Inactive), "inactive");
    EXPECT_EQ(nameOfAppStatus(AppStatus::Background), "background");
}

TEST(AppStateModelTest, StartsActiveAndFocusedBeforeAnyWindowSaysOtherwise) {
    const AppStateModel model;

    EXPECT_EQ(model.current(), kActive);
}

TEST(AppStateModelTest, NotifiesOnlyWhenTheAnswerChanges) {
    AppStateModel model;
    std::vector<std::pair<AppStateSnapshot, AppStateSnapshot>> changes;

    model.setChangeListener([&changes](const AppStateSnapshot& previous, const AppStateSnapshot& next) {
        changes.emplace_back(previous, next);
    });

    model.onToplevelStateChanged(ToplevelState{.activated = true});
    model.onToplevelStateChanged(ToplevelState{.activated = true, .maximized = true});
    model.onToplevelStateChanged(ToplevelState{});
    model.onToplevelStateChanged(ToplevelState{});

    ASSERT_EQ(changes.size(), 1U);
    EXPECT_EQ(changes[0].first, kActive);
    EXPECT_EQ(changes[0].second, kInactive);
    EXPECT_EQ(model.current(), kInactive);
}

TEST(AppStateModelTest, WithNoChangeListenerAChangeStillUpdatesTheState) {
    AppStateModel model;

    model.onToplevelStateChanged(ToplevelState{});

    EXPECT_EQ(model.current(), kInactive);
}

// The TSan half of the threading contract: the frame thread toggles activation as fast as it can while the
// JavaScript thread reads `current`, as `getConstants` and `getCurrentAppState` do. The listener runs on the writer,
// and with one writer it sees every transition in order, so the alternation it records can never repeat a state.
TEST(AppStateModelTest, AFrameThreadWriterAndAJavaScriptThreadReaderDoNotRace) {
    constexpr int kTransitions = 2000;
    AppStateModel model;
    std::vector<AppStatus> notified;
    std::latch startLatch(2);
    std::atomic<bool> isWriting{true};

    model.setChangeListener([&notified](const AppStateSnapshot& /*previous*/, const AppStateSnapshot& next) {
        notified.push_back(next.status);
    });

    std::thread frameThread([&model, &startLatch, &isWriting] {
        startLatch.arrive_and_wait();

        for (int transition = 0; transition < kTransitions; ++transition) {
            model.onToplevelStateChanged(ToplevelState{.activated = transition % 2 == 1});
        }

        isWriting = false;
    });

    startLatch.arrive_and_wait();

    do {
        const AppStateSnapshot read = model.current();

        EXPECT_EQ(read.isFocused, read.status == AppStatus::Active);
    } while (isWriting);

    frameThread.join();

    ASSERT_EQ(notified.size(), static_cast<size_t>(kTransitions));
    for (size_t index = 0; index < notified.size(); ++index) {
        EXPECT_EQ(notified[index], index % 2 == 0 ? AppStatus::Inactive : AppStatus::Active) << index;
    }
}

} // namespace
