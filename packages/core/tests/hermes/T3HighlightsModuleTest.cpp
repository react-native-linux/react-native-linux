#include "ReactHost.h"
#include "T3Highlights.h"
#include "t3_highlights.h"

#include <ReactCommon/CxxTurboModuleUtils.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <cxxreact/JSBigString.h>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

std::vector<std::string> lexedSources;
std::array<std::uint32_t, 2> lexedRecords{};

} // namespace

/** Stands in for T3 Code's lexer: one record pair, the UTF-8 byte count and the language, or -1 for "fail". */
extern "C" int64_t t3_highlights_tokenize(const uint8_t* source, size_t length, uint8_t language,
                                          const uint32_t** records) {
    lexedSources.emplace_back(reinterpret_cast<const char*>(source), length);
    *records = nullptr;

    if (lexedSources.back() == "fail") {
        return -1;
    }

    if (length == 0) {
        return 0;
    }

    lexedRecords = {static_cast<std::uint32_t>(length), language};
    *records = lexedRecords.data();

    return static_cast<int64_t>(lexedRecords.size());
}

namespace react_native_linux {
namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

constexpr char kTokenizeProbe[] = R"JAVASCRIPT(
const highlights = globalThis.nativeModuleProxy.T3Highlights;
const pairs = (buffer) =>
  buffer instanceof ArrayBuffer ? `[${Array.from(new Uint32Array(buffer)).join(',')}]` : String(buffer);
const errorOf = (call) => { try { return `returned ${pairs(call())}`; } catch (error) { return error.message; } };

globalThis.tokenizeTrace = [
  Object.keys(Object.getPrototypeOf(highlights)).join(','),
  pairs(highlights.tokenize('é\r\n', 139)),
  pairs(highlights.tokenize('', 139)),
  pairs(highlights.tokenize('fail', 1)),
  pairs(highlights.tokenize('x', 256)),
  pairs(highlights.tokenize('x', -1)),
  pairs(highlights.tokenize('x', 1.5)),
  pairs(highlights.tokenize('x', NaN)),
  errorOf(() => highlights.tokenize('x')),
].join(' | ');
)JAVASCRIPT";

/**
 * #684: T3 Code's `highlights.ts` reaches `T3Highlights` as an autolinked C++ TurboModule with the Nitro method's
 * contract. The source reaches the lexer as UTF-8, so `é` counts two bytes. The records come back as an
 * `ArrayBuffer`, and an empty input gives an empty one. A lexer failure gives `undefined`, and so does a language
 * that is not a lexer ID, without calling the lexer. A missing argument throws instead of reading past the arguments.
 */
TEST(T3HighlightsModuleTest, TokenizesFromJavaScriptWithTheNitroContract) {
    facebook::react::registerCxxModuleToGlobalModuleMap(std::string(facebook::react::T3Highlights::kModuleName),
                                                        [](std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
                                                            return std::make_shared<facebook::react::T3Highlights>(
                                                                std::move(jsInvoker));
                                                        });

    ReactHost reactHost;
    std::promise<std::string> trace;

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kTokenizeProbe), "T3HighlightsTest.js");
    EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
    reactHost.reactInstance().getBufferedRuntimeExecutor()([&trace](facebook::jsi::Runtime& runtime) {
        const facebook::jsi::Value value = runtime.global().getProperty(runtime, "tokenizeTrace");

        trace.set_value(value.isString() ? value.getString(runtime).utf8(runtime) : "the probe never finished");
    });

    EXPECT_EQ(trace.get_future().get(), "tokenize | [4,139] | [] | undefined | undefined | undefined | undefined"
                                        " | undefined | T3Highlights.tokenize expects a source and a language");
    EXPECT_EQ(lexedSources, (std::vector<std::string>{"\xc3\xa9\r\n", "", "fail"}));
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
