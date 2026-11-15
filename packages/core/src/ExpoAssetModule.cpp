#include "ExpoAsset.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <cstddef>
#include <jsi/jsi.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include <react/bridging/Bridging.h>
#include <react/bridging/Promise.h>

namespace react_native_linux {
namespace {

constexpr size_t kDownloadArgumentCount = 3;

/**
 * Threading contract: `downloadAsync` reads its arguments on the JavaScript thread and hands them to
 * `ExpoAssetDownloads`, which settles the `AsyncPromise` from the JavaScript thread for a `file://` URL and from one
 * of its workers otherwise; the promise reaches JavaScript through the call invoker either way. `TurboModuleRegistry`
 * owns the downloads and destroys them only after the JavaScript thread has quit, so the reference outlives every call.
 */
class ExpoAssetModule final : public facebook::react::TurboModule {
public:
    ExpoAssetModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, ExpoAssetDownloads& downloads)
        : TurboModule("ExpoAsset", std::move(jsInvoker)), downloads_(downloads) {
        methodMap_["downloadAsync"] = {kDownloadArgumentCount, &downloadAsync};
    }

private:
    static facebook::jsi::Value downloadAsync(facebook::jsi::Runtime& runtime, TurboModule& turboModule,
                                              const facebook::jsi::Value* arguments, size_t count) {
        if (count < kDownloadArgumentCount) {
            throw facebook::jsi::JSError(runtime, "ExpoAsset.downloadAsync takes a url, an md5Hash and a type");
        }

        auto& module = static_cast<ExpoAssetModule&>(turboModule);
        const std::optional<std::string> md5Hash =
            arguments[1].isString() ? std::optional(arguments[1].getString(runtime).utf8(runtime)) : std::nullopt;
        facebook::react::AsyncPromise<std::string> promise(runtime, module.jsInvoker_);
        facebook::jsi::Value promiseObject = promise.get(runtime);

        module.downloads_.download(
            arguments[0].asString(runtime).utf8(runtime), md5Hash, arguments[2].asString(runtime).utf8(runtime),
            [promise](const std::string& fileUrl) mutable { promise.resolve(fileUrl); },
            [promise](const std::string& message) mutable { promise.reject(facebook::react::Error(message)); });

        return promiseObject;
    }

    ExpoAssetDownloads& downloads_;
};

} // namespace

std::shared_ptr<facebook::react::TurboModule>
makeExpoAssetModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, ExpoAssetDownloads& downloads) {
    return std::make_shared<ExpoAssetModule>(std::move(jsInvoker), downloads);
}

} // namespace react_native_linux
