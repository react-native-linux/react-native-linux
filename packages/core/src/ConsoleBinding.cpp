#include "ConsoleBinding.h"

#include <cstddef>
#include <iostream>
#include <ostream>
#include <string>
#include <string_view>

#include <react/runtime/JSRuntimeBindings.h>

namespace react_native_linux {

namespace {

constexpr unsigned int kFirstStandardErrorLogLevel = 2;
constexpr unsigned int kErrorLogLevel = 3;

/**
 * Prefixes what `console.error` prints, so a trace that folds standard error into standard output can still tell an
 * error from a log line: the T3 Code fixture (#798) fails on any.
 */
constexpr std::string_view kConsoleErrorTag = "[console.error] ";

std::string joinArguments(facebook::jsi::Runtime& runtime, const facebook::jsi::Value* arguments,
                          size_t argumentCount) {
    std::string message;

    for (size_t index = 0; index < argumentCount; ++index) {
        if (index > 0) {
            message += ' ';
        }

        message += arguments[index].toString(runtime).utf8(runtime);
    }

    return message;
}

void installConsoleMethod(facebook::jsi::Runtime& runtime, facebook::jsi::Object& console, const char* methodName,
                          std::ostream& stream, std::string_view tag = {}) {
    facebook::jsi::Function method = facebook::jsi::Function::createFromHostFunction(
        runtime, facebook::jsi::PropNameID::forAscii(runtime, methodName), 1,
        [&stream, tag](facebook::jsi::Runtime& hostRuntime, const facebook::jsi::Value& /*thisValue*/,
                       const facebook::jsi::Value* arguments, size_t argumentCount) {
            stream << tag << joinArguments(hostRuntime, arguments, argumentCount) << std::endl;

            return facebook::jsi::Value::undefined();
        });

    console.setProperty(runtime, methodName, method);
}

} // namespace

void installConsoleBinding(facebook::jsi::Runtime& runtime) {
    facebook::jsi::Object console(runtime);

    installConsoleMethod(runtime, console, "log", std::cout);
    installConsoleMethod(runtime, console, "info", std::cout);
    installConsoleMethod(runtime, console, "debug", std::cout);
    installConsoleMethod(runtime, console, "trace", std::cout);
    installConsoleMethod(runtime, console, "warn", std::cerr);
    installConsoleMethod(runtime, console, "error", std::cerr, kConsoleErrorTag);

    runtime.global().setProperty(runtime, "console", console);

    facebook::react::bindNativeLogger(runtime, [](const std::string& message, unsigned int logLevel) {
        std::ostream& stream = logLevel >= kFirstStandardErrorLogLevel ? std::cerr : std::cout;

        stream << (logLevel >= kErrorLogLevel ? kConsoleErrorTag : std::string_view{}) << message << std::endl;
    });
}

} // namespace react_native_linux
