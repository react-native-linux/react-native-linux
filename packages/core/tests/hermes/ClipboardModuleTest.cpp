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

constexpr char kExpoClipboardProbe[] = R"JAVASCRIPT(
const clipboard = globalThis.nativeModuleProxy.ExpoClipboard;
const settled = (promise) => promise.then((value) => `${value}`, (error) => `rejected: ${error.message}`);
const thrown = (call) => { try { call(); return 'returned'; } catch (error) { return `threw: ${error.message}`; } };

(async () => {
  const trace = [await clipboard.getStringAsync({}), await clipboard.hasStringAsync()];

  trace.push(await clipboard.setStringAsync(''), await clipboard.hasStringAsync());
  trace.push(await clipboard.setStringAsync('plain', { inputFormat: 'plainText' }), await clipboard.getStringAsync());
  trace.push(await clipboard.setStringAsync('no options'), await clipboard.getStringAsync({ preferredFormat: 'html' }));
  trace.push(await clipboard.setStringAsync('null options', null), await clipboard.getStringAsync());
  trace.push(await clipboard.setStringAsync('no format', { inputFormat: 1 }), await clipboard.getStringAsync());
  trace.push(await settled(clipboard.setStringAsync('<b>markup</b>', { inputFormat: 'html' })));
  trace.push(thrown(() => clipboard.setStringAsync(42)), thrown(() => clipboard.setStringAsync()));
  trace.push(typeof clipboard.getImageAsync, typeof clipboard.getUrlAsync, typeof clipboard.onClipboardChanged);
  globalThis.clipboardTrace = trace.join('|');
})();
)JAVASCRIPT";

/** Sets the clipboard to what a field copied, runs `probe` until it settles, and answers its `clipboardTrace`. */
std::string settledTrace(const char* probe) {
    ReactHost reactHost;
    std::promise<std::string> trace;

    setClipboardText("copied in a field");
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(probe), "ClipboardTest.js");
    EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
    reactHost.reactInstance().getBufferedRuntimeExecutor()([&trace](facebook::jsi::Runtime& runtime) {
        const facebook::jsi::Value value = runtime.global().getProperty(runtime, "clipboardTrace");

        trace.set_value(value.isString() ? value.getString(runtime).utf8(runtime) : "the probe never finished");
    });

    std::string answered = trace.get_future().get();

    EXPECT_FALSE(reactHost.hasReportedFatalError());

    return answered;
}

/**
 * #23: `RNCClipboard` reads and writes the one clipboard the text field's Ctrl+C and Ctrl+V use, so what a field
 * copied pastes from JavaScript and what JavaScript set pastes into a field.
 */
TEST(ClipboardModuleTest, SharesTheTextFieldClipboardWithJavaScript) {
    EXPECT_EQ(settledTrace(kClipboardProbe), "copied in a field true false from JavaScript");
    EXPECT_EQ(clipboardText(), "from JavaScript");
}

/**
 * #278: `ExpoClipboard` is upstream `expo-clipboard`'s string API over the same clipboard, so T3 Code's
 * `setStringAsync` and `getStringAsync` reach what a field copied and pastes into it. Markup is refused rather than
 * stored as text, and images, URLs and the change listener are absent, which upstream reports as unavailable.
 */
TEST(ClipboardModuleTest, ExpoClipboardAnswersStringsFromTheSameClipboard) {
    EXPECT_EQ(settledTrace(kExpoClipboardProbe),
              "copied in a field|true|true|false|true|plain|true|no options|true|null options|true|no format|"
              "rejected: The Linux clipboard holds plain text only: an inputFormat of html is not supported|"
              "threw: setStringAsync takes the text to copy as a string|"
              "threw: setStringAsync takes the text to copy as a string|undefined|undefined|undefined");
    EXPECT_EQ(clipboardText(), "no format");
}

} // namespace
} // namespace react_native_linux
