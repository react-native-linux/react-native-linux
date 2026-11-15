#pragma once

#include <ReactCommon/TurboModule.h>
#include <memory>
#include <string_view>

namespace facebook::react {

/**
 * `T3NativeControls` on Linux: the Expo module T3 Code's `requireNativeModule("T3NativeControls")` resolves, served
 * as a C++ TurboModule under the Expo module's name. It answers the three functions `dpop.ts` calls on every
 * platform, `generateDpopKey`, `dpopKeyMatches` and `signDpop`, with the iOS and Android byte layouts. The rest of
 * the module is per-platform presentation that T3 Code calls only from `.ios` and `.android` files.
 *
 * Every method runs on the JavaScript thread and settles its promise before returning: a P-256 operation costs
 * less than a hop to a worker thread and back. The class is in `facebook::react` because the autolinking
 * registration names it there, as the `cpp-library` template's does.
 */
class T3NativeControls final : public TurboModule {
public:
    static constexpr std::string_view kModuleName = "T3NativeControls";

    explicit T3NativeControls(std::shared_ptr<CallInvoker> jsInvoker);
};

} // namespace facebook::react
