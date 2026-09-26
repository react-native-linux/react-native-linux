#include <folly/json.h>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <worklets/Tools/FeatureFlags.h>
#include <worklets/Tools/WorkletsVersion.h>

namespace react_native_linux {
namespace {

std::string installedWorkletsVersion() {
    std::ifstream packageJson(RNL_WORKLETS_PACKAGE_JSON);
    std::stringstream contents;
    contents << packageJson.rdbuf();

    return folly::parseJson(contents.str())["version"].asString();
}

/**
 * Issue #134: the version rnl_worklets was compiled with is the version installed now. A package bump that was not
 * followed by a reconfigure fails here, rather than in worklets' own JavaScript-against-C++ version check at runtime.
 */
TEST(WorkletsVersionTest, TheCompiledVersionStampIsTheInstalledPackagesVersion) {
    EXPECT_EQ(worklets::getWorkletsCppVersion(), installedWorkletsVersion());
}

TEST(WorkletsVersionTest, TheStaticFeatureFlagsAreThePackagesOwnAndFetchPreviewIsOff) {
    EXPECT_FALSE(worklets::StaticFeatureFlags::getFlag("FETCH_PREVIEW_ENABLED"));
    EXPECT_TRUE(worklets::StaticFeatureFlags::getFlag("ENABLE_CROSS_RUNTIME_STACK_TRACES"));
    EXPECT_THROW(worklets::StaticFeatureFlags::getFlag("NOT_A_FLAG"), std::logic_error);
}

} // namespace
} // namespace react_native_linux
