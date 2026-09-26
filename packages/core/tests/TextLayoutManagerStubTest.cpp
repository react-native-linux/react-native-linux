#include <gtest/gtest.h>
#include <memory>

#include <react/renderer/attributedstring/AttributedStringBox.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/textlayoutmanager/TextLayoutManager.h>
#include <react/utils/ContextContainer.h>

namespace react_native_linux {
namespace {

using facebook::react::AttributedString;
using facebook::react::LayoutConstraints;
using facebook::react::Size;

facebook::react::TextMeasurement measureHello(const LayoutConstraints& constraints) {
    AttributedString attributedString;
    AttributedString::Fragment fragment;
    fragment.string = "Hello, a paragraph with real glyphs in it";
    attributedString.appendFragment(std::move(fragment));

    const facebook::react::TextLayoutManager textLayoutManager(
        std::make_shared<const facebook::react::ContextContainer>());

    return textLayoutManager.measure(facebook::react::AttributedStringBox(attributedString),
                                     facebook::react::ParagraphAttributes{}, {}, constraints);
}

/**
 * #457: the Hermes-free unit tier links upstream's `platform/cxx` `TextLayoutManager` stub, not the SkParagraph one
 * `TextPipeline.cpp` swaps in where Skia is, so every paragraph here measures to its layout constraints' minimum —
 * 0x0 unconstrained — whatever its text. A unit test can therefore never prove a contract that depends on a
 * paragraph's intrinsic size; those belong to the golden and e2e tiers (see *Unit tests and coverage* in
 * docs/cpp-toolchain.md). This canary fails the day the unit tier measures text for real, which is when that
 * paragraph of the docs, and the suites it names, have to be revisited.
 */
TEST(TextLayoutManagerStubTest, AParagraphMeasuresToItsMinimumConstraintWhateverItsText) {
    EXPECT_EQ(measureHello({.minimumSize = Size{}, .maximumSize = Size{.width = 800, .height = 600}}).size, Size{});
    EXPECT_EQ(
        measureHello({.minimumSize = Size{.width = 12, .height = 34}, .maximumSize = Size{.width = 800, .height = 600}})
            .size,
        (Size{.width = 12, .height = 34}));
}

} // namespace
} // namespace react_native_linux
