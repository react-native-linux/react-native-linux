#include "ClipboardModules.h"

#include "Clipboard.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <ReactCommon/TurboModuleUtils.h>
#include <cstddef>
#include <initializer_list>
#include <jsi/jsi.h>
#include <memory>
#include <string>
#include <utility>

namespace react_native_linux {
namespace {

struct ClipboardMethod {
    const char* name;
    size_t argumentCount;
    facebook::jsi::Value (*invoke)(facebook::jsi::Runtime&, facebook::react::TurboModule&, const facebook::jsi::Value*,
                                   size_t);
};

class ClipboardModule final : public facebook::react::TurboModule {
public:
    ClipboardModule(std::string name, std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                    std::initializer_list<ClipboardMethod> methods)
        : TurboModule(std::move(name), std::move(jsInvoker)) {
        for (const ClipboardMethod& method : methods) {
            methodMap_[method.name] = {method.argumentCount, method.invoke};
        }
    }
};

facebook::jsi::Value resolved(facebook::jsi::Runtime& runtime, facebook::jsi::Value value) {
    return facebook::react::createPromiseAsJSIValue(
        runtime, [&value](facebook::jsi::Runtime& /*promiseRuntime*/,
                          const std::shared_ptr<facebook::react::Promise>& promise) { promise->resolve(value); });
}

facebook::jsi::Value getText(facebook::jsi::Runtime& runtime, facebook::react::TurboModule& /*turboModule*/,
                             const facebook::jsi::Value* /*arguments*/, size_t /*count*/) {
    return resolved(runtime, facebook::jsi::String::createFromUtf8(runtime, clipboardText()));
}

facebook::jsi::Value hasText(facebook::jsi::Runtime& runtime, facebook::react::TurboModule& /*turboModule*/,
                             const facebook::jsi::Value* /*arguments*/, size_t /*count*/) {
    return resolved(runtime, facebook::jsi::Value(!clipboardText().empty()));
}

facebook::jsi::Value setString(facebook::jsi::Runtime& runtime, facebook::react::TurboModule& /*turboModule*/,
                               const facebook::jsi::Value* arguments, size_t count) {
    setClipboardText(count > 0 && arguments[0].isString() ? arguments[0].getString(runtime).utf8(runtime)
                                                          : std::string());

    return facebook::jsi::Value::undefined();
}

bool asksForHtml(facebook::jsi::Runtime& runtime, const facebook::jsi::Value* arguments, size_t count) {
    if (count < 2 || !arguments[1].isObject()) {
        return false;
    }

    const facebook::jsi::Value format = arguments[1].getObject(runtime).getProperty(runtime, "inputFormat");

    return format.isString() && format.getString(runtime).utf8(runtime) == "html";
}

facebook::jsi::Value setStringAsync(facebook::jsi::Runtime& runtime, facebook::react::TurboModule& /*turboModule*/,
                                    const facebook::jsi::Value* arguments, size_t count) {
    if (count == 0 || !arguments[0].isString()) {
        throw facebook::jsi::JSError(runtime, "setStringAsync takes the text to copy as a string");
    }

    if (asksForHtml(runtime, arguments, count)) {
        return facebook::react::createPromiseAsJSIValue(
            runtime,
            [](facebook::jsi::Runtime& /*promiseRuntime*/, const std::shared_ptr<facebook::react::Promise>& promise) {
                promise->reject("The Linux clipboard holds plain text only: an inputFormat of html is not supported");
            });
    }

    setClipboardText(arguments[0].getString(runtime).utf8(runtime));

    return resolved(runtime, facebook::jsi::Value(true));
}

} // namespace

std::shared_ptr<facebook::react::TurboModule>
makeClipboardModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
    return std::make_shared<ClipboardModule>("RNCClipboard", std::move(jsInvoker),
                                             std::initializer_list<ClipboardMethod>{{"getString", 0, &getText},
                                                                                    {"hasString", 0, &hasText},
                                                                                    {"setString", 1, &setString}});
}

std::shared_ptr<facebook::react::TurboModule>
makeExpoClipboardModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
    return std::make_shared<ClipboardModule>(
        "ExpoClipboard", std::move(jsInvoker),
        std::initializer_list<ClipboardMethod>{
            {"getStringAsync", 1, &getText}, {"hasStringAsync", 0, &hasText}, {"setStringAsync", 2, &setStringAsync}});
}

} // namespace react_native_linux
