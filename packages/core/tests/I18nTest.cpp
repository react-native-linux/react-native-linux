#include "I18n.h"

#include "AsyncStorage.h"
#include "ScopedEnvironment.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using facebook::react::LayoutDirection;
using react_native_linux::I18nModel;
using react_native_linux::KeyValueStore;
using react_native_linux::LayoutDirectionRequest;

constexpr LayoutDirectionRequest kLeftToRight{.layoutDirection = LayoutDirection::LeftToRight,
                                              .swapLeftAndRightInRightToLeft = false};
constexpr LayoutDirectionRequest kRightToLeftSwapped{.layoutDirection = LayoutDirection::RightToLeft,
                                                     .swapLeftAndRightInRightToLeft = true};
constexpr LayoutDirectionRequest kRightToLeftUnswapped{.layoutDirection = LayoutDirection::RightToLeft,
                                                       .swapLeftAndRightInRightToLeft = false};

TEST(I18nTest, TheLanguageSubtagDecidesWhetherALocaleIsRightToLeft) {
    constexpr std::array kCases{
        std::pair<std::string_view, bool>{"ar", true},       std::pair<std::string_view, bool>{"ar_EG", true},
        std::pair<std::string_view, bool>{"he_IL", true},    std::pair<std::string_view, bool>{"iw", true},
        std::pair<std::string_view, bool>{"fa-IR", true},    std::pair<std::string_view, bool>{"ckb_IQ", true},
        std::pair<std::string_view, bool>{"ur@latin", true}, std::pair<std::string_view, bool>{"yi.UTF-8", true},
        std::pair<std::string_view, bool>{"en_US", false},   std::pair<std::string_view, bool>{"ku_TR", false},
        std::pair<std::string_view, bool>{"arn_CL", false},  std::pair<std::string_view, bool>{"C", false},
        std::pair<std::string_view, bool>{"", false},
    };

    for (const auto& [locale, isRightToLeft] : kCases) {
        EXPECT_EQ(react_native_linux::isRightToLeftLanguage(locale), isRightToLeft) << locale;
    }
}

/** Every locale variable unset for one test, whatever the process had. */
class I18nEnvironmentTest : public ::testing::Test {
private:
    const react_native_linux::ScopedEnvironment lcAll_{"LC_ALL", nullptr};
    const react_native_linux::ScopedEnvironment lcMessages_{"LC_MESSAGES", nullptr};
    const react_native_linux::ScopedEnvironment lang_{"LANG", nullptr};
    const react_native_linux::ScopedEnvironment language_{"LANGUAGE", nullptr};
};

TEST_F(I18nEnvironmentTest, TheFirstSetLocaleVariableWinsWithoutItsCodesetOrModifier) {
    EXPECT_EQ(react_native_linux::localeFromEnvironment(), "");

    setenv("LANG", "he_IL.UTF-8", 1);
    EXPECT_EQ(react_native_linux::localeFromEnvironment(), "he_IL");

    setenv("LC_MESSAGES", "ar_EG@latin", 1);
    EXPECT_EQ(react_native_linux::localeFromEnvironment(), "ar_EG");

    setenv("LC_ALL", "", 1);
    EXPECT_EQ(react_native_linux::localeFromEnvironment(), "ar_EG");

    setenv("LC_ALL", "en_US.UTF-8", 1);
    EXPECT_EQ(react_native_linux::localeFromEnvironment(), "en_US");
}

/**
 * #647: `gt-react-native`'s `getNativeLocales`, the languages GNU gettext would show messages in, best first. The
 * `LANGUAGE` list leads and the locale follows, all as BCP 47 tags, dropping anything not shaped like one. A `C` or
 * `POSIX` entry in `LANGUAGE` ends the list there, untranslated, without the locale; a `C` or `POSIX` locale switches
 * the list off.
 */
