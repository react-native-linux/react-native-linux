#include "ReactHost.h"

#include <chrono>
#include <cstdlib>
#include <cxxreact/JSBigString.h>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>

namespace react_native_linux {
namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

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

/** Sets `name` for one test and puts back what was there before. */
class ScopedVariable final {
public:
    ScopedVariable(const char* name, const char* value) : name_(name) {
        const char* saved = std::getenv(name);

        if (saved != nullptr) {
            saved_ = saved;
        }

        setenv(name, value, 1);
    }

    ScopedVariable(const ScopedVariable&) = delete;
    ScopedVariable(ScopedVariable&&) = delete;
    ScopedVariable& operator=(const ScopedVariable&) = delete;
    ScopedVariable& operator=(ScopedVariable&&) = delete;

    ~ScopedVariable() {
        if (saved_.has_value()) {
            setenv(name_, saved_->c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }

private:
    const char* name_;
    std::optional<std::string> saved_;
};

/**
 * #647: `gt-react-native` requires `GtReactNative` with `getEnforcing` as it is imported, which is T3 Code's first
 * fatal on Linux. The module answers the user's languages from the POSIX environment, best first, and keeps the
 * locale the user picked in its own store; a call missing an argument or passing the wrong type throws.
 */
TEST(GtReactNativeModuleTest, AnswersTheEnvironmentLanguagesAndStoresThePickedLocale) {
    const ScopedVariable lcAll("LC_ALL", "");
    const ScopedVariable lcMessages("LC_MESSAGES", "");
    const ScopedVariable lang("LANG", "de_DE.UTF-8");
    const ScopedVariable language("LANGUAGE", "fr_CA:fr");
    ReactHost reactHost;
    std::promise<std::string> trace;

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kGtReactNativeProbe),
                         "GtReactNativeModuleTest.js");
    EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
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
