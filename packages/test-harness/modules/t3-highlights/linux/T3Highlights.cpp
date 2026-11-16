#include "T3Highlights.h"

#include "SourceText.h"
#include "t3_highlights.h"

#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace facebook::react {
namespace {

// The lexer's memory is process-global, as in T3 Code's Nitro binding. Only the JavaScript runtime calls this today:
// the worklet UI runtime gets no TurboModule proxy on this platform, so nothing contends for the lock yet.
std::mutex lexerMutex;

class OwnedRecords final : public jsi::MutableBuffer {
public:
    explicit OwnedRecords(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}

    [[nodiscard]] std::size_t size() const override { return bytes_.size(); }

    std::uint8_t* data() override { return bytes_.data(); }

private:
    std::vector<std::uint8_t> bytes_;
};

jsi::Value tokenize(jsi::Runtime& runtime, const jsi::Value* arguments, std::size_t count) {
    if (count < 2) {
        throw jsi::JSError(runtime, "T3Highlights.tokenize expects a source and a language");
    }

    SourceText sourceText;
    auto appendChunk = [&sourceText](bool ascii, const void* data, std::size_t length) {
        sourceText.append(ascii, data, length);
    };
    arguments[0].asString(runtime).getStringData(runtime, appendChunk);
    const std::string source = std::move(sourceText).finish();
    const double language = arguments[1].asNumber();

    if (!(language >= 0 && language <= std::numeric_limits<std::uint8_t>::max())) {
        return jsi::Value::undefined();
    }

    const auto languageId = static_cast<std::uint8_t>(language);

    if (languageId != language) {
        return jsi::Value::undefined();
    }

    std::vector<std::uint8_t> bytes;
    {
        const std::lock_guard lock(lexerMutex);
        const std::uint32_t* records = nullptr;
        const std::int64_t recordCount = t3_highlights_tokenize(reinterpret_cast<const std::uint8_t*>(source.data()),
                                                                source.size(), languageId, &records);

        if (recordCount < 0) {
            return jsi::Value::undefined();
        }

        const auto* const first = reinterpret_cast<const std::uint8_t*>(records);
        bytes.assign(first, first + (static_cast<std::size_t>(recordCount) * sizeof(std::uint32_t)));
    }

    return jsi::ArrayBuffer(runtime, std::make_shared<OwnedRecords>(std::move(bytes)));
}

} // namespace

T3Highlights::T3Highlights(std::shared_ptr<CallInvoker> jsInvoker)
    : TurboModule(std::string(kModuleName), std::move(jsInvoker)) {
    methodMap_["tokenize"] = {2, [](jsi::Runtime& runtime, TurboModule& /*turboModule*/, const jsi::Value* arguments,
                                    std::size_t count) { return tokenize(runtime, arguments, count); }};
}

} // namespace facebook::react
