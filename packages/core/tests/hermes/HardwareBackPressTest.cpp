#include "FabricHost.h"
#include "OnJavaScriptThread.h"

#include <cxxreact/JSBigString.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <thread>

namespace react_native_linux {
namespace {

constexpr facebook::react::Size kSurfaceSize{.width = 400, .height = 300};

// The subscription `BackHandler.android.js` makes, through the global emitter `RCTDeviceEventEmitter` installs: one
// `hardwareBackPress` listener, which records the thread it ran on and the payload it was given.
constexpr char kBackHandlerProbe[] = R"JAVASCRIPT(
globalThis.backPresses = [];
globalThis.__rctDeviceEventEmitter = {
  emit: (eventName, ...payload) => {
    globalThis.recordListenerThread();
    globalThis.backPresses.push(`${eventName} with ${payload.length} arguments`);
  },
};
)JAVASCRIPT";

/**
 * #788: an Escape dispatched on the owning thread, as the frame thread dispatches it, reaches a `hardwareBackPress`
 * listener on the JavaScript thread through the runtime executor, once, with no payload. The repeat that follows a
 * held Escape does not reach it. This is the frame-thread to JavaScript-thread handoff the TSan job exercises.
 */
TEST(HardwareBackPressTest, AnEscapeReachesTheListenerOnTheJavaScriptThread) {
    ReactHost reactHost;
    auto fabricHost = std::make_unique<FabricHost>(reactHost, kSurfaceSize);

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kBackHandlerProbe), "BackHandlerProbe.js");
    reactHost.drainJavaScriptThread();

    std::thread::id listenerThread;
    const std::thread::id javaScriptThread =
        onJavaScriptThread<std::thread::id>(reactHost, [&listenerThread](facebook::jsi::Runtime& runtime) {
            runtime.global().setProperty(
                runtime, "recordListenerThread",
                facebook::jsi::Function::createFromHostFunction(
                    runtime, facebook::jsi::PropNameID::forAscii(runtime, "recordListenerThread"), 0,
                    [&listenerThread](facebook::jsi::Runtime& /*hostRuntime*/,
                                      const facebook::jsi::Value& /*thisValue*/,
                                      const facebook::jsi::Value* /*arguments*/, size_t /*count*/) {
                        listenerThread = std::this_thread::get_id();

                        return facebook::jsi::Value::undefined();
                    }));

            return std::this_thread::get_id();
        });

    fabricHost->dispatchInput({InputEvent{.kind = InputEventKind::KeyPress, .key = "Escape"},
                               InputEvent{.kind = InputEventKind::KeyPress, .key = "Escape", .repeat = true}});

    const std::string backPresses = onJavaScriptThread<std::string>(reactHost, [](facebook::jsi::Runtime& runtime) {
        return runtime.global()
            .getPropertyAsObject(runtime, "backPresses")
            .getPropertyAsFunction(runtime, "join")
            .callWithThis(runtime, runtime.global().getPropertyAsObject(runtime, "backPresses"), "|")
            .getString(runtime)
            .utf8(runtime);
    });

    EXPECT_EQ(backPresses, "hardwareBackPress with 0 arguments");
    EXPECT_EQ(listenerThread, javaScriptThread);
    EXPECT_NE(listenerThread, std::this_thread::get_id());

    fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    fabricHost.reset();

    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
