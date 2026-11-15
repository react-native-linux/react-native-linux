#include "ReactHost.h"
#include "T3NativeControls.h"

#include <ReactCommon/CxxTurboModuleUtils.h>
#include <chrono>
#include <cxxreact/JSBigString.h>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <utility>

namespace react_native_linux {
namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

constexpr char kDpopProbe[] = R"JAVASCRIPT(
const controls = globalThis.nativeModuleProxy.T3NativeControls;
const signingInput = Uint8Array.from('header.payload', (character) => character.charCodeAt(0));
const rejectionOf = (promise) => promise.then(() => 'resolved', (error) => error.message);

(async () => {
  const key = await controls.generateDpopKey();
  const privateKey = key.slice(0, 32);
  const publicKey = key.subarray(32);
  const otherKey = await controls.generateDpopKey();
  const signature = await controls.signDpop(privateKey, signingInput);

  globalThis.dpopTrace = [
    Object.keys(Object.getPrototypeOf(controls)).sort().join(','),
    key instanceof Uint8Array, key.length, publicKey[0],
    await controls.dpopKeyMatches(privateKey, publicKey),
    await controls.dpopKeyMatches(otherKey.slice(0, 32), publicKey),
    signature instanceof Uint8Array, signature.length,
    await rejectionOf(controls.signDpop(new Uint8Array(31), signingInput)),
    await rejectionOf(controls.dpopKeyMatches('key', publicKey)),
    await rejectionOf(controls.signDpop(privateKey)),
  ].join(' | ');
})();
)JAVASCRIPT";

/**
 * #640: T3 Code's `dpop.ts` reaches `T3NativeControls` as an autolinked C++ TurboModule, and each function takes
 * and returns `Uint8Array`s with the iOS and Android byte layouts: a scalar followed by its uncompressed point, a
 * key pair that matches itself and not another, a raw `r‖s` signature, and a rejection, not a crash, for bad input.
 * A `subarray` argument proves a view's byte offset is honoured. The method names are the enumerable keys of the
 * module's prototype, which is what expo-modules-core's `createTurboModuleToExpoProxy` copies for
 * `requireNativeModule`.
 */
TEST(T3NativeControlsModuleTest, AnswersDpopFromJavaScriptWithTheMobileByteLayouts) {
    facebook::react::registerCxxModuleToGlobalModuleMap(std::string(facebook::react::T3NativeControls::kModuleName),
                                                        [](std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
                                                            return std::make_shared<facebook::react::T3NativeControls>(
                                                                std::move(jsInvoker));
                                                        });

    ReactHost reactHost;
    std::promise<std::string> trace;

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kDpopProbe), "T3NativeControlsTest.js");
    EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
    reactHost.reactInstance().getBufferedRuntimeExecutor()([&trace](facebook::jsi::Runtime& runtime) {
        const facebook::jsi::Value value = runtime.global().getProperty(runtime, "dpopTrace");

        trace.set_value(value.isString() ? value.getString(runtime).utf8(runtime) : "the probe never finished");
    });

    EXPECT_EQ(trace.get_future().get(),
              "dpopKeyMatches,generateDpopKey,signDpop | true | 97 | 4 | true | false | true | 64"
              " | A DPoP private key is a 32-byte P-256 scalar."
              " | T3NativeControls: argument 0 is not a Uint8Array"
              " | T3NativeControls: argument 1 is not a Uint8Array");
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
