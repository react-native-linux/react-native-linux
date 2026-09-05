#include "PinnedFontFamilies.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>
#include <vector>

namespace {

using react_native_linux::PinnedFontFamilyResolution;
using react_native_linux::pinnedFontFamiliesFatalMessage;

TEST(PinnedFontFamiliesTest, EveryPinnedFamilyResolvedIsNotFatal) {
    const std::vector<PinnedFontFamilyResolution> resolutions{{"Noto Sans", true}, {"Noto Color Emoji", true}};

    EXPECT_EQ(pinnedFontFamiliesFatalMessage(resolutions), std::nullopt);
}

TEST(PinnedFontFamiliesTest, NoFamiliesToCheckIsNotFatal) {
    EXPECT_EQ(pinnedFontFamiliesFatalMessage({}), std::nullopt);
}

TEST(PinnedFontFamiliesTest, OneMissingFamilyNamesItAndTheVendorCommand) {
    const std::vector<PinnedFontFamilyResolution> resolutions{{"Noto Sans", true}, {"Noto Color Emoji", false}};
    const std::optional<std::string> message = pinnedFontFamiliesFatalMessage(resolutions);

    ASSERT_TRUE(message.has_value());
    EXPECT_NE(message->find("\"Noto Color Emoji\""), std::string::npos);
    EXPECT_EQ(message->find("\"Noto Sans\""), std::string::npos);
    EXPECT_NE(message->find("scripts/fonts.lock.json"), std::string::npos);
    EXPECT_NE(message->find("pnpm --filter @react-native-linux/core vendor:fonts"), std::string::npos);
}

TEST(PinnedFontFamiliesTest, EveryMissingFamilyIsNamed) {
    const std::vector<PinnedFontFamilyResolution> resolutions{{"Noto Sans", false}, {"Noto Color Emoji", false}};
    const std::optional<std::string> message = pinnedFontFamiliesFatalMessage(resolutions);

    ASSERT_TRUE(message.has_value());
    EXPECT_NE(message->find("\"Noto Sans\""), std::string::npos);
    EXPECT_NE(message->find("\"Noto Color Emoji\""), std::string::npos);
}

}  // namespace
