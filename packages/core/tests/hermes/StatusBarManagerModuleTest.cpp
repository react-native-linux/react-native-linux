#include "OnJavaScriptThread.h"

#include <cxxreact/JSBigString.h>
#include <gtest/gtest.h>
#include <memory>

namespace react_native_linux {
namespace {

// What `NativeStatusBarManagerAndroid.js` and `NativeStatusBarManagerIOS.js` do as `StatusBar.js` imports them:
// `getEnforcing('StatusBarManager')`, which throws when the module is not registered. Then every call the Android
// spec has.
constexpr char kStatusBarProbe[] = R"JAVASCRIPT(
try {
  const statusBarManager = globalThis.nativeModuleProxy.StatusBarManager;

  globalThis.statusBarResult = JSON.stringify({
    constants: statusBarManager.getConstants(),
    setStyle: typeof statusBarManager.setStyle('light-content'),
    setStyleWithoutStyle: typeof statusBarManager.setStyle(null),
    setHidden: typeof statusBarManager.setHidden(true),
  });
} catch (error) {
  globalThis.statusBarResult = `threw: ${error.message}`;
}
)JAVASCRIPT";

/** #797: a desktop window has no status bar, so the height is 0 and every setter answers nothing. */
TEST(StatusBarManagerModuleTest, ReportsNoStatusBarAndIgnoresEverySetter) {
    ReactHost reactHost;

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kStatusBarProbe),
                         "StatusBarManagerModuleTest.js");

    EXPECT_EQ(awaitGlobalString(reactHost, "statusBarResult"),
              R"({"constants":{"HEIGHT":0},"setStyle":"undefined","setStyleWithoutStyle":"undefined",)"
              R"("setHidden":"undefined"})");
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
