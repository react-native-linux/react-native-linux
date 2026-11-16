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
 * #765: `AppState` from JavaScript. It starts `active`, and the window's activation, written on this thread as the
 * frame thread writes it, reaches the JavaScript thread as `appStateDidChange` and `appStateFocusChange` in the
 * order the window changed: a deactivation, a suspension, a resume that is still deactivated, and a reactivation.
 * A configure that changes nothing `AppState` reports emits nothing.
 */
TEST(AppStateModuleTest, ReportsTheWindowActivationAsChangeAndFocusEvents) {
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
              R"({"initialAppState":"active","events":["change inactive","focus false","change background",)"
              R"("change inactive","change active","focus true"],"currentAppState":"active",)"
              R"("afterChanges":"active"})");
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
