#include "ShadowTreeTestSupport.h"

#include <gtest/gtest.h>

namespace {

TEST(UpstreamAnimatedSuiteTest, VendoredTestsDirectoryMatchesTheV0871FileList) {
    const std::filesystem::path directory{RNL_ANIMATED_TESTS_DIR};

    ASSERT_TRUE(std::filesystem::is_directory(directory));

    const std::vector<std::string> expected{
        "AnimatedNodeTests.cpp",        "AnimationDriverTests.cpp",      "AnimationTestsBase.h",
        "DecayAnimationDriverTest.cpp", "EventAnimationDriverTests.cpp", "ManagedPropsMountingOverrideTests.cpp",
    };

    EXPECT_EQ(react_native_linux::sortedUpstreamFileNames(directory), expected);
}

} // namespace
