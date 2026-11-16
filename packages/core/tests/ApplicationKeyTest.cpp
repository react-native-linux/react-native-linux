#include "ApplicationKey.h"

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

namespace {

using react_native_linux::selectApplicationKey;

// React Native's own `AppRegistry` registers "LogBox" in every bundle, so an Expo app's keys are never "main" alone.
const std::vector<std::string> kExpoApplicationKeys{"LogBox", "main"};
const std::vector<std::string> kNamedApplicationKeys{"LogBox", "HelloWorld"};

TEST(ApplicationKeyTest, ModuleFlagWinsOverTheDefault) {
    EXPECT_EQ(selectApplicationKey("HelloWorld", kExpoApplicationKeys), "HelloWorld");
}

TEST(ApplicationKeyTest, ModuleFlagRunsEvenWhenTheBundleNeverRegisteredIt) {
    EXPECT_EQ(selectApplicationKey("HelloWorld", {}), "HelloWorld");
}

TEST(ApplicationKeyTest, WithoutTheFlagAnExpoBundleRunsMain) {
    EXPECT_EQ(selectApplicationKey(std::nullopt, kExpoApplicationKeys), "main");
}

TEST(ApplicationKeyTest, WithoutTheFlagABundleWithoutMainKeepsTheEmptySurface) {
    EXPECT_EQ(selectApplicationKey(std::nullopt, kNamedApplicationKeys), std::nullopt);
    EXPECT_EQ(selectApplicationKey(std::nullopt, {}), std::nullopt);
}

} // namespace
