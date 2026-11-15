#pragma once

#include <ReactCommon/TurboModule.h>
#include <memory>
#include <string_view>

namespace facebook::react {

/**
 * `T3Highlights` on Linux: T3 Code's `@pierre/highlights` lexer in `modules/t3-highlights`, served as a C++
 * TurboModule until Nitro runs on Linux (#152, #153). `tokenize(source, language)` keeps the Nitro method's
 * contract: `(endUtf16, tokenId)` uint32 pairs in an `ArrayBuffer`, or `undefined` when the language is not a lexer
 * ID or the lexer cannot run. The lexer is the module's own `t3_highlights_tokenize`, the C that iOS and Android run.
 *
 * JavaScript thread only. The lexer's memory is process-global and not thread-safe (`t3_highlights.h`), and only
 * the JavaScript runtime holds TurboModules. The class is in `facebook::react` because the autolinking
 * registration names it there.
 */
class T3Highlights final : public TurboModule {
public:
    static constexpr std::string_view kModuleName = "T3Highlights";

    explicit T3Highlights(std::shared_ptr<CallInvoker> jsInvoker);
};

} // namespace facebook::react
