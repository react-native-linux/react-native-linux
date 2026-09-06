#include "Appearance.h"

#include <gtest/gtest.h>
#include <optional>

namespace {

using react_native_linux::AppearanceModel;
using react_native_linux::ColorScheme;
using react_native_linux::resolveEffectiveColorScheme;
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

} // namespace
