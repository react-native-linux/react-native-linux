#include "ReactHost.h"
#include "SourceText.h"
#include "T3Highlights.h"
#include "t3_highlights.h"

#include <ReactCommon/CxxTurboModuleUtils.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <cxxreact/JSBigString.h>
#include <future>
#include <gtest/gtest.h>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
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
const OriginalArrayBuffer = ArrayBuffer;
const pairs = (buffer) =>
  buffer instanceof OriginalArrayBuffer ? `[${Array.from(new Uint32Array(buffer)).join(',')}]` : String(buffer);
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
  errorOf(() => {
    globalThis.ArrayBuffer = function OneByteArrayBuffer() { return new OriginalArrayBuffer(1); };
    try { return highlights.tokenize('ab', 7); } finally { globalThis.ArrayBuffer = OriginalArrayBuffer; }
  }),
].join(' | ');
)JAVASCRIPT";

/**
 * #684: T3 Code's `highlights.ts` reaches `T3Highlights` as an autolinked C++ TurboModule with the Nitro method's
 * contract. The source reaches the lexer as UTF-8, so `é` counts two bytes. The records come back as an
 * `ArrayBuffer`, and an empty input gives an empty one. A lexer failure gives `undefined`, and so does a language
 * that is not a lexer ID, without calling the lexer. A missing argument throws instead of reading past the arguments.
 * The records are copied into the buffer's own storage without calling JavaScript, so a replaced global `ArrayBuffer`
 * that would hand back one byte neither truncates them nor lets the copy overflow it.
 */
TEST(T3HighlightsModuleTest, TokenizesFromJavaScriptWithTheNitroContract) {
    lexedSources.clear();
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

    EXPECT_EQ(trace.get_future().get(),
              "tokenize | [4,139] | [] | undefined | undefined | undefined | undefined"
              " | undefined | T3Highlights.tokenize expects a source and a language | returned [2,7]");
    EXPECT_EQ(lexedSources, (std::vector<std::string>{"\xc3\xa9\r\n", "", "fail", "ab"}));
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

struct StringDataChunk {
    bool ascii;
    std::u16string_view utf16;
    std::string_view latin1;
};

std::string utf8OfChunks(std::initializer_list<StringDataChunk> chunks) {
    facebook::react::SourceText sourceText;

    for (const StringDataChunk& chunk : chunks) {
        if (chunk.ascii) {
            sourceText.append(true, chunk.latin1.data(), chunk.latin1.size());
        } else {
            sourceText.append(false, chunk.utf16.data(), chunk.utf16.size());
        }
    }

    return std::move(sourceText).finish();
}

StringDataChunk asciiChunk(std::string_view text) { return {.ascii = true, .utf16 = {}, .latin1 = text}; }

StringDataChunk utf16Chunk(std::u16string_view units) { return {.ascii = false, .utf16 = units, .latin1 = {}}; }

/**
 * #684: `jsi::String::getStringData` may hand one string over as several ASCII and UTF-16 chunks. A surrogate pair
 * split between two UTF-16 chunks is one code point, not two U+FFFD. A high surrogate that the next chunk does not
 * complete, an ASCII chunk included, or that ends the string, and a lone low surrogate each become U+FFFD.
 */
TEST(T3HighlightsModuleTest, PairsASurrogateSplitAcrossStringDataChunks) {
    EXPECT_EQ(utf8OfChunks({utf16Chunk(u"a\xD83D"), utf16Chunk(u"\xDE00z")}), "a\xF0\x9F\x98\x80z");
    EXPECT_EQ(utf8OfChunks({asciiChunk("ab"), utf16Chunk(u"\u00E9\xD83D"), asciiChunk("x"), utf16Chunk(u"\u00E9")}),
              "ab\xC3\xA9\xEF\xBF\xBDx\xC3\xA9");
    EXPECT_EQ(utf8OfChunks({utf16Chunk(u"\xD83D"), utf16Chunk(u"z")}), "\xEF\xBF\xBDz");
    EXPECT_EQ(utf8OfChunks({utf16Chunk(u"\xD83D"), utf16Chunk(u"\xD83D\xDE00")}), "\xEF\xBF\xBD\xF0\x9F\x98\x80");
    EXPECT_EQ(utf8OfChunks({utf16Chunk(u"x\xD83D")}), "x\xEF\xBF\xBD");
    EXPECT_EQ(utf8OfChunks({utf16Chunk(u"\xDE00"), asciiChunk("y")}), "\xEF\xBF\xBDy");
}

} // namespace
} // namespace react_native_linux
