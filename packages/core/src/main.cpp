#include <hermes/hermes.h>
#include <jsi/jsi.h>

#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>

namespace {

facebook::jsi::Value hostPrint(facebook::jsi::Runtime& runtime, const facebook::jsi::Value& /*thisValue*/,
                               const facebook::jsi::Value* arguments, size_t argumentCount) {
    for (size_t index = 0; index < argumentCount; ++index) {
        std::puts(arguments[index].toString(runtime).utf8(runtime).c_str());
    }

    return facebook::jsi::Value::undefined();
}

} // namespace

int main() {
    const std::unique_ptr<facebook::hermes::HermesRuntime> hermesRuntime = facebook::hermes::makeHermesRuntime();
    facebook::jsi::Runtime& runtime = *hermesRuntime;

    runtime.global().setProperty(
        runtime, "print",
        facebook::jsi::Function::createFromHostFunction(runtime, facebook::jsi::PropNameID::forAscii(runtime, "print"),
                                                        1, hostPrint));

    runtime.evaluateJavaScript(
        std::make_shared<facebook::jsi::StringBuffer>("print('react-native-linux: hermes alive');"), "hello.js");

    return 0;
}
