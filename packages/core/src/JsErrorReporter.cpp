#include "JsErrorReporter.h"

#include <iostream>
#include <string>

namespace react_native_linux {

namespace {

void printStackFrame(const facebook::react::JsErrorHandler::ProcessedError::StackFrame& stackFrame) {
    std::cerr << "    at " << stackFrame.methodName;

    if (stackFrame.file.has_value()) {
        std::cerr << " (" << stackFrame.file.value() << ':' << stackFrame.lineNumber.value_or(0) << ':'
                  << stackFrame.column.value_or(0) << ')';
    }

    std::cerr << '\n';
}

} // namespace

facebook::react::JsErrorHandler::OnJsError JsErrorReporter::createHandler() const {
    return [hasReportedFatalError = hasReportedFatalError_](
               facebook::jsi::Runtime& /*runtime*/,
               const facebook::react::JsErrorHandler::ProcessedError& processedError) {
        const std::string severity = processedError.isFatal ? "fatal" : "non-fatal";

        std::cerr << "[js-error] " << severity << ' ' << processedError.name.value_or("Error") << ": "
                  << processedError.message << '\n';

        for (const auto& stackFrame : processedError.stack) {
            printStackFrame(stackFrame);
        }

        std::cerr << std::flush;

        if (processedError.isFatal) {
            hasReportedFatalError->store(true);
        }
    };
}

bool JsErrorReporter::hasReportedFatalError() const {
    return hasReportedFatalError_->load();
}

} // namespace react_native_linux
