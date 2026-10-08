#include "Clipboard.h"
#include "ReactHost.h"

#include <chrono>
#include <cxxreact/JSBigString.h>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace react_native_linux {
namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

constexpr char kClipboardProbe[] = R"JAVASCRIPT(
const clipboard = globalThis.nativeModuleProxy.RNCClipboard;

(async () => {
  const pasted = await clipboard.getString();
  const hadPasted = await clipboard.hasString();

  clipboard.setString('');
  const hadEmpty = await clipboard.hasString();

  clipboard.setString('from JavaScript');
  globalThis.clipboardTrace = `${pasted} ${hadPasted} ${hadEmpty} ${await clipboard.getString()}`;
})();
)JAVASCRIPT";

/**
 * #23: `RNCClipboard` reads and writes the one clipboard the text field's Ctrl+C and Ctrl+V use, so what a field
 * copied pastes from JavaScript and what JavaScript set pastes into a field.
 */
TEST(ClipboardModuleTest, SharesTheTextFieldClipboardWithJavaScript) {
    ReactHost reactHost;
    std::promise<std::string> trace;

    setClipboardText("copied in a field");
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kClipboardProbe), "ClipboardTest.js");
    EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
    reactHost.reactInstance().getBufferedRuntimeExecutor()([&trace](facebook::jsi::Runtime& runtime) {
        const facebook::jsi::Value value = runtime.global().getProperty(runtime, "clipboardTrace");

        trace.set_value(value.isString() ? value.getString(runtime).utf8(runtime) : "the probe never finished");
    });

    EXPECT_EQ(trace.get_future().get(), "copied in a field true false from JavaScript");
    EXPECT_EQ(clipboardText(), "from JavaScript");
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
