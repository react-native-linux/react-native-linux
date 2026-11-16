#include "GtReactNativeModule.h"

#include "AsyncStorage.h"
#include "I18n.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <cstddef>
#include <jsi/jsi.h>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {
namespace {

using facebook::jsi::Runtime;
using facebook::jsi::Value;

constexpr const char* kDatabase = "@react-native-linux/GtReactNative";

const Value& argument(Runtime& runtime, const Value* arguments, size_t count, size_t index) {
    if (index >= count) {
        throw facebook::jsi::JSError(runtime, "GtReactNative: argument " + std::to_string(index) + " is missing");
    }

    return arguments[index];
}

std::string stringArgument(Runtime& runtime, const Value* arguments, size_t count, size_t index) {
    return argument(runtime, arguments, count, index).asString(runtime).utf8(runtime);
}

class GtReactNativeModule final : public facebook::react::TurboModule {
public:
    GtReactNativeModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, std::shared_ptr<KeyValueStore> store)
        : TurboModule("GtReactNative", std::move(jsInvoker)), store_(std::move(store)) {
        methodMap_["multiply"] = {
            2, [](Runtime& runtime, TurboModule& /*turboModule*/, const Value* arguments, size_t count) {
                return Value(argument(runtime, arguments, count, 0).asNumber() *
                             argument(runtime, arguments, count, 1).asNumber());
            }};
        methodMap_["getNativeLocales"] = {
            0, [](Runtime& runtime, TurboModule& /*turboModule*/, const Value* /*arguments*/, size_t /*count*/) {
                const std::vector<std::string> tags = preferredLanguageTags();
                facebook::jsi::Array array(runtime, tags.size());

                for (size_t index = 0; index < tags.size(); ++index) {
                    array.setValueAtIndex(runtime, index, facebook::jsi::String::createFromUtf8(runtime, tags[index]));
                }

                return Value(runtime, array);
            }};
        methodMap_["nativeStoreGet"] = {
            1, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                const std::optional<std::string> value =
                    storeOf(turboModule).get(kDatabase, {stringArgument(runtime, arguments, count, 0)}).front().second;

                return value.has_value() ? Value(runtime, facebook::jsi::String::createFromUtf8(runtime, *value))
                                         : Value::null();
            }};
        methodMap_["nativeStoreSet"] = {
            2, [](Runtime& runtime, TurboModule& turboModule, const Value* arguments, size_t count) {
                storeOf(turboModule)
                    .set(kDatabase, {{stringArgument(runtime, arguments, count, 0),
                                      stringArgument(runtime, arguments, count, 1)}});

                return Value::undefined();
            }};
    }

private:
    static KeyValueStore& storeOf(TurboModule& turboModule) {
        return *static_cast<GtReactNativeModule&>(turboModule).store_;
    }

    std::shared_ptr<KeyValueStore> store_;
};

} // namespace

std::shared_ptr<facebook::react::TurboModule>
makeGtReactNativeModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, std::shared_ptr<KeyValueStore> store) {
    return std::make_shared<GtReactNativeModule>(std::move(jsInvoker), std::move(store));
}

} // namespace react_native_linux
