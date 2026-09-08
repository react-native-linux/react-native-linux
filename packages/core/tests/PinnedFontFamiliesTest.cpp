#include "PinnedFontFamilies.h"

#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

namespace {

using react_native_linux::pinnedFontFamiliesFatalMessage;
using react_native_linux::PinnedFontFamilyResolution;
using react_native_linux::resolvedStyleIsPinnedDefault;

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

TEST(PinnedFontFamiliesTest, TheUprightNormalWeightNormalWidthStyleIsThePinnedDefault) {
    EXPECT_TRUE(resolvedStyleIsPinnedDefault(/* weight */ 400, /* width */ 5, /* slant */ 0));
}

// The #372/CodeRabbit regression, measured against a real `SkFontMgr_New_Custom_Directory`: with
// `NotoSans-Regular.ttf` missing and only `NotoSans-Bold.ttf`/`NotoSans-Italic.ttf` left, `matchFamily("Noto
// Sans")` is still non-empty (a family-only check would call this resolved), and
// `matchFamilyStyle("Noto Sans", SkFontStyle())`'s nearest-match fallback silently returns the italic face
// — weight 400, matching the request, but slant 1, not upright.
TEST(PinnedFontFamiliesTest, OnlyANonRegularNotoSansFaceIsNotThePinnedDefaultStyle) {
    EXPECT_FALSE(resolvedStyleIsPinnedDefault(/* weight */ 400, /* width */ 5, /* slant */ 1));
}

TEST(PinnedFontFamiliesTest, ABoldFaceIsNotThePinnedDefaultStyle) {
    EXPECT_FALSE(resolvedStyleIsPinnedDefault(/* weight */ 700, /* width */ 5, /* slant */ 0));
}

TEST(PinnedFontFamiliesTest, ANonNormalWidthIsNotThePinnedDefaultStyle) {
    EXPECT_FALSE(resolvedStyleIsPinnedDefault(/* weight */ 400, /* width */ 3, /* slant */ 0));
}

} // namespace
