#include "HermesJSRuntimeFactory.h"

#include <gtest/gtest.h>
#include <jsi/jsi.h>
#include <memory>
#include <string>

namespace react_native_linux {
namespace {

/**
 * #814: a Hermes built with `HERMES_SLOW_DEBUG` verifies its heap on every collection and ran T3 Code's development
 * bundle about 30 times slower, slow enough that its first screen mounted a minute after its first commit. The build
 * keeps those checks off in every configure; Hermes reports which ones it was compiled with.
 */
TEST(HermesBuildTest, ThePlatformRuntimeIsNotASlowDebugBuild) {
    const std::unique_ptr<facebook::react::JSRuntime> jsRuntime = HermesJSRuntimeFactory{}.createJSRuntime(nullptr);
    facebook::jsi::Runtime& runtime = jsRuntime->getRuntime();
    const std::string build = runtime
                                  .evaluateJavaScript(std::make_shared<facebook::jsi::StringBuffer>(
                                                          "HermesInternal.getRuntimeProperties().Build"),
                                                      "hermes-build-test.js")
                                  .getString(runtime)
                                  .utf8(runtime);

    EXPECT_NE(build, "SlowDebug");
}

} // namespace
} // namespace react_native_linux
