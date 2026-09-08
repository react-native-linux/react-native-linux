#include "ReactHost.h"

#include <chrono>
#include <cxxreact/JSBigString.h>
#include <gtest/gtest.h>
#include <memory>

namespace react_native_linux {
namespace {

// A `setTimeout` distant enough that the short quiescence budget below can never let it fire, so the destructor
// that follows always tears down a `TimerManager` that is still holding a real `jsi::Function` callback for
// it — exactly the state that used to abort a debug Hermes with "This PointerValue was left dangling after the
// Runtime was destroyed" (see the shutdown contract in ReactHost.h).
constexpr char kScriptWithPendingTimer[] = "setTimeout(function () {}, 60000);";
constexpr std::chrono::milliseconds kQuiescenceBudget{50};

TEST(ReactHostTeardownTest, DestroysCleanlyWithAPendingTimerCallbackStillOutstanding) {
    ReactHost reactHost;

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kScriptWithPendingTimer),
                         "ReactHostTeardownTest.js");

    EXPECT_FALSE(reactHost.runUntilQuiescent(kQuiescenceBudget));

    // `reactHost` goes out of scope here with the timer still pending: the assertion this test exists to guard
    // against is a process abort during that destruction, which a crashed test binary reports on its own.
}

} // namespace
} // namespace react_native_linux
