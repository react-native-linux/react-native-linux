#include "ExpoSharingModule.h"

#include "ExpoFileSystem.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <jsi/jsi.h>
#include <memory>
#include <string>
#include <utility>

#include <react/bridging/Bridging.h>
#include <react/bridging/Promise.h>

namespace react_native_linux {
namespace {

using facebook::jsi::Runtime;
using facebook::jsi::Value;
using facebook::react::TurboModule;

/**
 * Threading contract: arguments are read on the JavaScript thread; `FileSharing`'s functions run on its worker, which
 * settles each `AsyncPromise` back on the JavaScript thread through the call invoker. The module holds the sharing
 * object by reference because `TurboModuleRegistry` owns it and outlives every JavaScript call.
 */
class ExpoSharingModule final : public TurboModule {
public:
    ExpoSharingModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, FileSharing& sharing)
        : TurboModule("ExpoSharing", std::move(jsInvoker)), sharing_(sharing) {
        methodMap_["isAvailableAsync"] = {0, [](Runtime& runtime, TurboModule& turboModule, const Value*, size_t) {
                                              auto& module = static_cast<ExpoSharingModule&>(turboModule);
                                              facebook::react::AsyncPromise<bool> promise(runtime, module.jsInvoker_);
                                              Value promiseObject = promise.get(runtime);

                                              module.sharing_.worker.post(
                                                  [promise = std::move(promise), &sharing = module.sharing_]() mutable {
                                                      promise.resolve(sharing.isAvailable());
                                                  });

                                              return promiseObject;
                                          }};
        methodMap_["shareAsync"] = {
            2, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                auto& module = static_cast<ExpoSharingModule&>(turboModule);
                std::filesystem::path file = pathFromFileUri(
                    (count == 0 ? Value::undefined() : Value(runtime, arguments[0])).asString(runtime).utf8(runtime));
                facebook::react::AsyncPromise<> promise(runtime, module.jsInvoker_);
                Value promiseObject = promise.get(runtime);

                module.sharing_.worker.post(
                    [promise = std::move(promise), file = std::move(file), &sharing = module.sharing_]() mutable {
                        try {
                            sharing.share(file);
                            promise.resolve();
                        } catch (const std::exception& error) {
                            promise.reject(
                                facebook::react::Error(std::string("Failed to share the file: ") + error.what()));
                        }
                    });

                return promiseObject;
            }};
        methodMap_["getSharedPayloads"] = {0, [](Runtime& runtime, TurboModule&, const Value*, size_t) {
                                               return Value(facebook::jsi::Array(runtime, 0));
                                           }};
        methodMap_["getResolvedSharedPayloadsAsync"] = {
            0, [](Runtime& runtime, TurboModule&, const Value*, size_t) {
                facebook::jsi::Object promiseConstructor = runtime.global().getPropertyAsObject(runtime, "Promise");

                return promiseConstructor.getPropertyAsFunction(runtime, "resolve")
                    .callWithThis(runtime, promiseConstructor, facebook::jsi::Array(runtime, 0));
            }};
        methodMap_["clearSharedPayloads"] = {0, [](Runtime&, TurboModule&, const Value*, size_t) { return Value(); }};
    }

private:
    FileSharing& sharing_;
};

} // namespace

std::shared_ptr<TurboModule> makeExpoSharingModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                                                   FileSharing& sharing) {
    return std::make_shared<ExpoSharingModule>(std::move(jsInvoker), sharing);
}

} // namespace react_native_linux
