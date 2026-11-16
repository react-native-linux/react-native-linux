#include "OnJavaScriptThread.h"

#include <cxxreact/JSBigString.h>
#include <gtest/gtest.h>
#include <memory>

namespace react_native_linux {
namespace {

// What `AppState.js` does with the module, minus React Native: read the initial state, ask for the current one, and
// listen for the two events through the global emitter `RCTDeviceEventEmitter` installs.
constexpr char kAppStateProbe[] = R"JAVASCRIPT(
const appState = globalThis.nativeModuleProxy.AppState;
const trace = { initialAppState: appState.getConstants().initialAppState, events: [] };

globalThis.appStateTrace = trace;
appState.getCurrentAppState((current) => { trace.currentAppState = current.app_state; }, () => {});
appState.addListener('appStateDidChange');
globalThis.__rctDeviceEventEmitter = {
  emit: (eventName, payload) => {
    trace.events.push(eventName === 'appStateDidChange' ? `change ${payload.app_state}` : `focus ${payload}`);
  },
};
)JAVASCRIPT";

constexpr char kAfterChanges[] = R"JAVASCRIPT(
globalThis.nativeModuleProxy.AppState.removeListeners(1);
globalThis.nativeModuleProxy.AppState.getCurrentAppState((current) => {
  globalThis.appStateTrace.afterChanges = current.app_state;
  globalThis.appStateResult = JSON.stringify(globalThis.appStateTrace);
}, () => {});
)JAVASCRIPT";

/**
 * #765: `AppState` from JavaScript. It starts `active`, and the window's state, written on this thread as the frame
 * thread writes it, reaches the JavaScript thread in the order the window changed: losing activation is `blur` alone,
 * a suspension is `background`, a resume is `active` again, and a reactivation is `focus`. A configure that changes
 * nothing `AppState` reports emits nothing.
 */
TEST(AppStateModuleTest, ReportsSuspensionAsChangeAndActivationAsFocus) {
    ReactHost reactHost;

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kAppStateProbe), "AppStateModuleTest.js");
    reactHost.drainJavaScriptThread();

    AppStateModel& model = reactHost.appState();

    model.onToplevelStateChanged(ToplevelState{.activated = true, .maximized = true});
    model.onToplevelStateChanged(ToplevelState{});
    model.onToplevelStateChanged(ToplevelState{.suspended = true});
    model.onToplevelStateChanged(ToplevelState{});
    model.onToplevelStateChanged(ToplevelState{.activated = true});
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kAfterChanges), "AppStateAfter.js");

    EXPECT_EQ(awaitGlobalString(reactHost, "appStateResult"),
              R"({"initialAppState":"active","events":["focus false","change background","change active",)"
              R"("focus true"],"currentAppState":"active","afterChanges":"active"})");
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

/**
 * A window that is already suspended when its session starts: `WindowSession` seeds the model before the bundle, so
 * the first script reads `background` from both the constants and `getCurrentAppState`, and no change event follows.
 */
TEST(AppStateModuleTest, ASeededSuspendedWindowStartsInTheBackground) {
    ReactHost reactHost;

    reactHost.appState().seed(ToplevelState{.suspended = true});
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kAppStateProbe), "AppStateModuleTest.js");
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kAfterChanges), "AppStateAfter.js");

    EXPECT_EQ(awaitGlobalString(reactHost, "appStateResult"),
              R"({"initialAppState":"background","events":[],"currentAppState":"background",)"
              R"("afterChanges":"background"})");
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
