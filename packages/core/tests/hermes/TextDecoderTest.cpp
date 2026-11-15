#include "HermesJSRuntimeFactory.h"

#include <gtest/gtest.h>
#include <jsi/jsi.h>
#include <memory>
#include <string>

namespace react_native_linux {
namespace {

std::string evaluate(facebook::jsi::Runtime& runtime, const std::string& expression) {
    const std::string source = "(() => { try { return JSON.stringify(" + expression +
                               "); } catch (error) { return error.name + ': ' + error.message; } })()";

    return runtime.evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(source), "text-decoder-test.js")
        .getString(runtime)
        .utf8(runtime);
}

/**
 * #641: T3 Code's bundle constructs a global `TextDecoder` at startup. It is Hermes' own contrib extension, so this
 * goes through the platform's runtime factory rather than a bare `makeHermesRuntime()`.
 */
TEST(TextDecoderTest, ThePlatformRuntimeDecodesUtf8IncludingMultibyteSequencesSplitAcrossStreamedChunks) {
    const std::unique_ptr<facebook::react::JSRuntime> jsRuntime = HermesJSRuntimeFactory{}.createJSRuntime(nullptr);
    facebook::jsi::Runtime& runtime = jsRuntime->getRuntime();

    EXPECT_EQ(evaluate(runtime, "new TextDecoder().encoding"), R"("utf-8")");
    EXPECT_EQ(evaluate(runtime, "new TextDecoder().decode(new Uint8Array([0x68, 0x69, 0x20, 0xC3, 0xA9, 0xE2, 0x82, "
                                "0xAC, 0xF0, 0x9F, 0x98, 0x80]))"),
              "\"hi é€\U0001F600\"");
    EXPECT_EQ(evaluate(runtime, "((decoder) => decoder.decode(new Uint8Array([0xE2, 0x82]), {stream: true}) + "
                                "decoder.decode(new Uint8Array([0xAC])))(new TextDecoder())"),
              "\"€\"");
    EXPECT_EQ(evaluate(runtime, "new TextDecoder('utf-8', {fatal: true}).decode(new Uint8Array([0xFF]))"),
              "TypeError: Invalid byte sequence");
}

} // namespace
} // namespace react_native_linux
