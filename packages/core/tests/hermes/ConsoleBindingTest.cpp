#include "ConsoleBinding.h"

#include <gtest/gtest.h>
#include <hermes/hermes.h>
#include <jsi/jsi.h>
#include <memory>
#include <string>

namespace react_native_linux {
namespace {

std::string captureStandardError(const std::string& source) {
    const std::unique_ptr<facebook::jsi::Runtime> runtime = facebook::hermes::makeHermesRuntime();

    installConsoleBinding(*runtime);
    testing::internal::CaptureStderr();
    runtime->evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(source), "console-binding-test.js");

    return testing::internal::GetCapturedStderr();
}

/**
 * #798: the T3 Code fixture tells an error from a log line by this tag alone, because the trace folds standard error
 * into standard output. React Native's console polyfill reaches native code through both `console.error` and
 * `nativeLoggingHook` at its error level, 3, so both carry the tag and a warning, level 2, does not.
 */
TEST(ConsoleBindingTest, ConsoleErrorAndTheLoggingHookAtErrorLevelAreTaggedAndWarningsAreNot) {
    EXPECT_EQ(captureStandardError("console.error('x', 1); nativeLoggingHook('hook error', 3);"
                                   "console.warn('w'); nativeLoggingHook('hook warning', 2);"),
              "[console.error] x 1\n[console.error] hook error\nw\nhook warning\n");
}

} // namespace
} // namespace react_native_linux
