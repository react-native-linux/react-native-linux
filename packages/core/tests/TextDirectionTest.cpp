#include "TextDirection.h"

#include <array>
#include <gtest/gtest.h>
#include <optional>

namespace {

using facebook::react::LayoutDirection;
using facebook::react::TextAttributes;
using facebook::react::WritingDirection;

struct DirectionCase {
    std::optional<WritingDirection> writing;
    std::optional<LayoutDirection> layout;
    bool isRightToLeft;
};

/** #72: an explicit writingDirection wins; natural or unset follows the inherited layout direction. */
TEST(TextDirectionTest, AnExplicitWritingDirectionWinsAndANaturalOneFollowsTheLayout) {
    constexpr std::array kCases{
        DirectionCase{.writing = std::nullopt, .layout = std::nullopt, .isRightToLeft = false},
        DirectionCase{.writing = std::nullopt, .layout = LayoutDirection::RightToLeft, .isRightToLeft = true},
        DirectionCase{
            .writing = WritingDirection::Natural, .layout = LayoutDirection::RightToLeft, .isRightToLeft = true},
        DirectionCase{
            .writing = WritingDirection::Natural, .layout = LayoutDirection::LeftToRight, .isRightToLeft = false},
        DirectionCase{
            .writing = WritingDirection::RightToLeft, .layout = LayoutDirection::LeftToRight, .isRightToLeft = true},
        DirectionCase{
            .writing = WritingDirection::LeftToRight, .layout = LayoutDirection::RightToLeft, .isRightToLeft = false},
    };

    for (const DirectionCase& testCase : kCases) {
        TextAttributes attributes;

        attributes.baseWritingDirection = testCase.writing;
        attributes.layoutDirection = testCase.layout;

        EXPECT_EQ(react_native_linux::isRightToLeft(attributes), testCase.isRightToLeft);
    }
}

} // namespace
