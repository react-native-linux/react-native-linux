#include "SurfacePresentationPolicy.h"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <string_view>

namespace {

using react_native_linux::compositeAlphaFlagBitFor;
using react_native_linux::describeSurfaceAlphaChoice;
using react_native_linux::describeSurfaceFormatChoice;
using react_native_linux::kCompositeAlphaInheritBit;
using react_native_linux::kCompositeAlphaOpaqueBit;
using react_native_linux::kCompositeAlphaPostMultipliedBit;
using react_native_linux::kCompositeAlphaPreMultipliedBit;
using react_native_linux::selectSurfaceAlpha;
using react_native_linux::selectSurfaceFormat;
using react_native_linux::SurfaceAlphaChoice;
using react_native_linux::SurfaceFormatAvailability;
using react_native_linux::SurfaceFormatChoice;

struct AlphaRow {
    std::string_view name;
    uint32_t supportedCompositeAlphaFlags;
    SurfaceAlphaChoice choice;
};

// Every reachable `supportedCompositeAlpha` shape, including combinations and the INHERIT-only surface.
constexpr std::array<AlphaRow, 8> kAlphaTable{{
    {"only opaque", kCompositeAlphaOpaqueBit, SurfaceAlphaChoice::Opaque},
    {"only pre-multiplied", kCompositeAlphaPreMultipliedBit, SurfaceAlphaChoice::PreMultiplied},
    {"only post-multiplied", kCompositeAlphaPostMultipliedBit, SurfaceAlphaChoice::PostMultiplied},
    {"only inherit", kCompositeAlphaInheritBit, SurfaceAlphaChoice::InheritFromWindowSystem},
    {"pre-multiplied outranks opaque", kCompositeAlphaPreMultipliedBit | kCompositeAlphaOpaqueBit,
     SurfaceAlphaChoice::PreMultiplied},
    {"opaque outranks post-multiplied", kCompositeAlphaOpaqueBit | kCompositeAlphaPostMultipliedBit,
     SurfaceAlphaChoice::Opaque},
    {"post-multiplied outranks inherit", kCompositeAlphaPostMultipliedBit | kCompositeAlphaInheritBit,
     SurfaceAlphaChoice::PostMultiplied},
    {"everything set picks pre-multiplied",
     kCompositeAlphaPreMultipliedBit | kCompositeAlphaOpaqueBit | kCompositeAlphaPostMultipliedBit |
         kCompositeAlphaInheritBit,
     SurfaceAlphaChoice::PreMultiplied},
}};

class SurfaceAlphaPolicyTest : public testing::TestWithParam<AlphaRow> {};

TEST_P(SurfaceAlphaPolicyTest, SelectsThePrecedentChoice) {
    EXPECT_EQ(selectSurfaceAlpha(GetParam().supportedCompositeAlphaFlags), GetParam().choice) << GetParam().name;
}

INSTANTIATE_TEST_SUITE_P(GateTable, SurfaceAlphaPolicyTest, testing::ValuesIn(kAlphaTable));

TEST(SurfacePresentationPolicyTest, CompositeAlphaFlagBitRoundTripsEveryChoice) {
    EXPECT_EQ(compositeAlphaFlagBitFor(SurfaceAlphaChoice::PreMultiplied), kCompositeAlphaPreMultipliedBit);
    EXPECT_EQ(compositeAlphaFlagBitFor(SurfaceAlphaChoice::Opaque), kCompositeAlphaOpaqueBit);
    EXPECT_EQ(compositeAlphaFlagBitFor(SurfaceAlphaChoice::PostMultiplied), kCompositeAlphaPostMultipliedBit);
    EXPECT_EQ(compositeAlphaFlagBitFor(SurfaceAlphaChoice::InheritFromWindowSystem), kCompositeAlphaInheritBit);
}

TEST(SurfacePresentationPolicyTest, DescribesEveryAlphaChoiceDistinctly) {
    EXPECT_EQ(describeSurfaceAlphaChoice(SurfaceAlphaChoice::PreMultiplied), "pre-multiplied");
    EXPECT_EQ(describeSurfaceAlphaChoice(SurfaceAlphaChoice::Opaque), "opaque");
    EXPECT_EQ(describeSurfaceAlphaChoice(SurfaceAlphaChoice::PostMultiplied), "post-multiplied");
    EXPECT_EQ(describeSurfaceAlphaChoice(SurfaceAlphaChoice::InheritFromWindowSystem), "inherit-from-window-system");
}

struct FormatRow {
    std::string_view name;
    SurfaceFormatAvailability availability;
    SurfaceFormatChoice choice;
};

constexpr std::array<FormatRow, 4> kFormatTable{{
    {"neither available is the missing-preferred-format case",
     {.isPreferredBgra8Available = false, .isFallbackRgba8Available = false},
     SurfaceFormatChoice::NoUsableFormat},
    {"only the fallback is available",
     {.isPreferredBgra8Available = false, .isFallbackRgba8Available = true},
     SurfaceFormatChoice::FallbackRgba8},
    {"only the preferred format is available",
     {.isPreferredBgra8Available = true, .isFallbackRgba8Available = false},
     SurfaceFormatChoice::PreferredBgra8},
    {"the preferred format outranks the fallback",
     {.isPreferredBgra8Available = true, .isFallbackRgba8Available = true},
     SurfaceFormatChoice::PreferredBgra8},
}};

class SurfaceFormatPolicyTest : public testing::TestWithParam<FormatRow> {};

TEST_P(SurfaceFormatPolicyTest, SelectsThePrecedentFormat) {
    EXPECT_EQ(selectSurfaceFormat(GetParam().availability), GetParam().choice) << GetParam().name;
}

INSTANTIATE_TEST_SUITE_P(GateTable, SurfaceFormatPolicyTest, testing::ValuesIn(kFormatTable));

TEST(SurfacePresentationPolicyTest, DescribesEveryFormatChoiceDistinctly) {
    EXPECT_EQ(describeSurfaceFormatChoice(SurfaceFormatChoice::PreferredBgra8), "bgra8");
    EXPECT_EQ(describeSurfaceFormatChoice(SurfaceFormatChoice::FallbackRgba8), "rgba8");
    EXPECT_EQ(describeSurfaceFormatChoice(SurfaceFormatChoice::NoUsableFormat), "no-usable-format");
}

} // namespace
