#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::vector<std::string> sortedFileNames(const std::filesystem::path& directory) {
    std::vector<std::string> fileNames;

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            fileNames.push_back(entry.path().filename().string());
        }
    }

    std::sort(fileNames.begin(), fileNames.end());

    return fileNames;
}

TEST(UpstreamAnimatedSuiteTest, VendoredTestsDirectoryMatchesTheV0871FileList) {
    const std::filesystem::path directory{RNL_ANIMATED_TESTS_DIR};

    ASSERT_TRUE(std::filesystem::is_directory(directory));

    const std::vector<std::string> expected{
        "AnimatedNodeTests.cpp",
        "AnimationDriverTests.cpp",
        "AnimationTestsBase.h",
        "DecayAnimationDriverTest.cpp",
        "EventAnimationDriverTests.cpp",
        "ManagedPropsMountingOverrideTests.cpp",
    };

    EXPECT_EQ(sortedFileNames(directory), expected);
}

} // namespace
