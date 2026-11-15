#include "T3Highlights.h"

#include "t3_highlights.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace facebook::react {
namespace {

jsi::Value tokenize(jsi::Runtime& runtime, const jsi::Value* arguments, std::size_t count) {
    if (count < 2) {
        throw jsi::JSError(runtime, "T3Highlights.tokenize expects a source and a language");
    }

    const std::string source = arguments[0].asString(runtime).utf8(runtime);
    const double language = arguments[1].asNumber();

    if (!(language >= 0 && language <= std::numeric_limits<std::uint8_t>::max())) {
        return jsi::Value::undefined();
    }

    const auto languageId = static_cast<std::uint8_t>(language);

    if (languageId != language) {
        return jsi::Value::undefined();
    }

    const std::uint32_t* records = nullptr;
    const std::int64_t recordCount = t3_highlights_tokenize(reinterpret_cast<const std::uint8_t*>(source.data()),
                                                            source.size(), languageId, &records);

    if (recordCount < 0) {
        return jsi::Value::undefined();
    }

    const auto byteCount = static_cast<std::size_t>(recordCount) * sizeof(std::uint32_t);
    jsi::ArrayBuffer buffer = runtime.global()
                                  .getPropertyAsFunction(runtime, "ArrayBuffer")
                                  .callAsConstructor(runtime, static_cast<double>(byteCount))
                                  .getObject(runtime)
                                  .getArrayBuffer(runtime);

    std::copy_n(reinterpret_cast<const std::uint8_t*>(records), byteCount, buffer.data(runtime));

    return buffer;
}

} // namespace

T3Highlights::T3Highlights(std::shared_ptr<CallInvoker> jsInvoker)
    : TurboModule(std::string(kModuleName), std::move(jsInvoker)) {
    methodMap_["tokenize"] = {2, [](jsi::Runtime& runtime, TurboModule& /*turboModule*/, const jsi::Value* arguments,
                                    std::size_t count) { return tokenize(runtime, arguments, count); }};
}

} // namespace facebook::react
