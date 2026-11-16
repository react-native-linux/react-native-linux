#include "../ScopedEnvironment.h"
#include "ReactHost.h"

#include <cxxreact/JSBigString.h>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace react_native_linux {
namespace {

// gt-react-native's three calls, as its getLocale() and NativeConditionStore make them.
constexpr char kGtReactNativeProbe[] = R"JAVASCRIPT(
const thrown = (call) => { try { call(); return 'returned'; } catch (error) { return `threw: ${error.message}`; } };

try {
  const gt = globalThis.nativeModuleProxy.GtReactNative;
  const trace = [JSON.stringify(gt.getNativeLocales()), String(gt.nativeStoreGet('generaltranslation.locale'))];

  gt.nativeStoreSet('generaltranslation.locale', 'fr');
  gt.nativeStoreSet('generaltranslation.region', '');
  trace.push(gt.nativeStoreGet('generaltranslation.locale'), JSON.stringify(gt.nativeStoreGet('generaltranslation.region')));
  gt.nativeStoreSet('generaltranslation.locale', 'de');
  trace.push(gt.nativeStoreGet('generaltranslation.locale'), String(gt.multiply(6, 7)));
  trace.push(thrown(() => gt.nativeStoreSet('only a key')), thrown(() => gt.nativeStoreGet(42)), thrown(() => gt.multiply(6)));
  globalThis.gtTrace = trace.join('|');
} catch (error) {
  globalThis.gtTrace = `threw: ${error.message}`;
}
)JAVASCRIPT";

/**
 * #647: `gt-react-native` requires `GtReactNative` with `getEnforcing` as it is imported, which is T3 Code's first
 * fatal on Linux. The module answers the user's languages from the POSIX environment, best first, and keeps the
 * locale the user picked in its own store; a call missing an argument or passing the wrong type throws.
 */
TEST(GtReactNativeModuleTest, AnswersTheEnvironmentLanguagesAndStoresThePickedLocale) {
    const ScopedEnvironment lcAll("LC_ALL", "");
    const ScopedEnvironment lcMessages("LC_MESSAGES", "");
    const ScopedEnvironment lang("LANG", "de_DE.UTF-8");
    const ScopedEnvironment language("LANGUAGE", "fr_CA:fr");
    ReactHost reactHost;
    std::promise<std::string> trace;

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kGtReactNativeProbe),
                         "GtReactNativeModuleTest.js");
    reactHost.drainJavaScriptThread();
    reactHost.reactInstance().getBufferedRuntimeExecutor()([&trace](facebook::jsi::Runtime& runtime) {
        const facebook::jsi::Value value = runtime.global().getProperty(runtime, "gtTrace");

        trace.set_value(value.isString() ? value.getString(runtime).utf8(runtime) : "the probe never finished");
    });

    EXPECT_EQ(trace.get_future().get(), "[\"fr-CA\",\"fr\",\"de-DE\"]|null|fr|\"\"|de|42|"
                                        "threw: GtReactNative: argument 1 is missing|"
                                        "threw: Value is a number, expected a String|"
                                        "threw: GtReactNative: argument 1 is missing");
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