TEST_F(I18nEnvironmentTest, PreferredLanguagesFollowGettext) {
    using react_native_linux::preferredLanguageTags;
    using Tags = std::vector<std::string>;

    EXPECT_EQ(preferredLanguageTags(), Tags{});

    setenv("LANGUAGE", "de", 1);
    setenv("LANG", "C.UTF-8", 1);
    EXPECT_EQ(preferredLanguageTags(), Tags{});

    setenv("LANG", "POSIX", 1);
    EXPECT_EQ(preferredLanguageTags(), Tags{});

    setenv("LANG", "de_DE.UTF-8", 1);
    EXPECT_EQ(preferredLanguageTags(), (Tags{"de", "de-DE"}));

    setenv("LANGUAGE", "pt_BR:pt::fr_FR.UTF-8@euro:de_DE", 1);
    EXPECT_EQ(preferredLanguageTags(), (Tags{"pt-BR", "pt", "fr-FR", "de-DE"}));

    setenv("LANGUAGE", "C:fr", 1);
    EXPECT_EQ(preferredLanguageTags(), Tags{});

    setenv("LANGUAGE", "es:C.UTF-8:fr", 1);
    EXPECT_EQ(preferredLanguageTags(), Tags{"es"});

    setenv("LANGUAGE", "it:POSIX:fr", 1);
    EXPECT_EQ(preferredLanguageTags(), Tags{"it"});

    setenv("LANGUAGE", "en__US:en_US_abcdefghi:x+y:en_US", 1);
    EXPECT_EQ(preferredLanguageTags(), (Tags{"en-US", "de-DE"}));

    unsetenv("LANGUAGE");
    setenv("LC_MESSAGES", "sr_RS@latin", 1);
    EXPECT_EQ(preferredLanguageTags(), Tags{"sr-RS"});
}

class I18nModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = std::filesystem::temp_directory_path() /
                ("rnl-i18n-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        std::filesystem::remove_all(root_);
    }

    void TearDown() override { std::filesystem::remove_all(root_); }

    std::shared_ptr<KeyValueStore> openStore() const {
        std::shared_ptr<KeyValueStore> store = std::make_shared<KeyValueStore>();

        store->setDatabasePath((root_ / "app" / "async-storage.sqlite").string());

        return store;
    }

    std::filesystem::path root_;
};

TEST_F(I18nModelTest, ALeftToRightLocaleStartsLeftToRightAndReportsItOnce) {
    I18nModel model("en_US", openStore());

    EXPECT_EQ(model.localeIdentifier(), "en_US");
    EXPECT_FALSE(model.isRightToLeft());
    EXPECT_TRUE(model.doesSwapLeftAndRightInRightToLeft());
    EXPECT_EQ(model.takeLayoutDirectionChange(), kLeftToRight);
    EXPECT_EQ(model.takeLayoutDirectionChange(), std::nullopt);
}

TEST_F(I18nModelTest, ForceRightToLeftFlipsTheLayoutAndSwapsLeftAndRightUntilToldNotTo) {
    I18nModel model("en_US", openStore());

    static_cast<void>(model.takeLayoutDirectionChange());
    model.forceRightToLeft(true);

    EXPECT_TRUE(model.isRightToLeft());
    EXPECT_EQ(model.takeLayoutDirectionChange(), kRightToLeftSwapped);

    model.swapLeftAndRightInRightToLeft(false);

    EXPECT_EQ(model.takeLayoutDirectionChange(), kRightToLeftUnswapped);
}

TEST_F(I18nModelTest, ARightToLeftLocaleIsRightToLeftOnlyWhileAllowed) {
    I18nModel model("ar_EG", openStore());

    EXPECT_EQ(model.takeLayoutDirectionChange(), kRightToLeftSwapped);

    model.allowRightToLeft(false);

    EXPECT_FALSE(model.isRightToLeft());
    EXPECT_EQ(model.takeLayoutDirectionChange(), kLeftToRight);
}

/** The react-native-windows#7070 half of #72: the choices outlive the process that made them. */
TEST_F(I18nModelTest, TheChoicesSurviveARestartThroughTheStore) {
    {
        I18nModel model("ar_EG", openStore());

        model.allowRightToLeft(false);
        model.forceRightToLeft(true);
        model.swapLeftAndRightInRightToLeft(false);
    }

    I18nModel restarted("ar_EG", openStore());

    restarted.restore();

    EXPECT_TRUE(restarted.isRightToLeft());
    EXPECT_FALSE(restarted.doesSwapLeftAndRightInRightToLeft());
    EXPECT_EQ(restarted.takeLayoutDirectionChange(), kRightToLeftUnswapped);
}

TEST_F(I18nModelTest, AnEmptyStoreRestoresTheDefaults) {
    I18nModel model("en_US", openStore());

    model.restore();

    EXPECT_FALSE(model.isRightToLeft());
    EXPECT_TRUE(model.doesSwapLeftAndRightInRightToLeft());
    EXPECT_EQ(model.takeLayoutDirectionChange(), kLeftToRight);
}

} // namespace
