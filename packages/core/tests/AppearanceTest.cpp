#include "Appearance.h"
#include "PlatformColor.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <latch>
#include <optional>
#include <string_view>
#include <thread>

namespace {

using react_native_linux::AppearanceModel;
using react_native_linux::colorSchemeFromName;
using react_native_linux::colorSchemeFromPortalSetting;
using react_native_linux::ColorScheme;
using react_native_linux::kFallbackColorScheme;
using react_native_linux::nameOfColorScheme;
using react_native_linux::platformColor;
using react_native_linux::resolveEffectiveColorScheme;
using react_native_linux::resolvePortalSettingOrFallback;
using react_native_linux::shouldEmitOnOverrideChange;
using react_native_linux::shouldEmitOnPortalChange;

constexpr ColorScheme kLight = ColorScheme::Light;
constexpr ColorScheme kDark = ColorScheme::Dark;

// resolveEffectiveColorScheme: override wins whenever it is set, portal value otherwise.

TEST(AppearanceResolutionTest, NoOverrideResolvesToThePortalValue) {
    EXPECT_EQ(resolveEffectiveColorScheme(std::nullopt, kLight), kLight);
    EXPECT_EQ(resolveEffectiveColorScheme(std::nullopt, kDark), kDark);
}

TEST(AppearanceResolutionTest, AnOverrideWinsRegardlessOfThePortalValue) {
    EXPECT_EQ(resolveEffectiveColorScheme(kDark, kLight), kDark);
    EXPECT_EQ(resolveEffectiveColorScheme(kLight, kDark), kLight);
    EXPECT_EQ(resolveEffectiveColorScheme(kDark, kDark), kDark);
}

// shouldEmitOnOverrideChange: fires exactly once per actual override transition — set, switch, or clear — and
// not on a call that restates the current override.

struct OverrideChangeCase {
    std::optional<ColorScheme> previousOverride;
    std::optional<ColorScheme> nextOverride;
    bool expectedEmit;
};

class OverrideChangeTest : public ::testing::TestWithParam<OverrideChangeCase> {};

TEST_P(OverrideChangeTest, MatchesTheTable) {
    const OverrideChangeCase testCase = GetParam();

    EXPECT_EQ(shouldEmitOnOverrideChange(testCase.previousOverride, testCase.nextOverride), testCase.expectedEmit);
}

INSTANTIATE_TEST_SUITE_P(
    Table, OverrideChangeTest,
    ::testing::Values(
        // Setting an override for the first time emits.
        OverrideChangeCase{std::nullopt, kDark, true}, OverrideChangeCase{std::nullopt, kLight, true},
        // Clearing an override emits, "returning to the portal value".
        OverrideChangeCase{kDark, std::nullopt, true}, OverrideChangeCase{kLight, std::nullopt, true},
        // Switching between the two override values emits.
        OverrideChangeCase{kDark, kLight, true}, OverrideChangeCase{kLight, kDark, true},
        // Restating the same override, or clearing an override that was never set, is a no-op.
        OverrideChangeCase{kDark, kDark, false}, OverrideChangeCase{kLight, kLight, false},
        OverrideChangeCase{std::nullopt, std::nullopt, false}));

// shouldEmitOnPortalChange: an override in place swallows the signal; without one, only an actual value change
// emits.

struct PortalChangeCase {
    std::optional<ColorScheme> currentOverride;
    ColorScheme previousPortalColorScheme;
    ColorScheme nextPortalColorScheme;
    bool expectedEmit;
};

class PortalChangeTest : public ::testing::TestWithParam<PortalChangeCase> {};

TEST_P(PortalChangeTest, MatchesTheTable) {
    const PortalChangeCase testCase = GetParam();

    EXPECT_EQ(shouldEmitOnPortalChange(testCase.currentOverride, testCase.previousPortalColorScheme,
                                        testCase.nextPortalColorScheme),
              testCase.expectedEmit);
}

INSTANTIATE_TEST_SUITE_P(
    Table, PortalChangeTest,
    ::testing::Values(
        // No override: a portal value change emits.
        PortalChangeCase{std::nullopt, kLight, kDark, true}, PortalChangeCase{std::nullopt, kDark, kLight, true},
        // No override: the portal repeating its own value does not emit.
        PortalChangeCase{std::nullopt, kLight, kLight, false},
        // An override in place swallows a portal change entirely, whether or not the value actually moved.
        PortalChangeCase{kDark, kLight, kDark, false}, PortalChangeCase{kLight, kLight, kDark, false},
        PortalChangeCase{kDark, kLight, kLight, false}));

// AppearanceModel: the stateful wrapper `setColorScheme`/`getColorScheme`/`appearanceChanged` bind to. The
// portal side is a fake — a direct call to onPortalColorSchemeChanged — because the real
// org.freedesktop.portal.Settings listener is #52 and has nothing to fake yet.

class AppearanceModelTest : public ::testing::Test {
protected:
    AppearanceModel model{kLight};
    int emitCount = 0;
    std::optional<ColorScheme> lastEmitted;

