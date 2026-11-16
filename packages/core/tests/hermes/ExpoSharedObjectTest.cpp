#include "ExpoSharedObject.h"

#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <hermes/hermes.h>
#include <jsi/instrumentation.h>
#include <jsi/jsi.h>
#include <memory>
#include <string>
#include <thread>
#include <utility>

namespace react_native_linux {
namespace {

constexpr std::chrono::milliseconds kFinalizerBudget{5000};
constexpr std::chrono::milliseconds kFinalizerPoll{1};

std::atomic<int> liveProbeStates = 0;

/** Hermes destroys a collected object's native state on its finalizer thread, after `collectGarbage` returns. */
bool probeStatesFreedWithinBudget() {
    const auto deadline = std::chrono::steady_clock::now() + kFinalizerBudget;

    while (liveProbeStates.load() != 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(kFinalizerPoll);
    }

    return liveProbeStates.load() == 0;
}

struct ProbeState final : facebook::jsi::NativeState {
    explicit ProbeState(std::string label) : label(std::move(label)) { ++liveProbeStates; }

    ProbeState(const ProbeState&) = delete;
    ProbeState(ProbeState&&) = delete;
    ProbeState& operator=(const ProbeState&) = delete;
    ProbeState& operator=(ProbeState&&) = delete;

    ~ProbeState() override { --liveProbeStates; }

    std::string label;
};

// What expo-modules-core's web polyfill installs, reduced to the shape the class extends.
constexpr char kPolyfill[] = R"JAVASCRIPT(
class EventEmitter { addListener() {} }
class SharedObject extends EventEmitter { release() {} }
globalThis.expo = { SharedObject };
)JAVASCRIPT";

// What upstream's File.ts does with ExpoFileSystem.FileSystemFile: extend it, call super, and hand instances back.
constexpr char kProbe[] = R"JAVASCRIPT((() => {
class Labelled extends globalThis.ProbeObject {
  constructor(label) {
    super(label);
    this.fromJavaScript = true;
  }
  describe() { return `${this.label}:${this.fromJavaScript}`; }
}
const first = new Labelled('first');
const second = new Labelled('second');
const trace = [
  first instanceof Labelled && first instanceof globalThis.ProbeObject && first instanceof globalThis.expo.SharedObject,
  typeof first.addListener,
  first.describe(),
  first.isSame(first),
  first.isSame(second),
  first.labelOf(second),
];
first.release();
try {
  first.describe();
} catch (error) {
  trace.push(String(error.message.includes('not a live native shared object')));
}
globalThis.second = second;
return JSON.stringify(trace);
})();
)JAVASCRIPT";

facebook::jsi::Function makeProbeClass(facebook::jsi::Runtime& runtime) {
    return makeSharedObjectClass(
        runtime,
        [](facebook::jsi::Runtime& initializeRuntime, const facebook::jsi::Value* arguments, size_t /*count*/) {
            return std::make_shared<ProbeState>(arguments[0].asString(initializeRuntime).utf8(initializeRuntime));
        },
        {{"isSame",
          [](facebook::jsi::Runtime& methodRuntime, const facebook::jsi::Value& thisValue,
             const facebook::jsi::Value* arguments, size_t /*count*/) {
              return facebook::jsi::Value(sharedObjectState<ProbeState>(methodRuntime, thisValue) ==
                                          sharedObjectState<ProbeState>(methodRuntime, arguments[0]));
          }},
         {"labelOf",
          [](facebook::jsi::Runtime& methodRuntime, const facebook::jsi::Value&, const facebook::jsi::Value* arguments,
             size_t /*count*/) {
              return facebook::jsi::Value(facebook::jsi::String::createFromUtf8(
                  methodRuntime, sharedObjectState<ProbeState>(methodRuntime, arguments[0])->label));
          }}},
        {{"label", [](facebook::jsi::Runtime& getterRuntime, const facebook::jsi::Value& thisValue,
                      const facebook::jsi::Value*, size_t) {
              return facebook::jsi::Value(facebook::jsi::String::createFromUtf8(
                  getterRuntime, sharedObjectState<ProbeState>(getterRuntime, thisValue)->label));
          }}});
}

std::string evaluate(facebook::jsi::Runtime& runtime, const char* source) {
    const facebook::jsi::Value result =
        runtime.evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(source), "ExpoSharedObjectTest.js");

    return result.isString() ? result.getString(runtime).utf8(runtime) : "";
}

/** A runtime with the polyfill's `SharedObject` and the probe class as `globalThis.ProbeObject`. */
std::unique_ptr<facebook::jsi::Runtime> makeProbeRuntime() {
    std::unique_ptr<facebook::jsi::Runtime> runtime = facebook::hermes::makeHermesRuntime();

    evaluate(*runtime, kPolyfill);
    runtime->global().setProperty(*runtime, "ProbeObject", makeProbeClass(*runtime));

    return runtime;
}

/**
 * #663's constructor-and-release probe. JavaScript subclasses a native class from `makeSharedObjectClass`, which
 * extends `globalThis.expo.SharedObject`; native methods resolve the instances passed back to them by their native
 * state, so identity crosses the boundary; `release()` frees that state at once and leaves the instance unusable,
 * and an instance never released is freed when the runtime collects it.
 */
TEST(ExpoSharedObjectTest, JavaScriptSubclassesCrossTheBoundaryByIdentityAndReleaseFreesTheNativeState) {
    const std::unique_ptr<facebook::jsi::Runtime> runtime = makeProbeRuntime();

    EXPECT_EQ(evaluate(*runtime, kProbe), R"([true,"function","first:true",true,false,"second","true"])");
    EXPECT_EQ(liveProbeStates.load(), 1);

    runtime->global().setProperty(*runtime, "second", facebook::jsi::Value::undefined());
    runtime->instrumentation().collectGarbage("ExpoSharedObjectTest");
    EXPECT_TRUE(probeStatesFreedWithinBudget());
}

TEST(ExpoSharedObjectTest, ANativeMethodRejectsAnObjectThatIsNotAnInstance) {
    const std::unique_ptr<facebook::jsi::Runtime> runtime = makeProbeRuntime();

    EXPECT_EQ(evaluate(*runtime, R"JAVASCRIPT(
const probe = new globalThis.ProbeObject('probe');
const outcomes = [];
for (const other of [{}, 1]) {
  try {
    probe.isSame(other);
  } catch (error) {
    outcomes.push(error.message.includes('not a live native shared object'));
  }
}
JSON.stringify(outcomes);
)JAVASCRIPT"),
              "[true,true]");
}

} // namespace
} // namespace react_native_linux