    void SetUp() override {
        model.setChangeListener([this](ColorScheme scheme) {
            ++emitCount;
            lastEmitted = scheme;
        });
    }
};

TEST_F(AppearanceModelTest, GetColorSchemeStartsAtThePortalValue) { EXPECT_EQ(model.colorScheme(), kLight); }

TEST_F(AppearanceModelTest, SetColorSchemeOverridesThePortalValueAndEmitsOnce) {
    model.setColorScheme(kDark);

    EXPECT_EQ(model.colorScheme(), kDark);
    EXPECT_EQ(emitCount, 1);
    EXPECT_EQ(lastEmitted, kDark);
}

TEST_F(AppearanceModelTest, ClearingTheOverrideRestoresThePortalValueAndEmitsOnce) {
    model.setColorScheme(kDark);
    model.setColorScheme(std::nullopt);

    EXPECT_EQ(model.colorScheme(), kLight);
    EXPECT_EQ(emitCount, 2);
    EXPECT_EQ(lastEmitted, kLight);
}

TEST_F(AppearanceModelTest, APortalChangeWhileAnOverrideIsSetDoesNotEmit) {
    model.setColorScheme(kDark);
    emitCount = 0;
    lastEmitted.reset();

    model.onPortalColorSchemeChanged(kDark);

    EXPECT_EQ(model.colorScheme(), kDark);
    EXPECT_EQ(emitCount, 0);
}

TEST_F(AppearanceModelTest, APortalChangeSwallowedByAnOverrideIsReResolvedWhenTheOverrideClears) {
    model.setColorScheme(kDark);
    model.onPortalColorSchemeChanged(kDark);
    emitCount = 0;

    model.setColorScheme(std::nullopt);

    EXPECT_EQ(model.colorScheme(), kDark);
    EXPECT_EQ(emitCount, 1);
    EXPECT_EQ(lastEmitted, kDark);
}

TEST_F(AppearanceModelTest, APortalNoPreferenceSignalAfterDarkReturnsToTheFallback) {
    model.onPortalColorSchemeChanged(kDark);
    emitCount = 0;
    lastEmitted.reset();

    // 0 "no preference" decodes to nothing in `colorSchemeFromPortalSetting`, but that is not a message the bus
    // failed to parse: `onSettingChanged` must resolve it to the fallback, not discard it and leave the model
    // stuck on the last real signal.
    model.onPortalColorSchemeChanged(resolvePortalSettingOrFallback(0));

    EXPECT_EQ(model.colorScheme(), kFallbackColorScheme);
    EXPECT_EQ(emitCount, 1);
    EXPECT_EQ(lastEmitted, kFallbackColorScheme);
}

TEST_F(AppearanceModelTest, APortalChangeWithNoOverrideEmits) {
    model.onPortalColorSchemeChanged(kDark);

    EXPECT_EQ(model.colorScheme(), kDark);
    EXPECT_EQ(emitCount, 1);
    EXPECT_EQ(lastEmitted, kDark);
}

TEST_F(AppearanceModelTest, RestatingTheSameOverrideDoesNotEmitAgain) {
    model.setColorScheme(kDark);
    emitCount = 0;

    model.setColorScheme(kDark);

    EXPECT_EQ(emitCount, 0);
}

TEST_F(AppearanceModelTest, WithNoChangeListenerSetColorSchemeStillUpdatesTheState) {
    AppearanceModel unlistened{kLight};

    unlistened.setColorScheme(kDark);

    EXPECT_EQ(unlistened.colorScheme(), kDark);
}

TEST_F(AppearanceModelTest, WithNoChangeListenerAPortalChangeStillUpdatesTheState) {
    AppearanceModel unlistened{kLight};

    unlistened.onPortalColorSchemeChanged(kDark);

    EXPECT_EQ(unlistened.colorScheme(), kDark);
}

// colorSchemeFromPortalSetting: the three values org.freedesktop.portal.Settings defines for
// `org.freedesktop.appearance color-scheme`, and the reserved rest, which mean the same as "no preference".

struct PortalSettingCase {
    uint32_t portalSettingValue;
    std::optional<ColorScheme> expectedColorScheme;
};

class PortalSettingTest : public ::testing::TestWithParam<PortalSettingCase> {};

TEST_P(PortalSettingTest, DecodesTheSettingValue) {
    EXPECT_EQ(colorSchemeFromPortalSetting(GetParam().portalSettingValue), GetParam().expectedColorScheme);
}

INSTANTIATE_TEST_SUITE_P(
    AppearancePortalDecode, PortalSettingTest,
    ::testing::Values(PortalSettingCase{.portalSettingValue = 0, .expectedColorScheme = std::nullopt},
                      PortalSettingCase{.portalSettingValue = 1, .expectedColorScheme = kDark},
                      PortalSettingCase{.portalSettingValue = 2, .expectedColorScheme = kLight},
                      PortalSettingCase{.portalSettingValue = 3, .expectedColorScheme = std::nullopt},
                      PortalSettingCase{.portalSettingValue = 4294967295U, .expectedColorScheme = std::nullopt}));

// resolvePortalSettingOrFallback: the same table, with the fallback applied for "no preference" and reserved
// values instead of leaving them undecoded.

struct PortalSettingFallbackCase {
    uint32_t portalSettingValue;
    ColorScheme expectedColorScheme;
};

class PortalSettingFallbackTest : public ::testing::TestWithParam<PortalSettingFallbackCase> {};

TEST_P(PortalSettingFallbackTest, ResolvesTheSettingValueOrTheFallback) {
    EXPECT_EQ(resolvePortalSettingOrFallback(GetParam().portalSettingValue), GetParam().expectedColorScheme);
}

INSTANTIATE_TEST_SUITE_P(
    AppearancePortalDecodeOrFallback, PortalSettingFallbackTest,
    ::testing::Values(PortalSettingFallbackCase{.portalSettingValue = 0, .expectedColorScheme = kFallbackColorScheme},
                      PortalSettingFallbackCase{.portalSettingValue = 1, .expectedColorScheme = kDark},
                      PortalSettingFallbackCase{.portalSettingValue = 2, .expectedColorScheme = kLight},
                      PortalSettingFallbackCase{.portalSettingValue = 3, .expectedColorScheme = kFallbackColorScheme},
                      PortalSettingFallbackCase{.portalSettingValue = 4294967295U,
                                                .expectedColorScheme = kFallbackColorScheme}));

// colorSchemeFromName: `ColorSchemeName` and `ColorSchemeOverride` from NativeAppearance.js. `auto` and
// `unspecified` are the two spellings of "clear the override", and an unrecognised string clears it too rather
// than throwing, because `setColorScheme` has no failure channel in the spec.

struct ColorSchemeNameCase {
    std::string_view colorSchemeName;
    std::optional<ColorScheme> expectedColorScheme;
};

class ColorSchemeNameTest : public ::testing::TestWithParam<ColorSchemeNameCase> {};

TEST_P(ColorSchemeNameTest, DecodesTheName) {
    EXPECT_EQ(colorSchemeFromName(GetParam().colorSchemeName), GetParam().expectedColorScheme);
}

INSTANTIATE_TEST_SUITE_P(
    AppearanceNameDecode, ColorSchemeNameTest,
    ::testing::Values(ColorSchemeNameCase{.colorSchemeName = "light", .expectedColorScheme = kLight},
                      ColorSchemeNameCase{.colorSchemeName = "dark", .expectedColorScheme = kDark},
                      ColorSchemeNameCase{.colorSchemeName = "auto", .expectedColorScheme = std::nullopt},
                      ColorSchemeNameCase{.colorSchemeName = "unspecified", .expectedColorScheme = std::nullopt},
                      ColorSchemeNameCase{.colorSchemeName = "", .expectedColorScheme = std::nullopt},
                      ColorSchemeNameCase{.colorSchemeName = "Dark", .expectedColorScheme = std::nullopt}));

TEST(AppearanceNameTest, EveryColorSchemeRoundTripsThroughItsName) {
    EXPECT_EQ(colorSchemeFromName(nameOfColorScheme(kLight)), kLight);
    EXPECT_EQ(colorSchemeFromName(nameOfColorScheme(kDark)), kDark);
}

// platformColor: the whole token set, in both schemes, and the name that is not in it.

struct PlatformColorCase {
    std::string_view platformColorName;
    int32_t expectedLightArgb;
    int32_t expectedDarkArgb;
};

class PlatformColorTest : public ::testing::TestWithParam<PlatformColorCase> {};

TEST_P(PlatformColorTest, ResolvesTheNameInBothSchemes) {
    EXPECT_EQ(platformColor(GetParam().platformColorName, kLight), GetParam().expectedLightArgb);
    EXPECT_EQ(platformColor(GetParam().platformColorName, kDark), GetParam().expectedDarkArgb);
}

INSTANTIATE_TEST_SUITE_P(AppearancePlatformColors, PlatformColorTest,
                         ::testing::Values(PlatformColorCase{.platformColorName = "labelColor",
                                                             .expectedLightArgb = static_cast<int32_t>(0xFF1B1F23),
                                                             .expectedDarkArgb = static_cast<int32_t>(0xFFE6EDF3)},
                                           PlatformColorCase{.platformColorName = "secondaryLabelColor",
                                                             .expectedLightArgb = static_cast<int32_t>(0xFF5C6370),
                                                             .expectedDarkArgb = static_cast<int32_t>(0xFF8B949E)},
                                           PlatformColorCase{.platformColorName = "windowBackgroundColor",
                                                             .expectedLightArgb = static_cast<int32_t>(0xFFF5F6F7),
                                                             .expectedDarkArgb = static_cast<int32_t>(0xFF0D1117)},
                                           PlatformColorCase{.platformColorName = "controlBackgroundColor",
                                                             .expectedLightArgb = static_cast<int32_t>(0xFFFFFFFF),
                                                             .expectedDarkArgb = static_cast<int32_t>(0xFF161B22)},
                                           PlatformColorCase{.platformColorName = "separatorColor",
                                                             .expectedLightArgb = static_cast<int32_t>(0xFFD0D7DE),
                                                             .expectedDarkArgb = static_cast<int32_t>(0xFF30363D)},
                                           PlatformColorCase{.platformColorName = "linkColor",
                                                             .expectedLightArgb = static_cast<int32_t>(0xFF0969DA),
                                                             .expectedDarkArgb = static_cast<int32_t>(0xFF58A6FF)}));

TEST(PlatformColorUnknownNameTest, AnUnrecognisedNameResolvesToNothingInEitherScheme) {
    EXPECT_EQ(platformColor("systemPinkColor", kLight), std::nullopt);
    EXPECT_EQ(platformColor("systemPinkColor", kDark), std::nullopt);
}

TEST(PlatformColorUnknownNameTest, AnEmptyNameResolvesToNothing) {
    EXPECT_EQ(platformColor("", kLight), std::nullopt);
}

// The TSan half of the mutex fix: `AppearanceModel` has a frame-thread writer (the portal, faked here by a
// second thread calling `onPortalColorSchemeChanged`) and a JavaScript-thread reader-and-writer (`getColorScheme`
// and `setColorScheme`, faked by the main thread resolving a `PlatformColor` and toggling the override). Both
// sides take `mutex_`, which is the whole guarantee; ThreadSanitizer is what checks the claim, not the
// assertions below, which only say the run finished and landed on a defined scheme. Run under CTest and under
// the TSan preset.
TEST(AppearanceModelThreadSafetyTest, PortalSignalsAndJavaScriptAccessAreSerialized) {
    constexpr int kConcurrentIterations = 2000;

    AppearanceModel model{kLight};
    std::latch startLatch{2};

    std::thread portalThread([&] {
        startLatch.arrive_and_wait();

        for (int iteration = 0; iteration < kConcurrentIterations; ++iteration) {
            model.onPortalColorSchemeChanged(iteration % 2 == 0 ? kDark : kLight);
        }
    });

    startLatch.arrive_and_wait();

    for (int iteration = 0; iteration < kConcurrentIterations; ++iteration) {
        const ColorScheme scheme = model.colorScheme();
        const std::optional<int32_t> resolved = platformColor("labelColor", scheme);

        EXPECT_TRUE(resolved.has_value());

        model.setColorScheme(iteration % 2 == 0 ? std::optional<ColorScheme>(kDark) : std::nullopt);
    }

    portalThread.join();

    const ColorScheme finalScheme = model.colorScheme();

    EXPECT_TRUE(finalScheme == kLight || finalScheme == kDark);
}

} // namespace
