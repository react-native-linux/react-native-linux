#include "ShadowTreeTestSupport.h"

#include <folly/dynamic.h>
#include <gtest/gtest.h>
#include <memory>
#include <react/renderer/components/view/AccessibilityPrimitives.h>
#include <react/renderer/components/view/AccessibilityProps.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>
#include <react/renderer/components/view/ViewShadowNode.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/renderer/core/LayoutableShadowNode.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/mounting/ShadowTree.h>
#include <utility>

namespace {

using react_native_linux::makeConfiguredShadowNode;
using react_native_linux::PassThroughShadowTreeDelegate;

constexpr SurfaceId kSurfaceId = 1;

using ChildList = std::vector<std::shared_ptr<const ShadowNode>>;

/**
 * Every web-prop alias the issue lists resolves in one of two places: `role`/`accessibilityRole` in
 * `AccessibilityProps`' constructor, and the CSS logical-property style aliases (`inset*`, `start`/`end`,
 * `marginInline*`/`marginBlock*`, `paddingInline*`/`paddingBlock*`, `gap`) as plain Yoga edge and gutter setters
 * that Yoga's own `Style::computeLeftEdge`/`computeTopEdge`/`computeRightEdge`/`computeBottomEdge` and
 * `computeRowGap`/`computeColumnGap` resolve against their physical counterpart at layout time. Both are proved
 * by committing a real tree through the `ShadowTree` commit path and reading back what a mount would see: the
 * parsed prop for the accessibility pair, the laid-out frame for the style pair — the same layer
 * `LayoutConformanceTest` reads. `aria-*`, `tabIndex` and `id`→`nativeID` are resolved by RN's own
 * `View.js`/`processAriaProps`, upstream JS this repository does not vendor or override (there is no
 * `Libraries/` tree under `third_party/react-native`); a `RawProps` object built directly, as every fixture here
 * does, is already past that stage, so there is nothing for a C++ shadow-node test to exercise, and no fork of it
 * exists to regress.
 */
class WebPropAliasTest : public ::testing::Test {
protected:
    facebook::react::AccessibilityTraits accessibilityTraitsFor(folly::dynamic props) {
        const std::shared_ptr<const ShadowNode> node =
            makeConfiguredShadowNode(viewDescriptor_, 1, kSurfaceId, contextContainer_, std::move(props),
                                     std::make_shared<const ChildList>());

        return std::static_pointer_cast<const ViewShadowNode>(node)->getConcreteProps().accessibilityTraits;
    }

    Rect frameOf(Tag tag, const std::map<Tag, Rect>& frames) {
        const auto entry = frames.find(tag);

        if (entry == frames.end()) {
            ADD_FAILURE() << "tag " << tag << " was not committed";
            return Rect{};
        }

        return entry->second;
    }

    const std::map<Tag, Rect>& commit(folly::dynamic containerProps, std::vector<folly::dynamic> childrenProps) {
        ChildList children;
        Tag tag = 11;

        for (folly::dynamic& childProps : childrenProps) {
            children.push_back(makeConfiguredShadowNode(viewDescriptor_, tag++, kSurfaceId, contextContainer_,
                                                        std::move(childProps), std::make_shared<const ChildList>()));
        }

        const std::shared_ptr<const ShadowNode> containerNode =
            makeConfiguredShadowNode(viewDescriptor_, 10, kSurfaceId, contextContainer_, std::move(containerProps),
                                     std::make_shared<const ChildList>(std::move(children)));

        shadowTree_ = std::make_unique<ShadowTree>(kSurfaceId, LayoutConstraints{}, LayoutContext{},
                                                   shadowTreeDelegate_, *contextContainer_);

        const ShadowTreeCommitOptions commitOptions{.enableStateReconciliation = false, .mountSynchronously = true};

        shadowTree_->commit(
            [&](const RootShadowNode& oldRootShadowNode) {
                return react_native_linux::cloneRootWithChildren(
                    oldRootShadowNode, std::make_shared<const ChildList>(ChildList{containerNode}));
            },
            commitOptions);

        frames_.clear();
        react_native_linux::collectAbsoluteFrames(shadowTree_->getCurrentRevision().rootShadowNode, Point{}, frames_,
                                                  [](const std::shared_ptr<const ShadowNode>&) {});

        return frames_;
    }

    folly::dynamic box(folly::dynamic extra) {
        folly::dynamic props = folly::dynamic::object("width", 50)("height", 50);

        for (const auto& entry : extra.items()) {
            props[entry.first] = entry.second;
        }

        return props;
    }

    PassThroughShadowTreeDelegate shadowTreeDelegate_;
    std::shared_ptr<const ContextContainer> contextContainer_{std::make_shared<ContextContainer>()};
    ViewComponentDescriptor viewDescriptor_ = makeViewComponentDescriptor(contextContainer_);
    std::unique_ptr<ShadowTree> shadowTree_;
    std::map<Tag, Rect> frames_;
};

#pragma mark - role / accessibilityRole

TEST_F(WebPropAliasTest, RoleAloneAndAccessibilityRoleAloneResolveToTheSameAccessibilityTraits) {
    const facebook::react::AccessibilityTraits fromRole =
        accessibilityTraitsFor(folly::dynamic::object("role", "button"));
    const facebook::react::AccessibilityTraits fromAccessibilityRole =
        accessibilityTraitsFor(folly::dynamic::object("accessibilityRole", "button"));

    EXPECT_EQ(fromRole, facebook::react::AccessibilityTraits::Button);
    EXPECT_EQ(fromRole, fromAccessibilityRole);
}

TEST_F(WebPropAliasTest, WhenBothAreGivenRoleTakesPrecedenceOverAccessibilityRole) {
    const facebook::react::AccessibilityTraits traits =
        accessibilityTraitsFor(folly::dynamic::object("role", "button")("accessibilityRole", "link"));

    EXPECT_EQ(traits, facebook::react::AccessibilityTraits::Button);
}

#pragma mark - inset family (position: absolute)

folly::dynamic absoluteBox(folly::dynamic extra) {
    folly::dynamic props = folly::dynamic::object("position", "absolute")("width", 50)("height", 50);

    for (const auto& entry : extra.items()) {
        props[entry.first] = entry.second;
    }

    return props;
}

TEST_F(WebPropAliasTest, InsetIsTheSameAsSettingAllFourPhysicalEdges) {
    const Rect withAlias =
        frameOf(11, commit(folly::dynamic::object("width", 300)("height", 300),
                           {absoluteBox(folly::dynamic::object("inset", 20))}));
    const Rect withCanonical = frameOf(
        11, commit(folly::dynamic::object("width", 300)("height", 300),
                   {absoluteBox(folly::dynamic::object("top", 20)("left", 20)("right", 20)("bottom", 20))}));

    EXPECT_EQ(withAlias.origin.x, withCanonical.origin.x);
    EXPECT_EQ(withAlias.origin.y, withCanonical.origin.y);
    EXPECT_EQ(withAlias.origin.x, 20);
    EXPECT_EQ(withAlias.origin.y, 20);
}

TEST_F(WebPropAliasTest, InsetInlineIsTheSameAsSettingLeftAndRight) {
    const Rect withAlias = frameOf(
        11, commit(folly::dynamic::object("width", 300)("height", 300), {absoluteBox(folly::dynamic::object(
                                                                             "insetInline", 15))}));
    const Rect withCanonical = frameOf(
        11, commit(folly::dynamic::object("width", 300)("height", 300),
                   {absoluteBox(folly::dynamic::object("left", 15)("right", 15))}));

    EXPECT_EQ(withAlias.origin.x, withCanonical.origin.x);
    EXPECT_EQ(withAlias.origin.x, 15);
}

TEST_F(WebPropAliasTest, InsetBlockIsTheSameAsSettingTopAndBottom) {
    const Rect withAlias = frameOf(
        11, commit(folly::dynamic::object("width", 300)("height", 300), {absoluteBox(folly::dynamic::object(
                                                                             "insetBlock", 15))}));
    const Rect withCanonical = frameOf(
        11, commit(folly::dynamic::object("width", 300)("height", 300),
                   {absoluteBox(folly::dynamic::object("top", 15)("bottom", 15))}));

    EXPECT_EQ(withAlias.origin.y, withCanonical.origin.y);
    EXPECT_EQ(withAlias.origin.y, 15);
}

TEST_F(WebPropAliasTest, StartIsTheSameAsLeftInLeftToRightDirection) {
    const Rect withAlias = frameOf(
        11, commit(folly::dynamic::object("width", 300)("height", 300), {absoluteBox(folly::dynamic::object(
                                                                             "start", 25))}));
    const Rect withCanonical = frameOf(
        11, commit(folly::dynamic::object("width", 300)("height", 300), {absoluteBox(folly::dynamic::object(
                                                                             "left", 25))}));

    EXPECT_EQ(withAlias.origin.x, withCanonical.origin.x);
    EXPECT_EQ(withAlias.origin.x, 25);
}

TEST_F(WebPropAliasTest, EndIsTheSameAsRightInLeftToRightDirection) {
    const Rect withAlias = frameOf(
        11, commit(folly::dynamic::object("width", 300)("height", 300), {absoluteBox(folly::dynamic::object(
                                                                             "end", 20))}));
    const Rect withCanonical = frameOf(
        11, commit(folly::dynamic::object("width", 300)("height", 300), {absoluteBox(folly::dynamic::object(
                                                                             "right", 20))}));

    EXPECT_EQ(withAlias.origin.x, withCanonical.origin.x);
    EXPECT_EQ(withAlias.origin.x, 300 - 50 - 20);
}

/**
 * The precedence Yoga's `Style::computeLeftEdge` states: the logical, direction-aware `start` beats the physical
 * `left`, which beats the logical shorthand `insetInline`, which beats the `inset` shorthand for all four edges.
 * A test that only ever sets one of the four never proves this — the point of a precedence test is the case where
 * two disagree.
 */
TEST_F(WebPropAliasTest, StartOutranksLeftWhichOutranksInsetInlineWhichOutranksInset) {
    const Rect leftOverInset = frameOf(
        11, commit(folly::dynamic::object("width", 300)("height", 300),
                   {absoluteBox(folly::dynamic::object("left", 30)("inset", 5))}));
    const Rect insetInlineOverInset = frameOf(
        11, commit(folly::dynamic::object("width", 300)("height", 300),
                   {absoluteBox(folly::dynamic::object("insetInline", 12)("inset", 5))}));
    const Rect startOverLeft = frameOf(
        11, commit(folly::dynamic::object("width", 300)("height", 300),
                   {absoluteBox(folly::dynamic::object("start", 40)("left", 30))}));

    EXPECT_EQ(leftOverInset.origin.x, 30);
    EXPECT_EQ(insetInlineOverInset.origin.x, 12);
    EXPECT_EQ(startOverLeft.origin.x, 40);
}

#pragma mark - gap family

TEST_F(WebPropAliasTest, GapIsTheSameAsColumnGapInARowContainer) {
    const std::map<Tag, Rect> withAlias =
        commit(folly::dynamic::object("width", 300)("height", 100)("flexDirection", "row")("gap", 10),
               {box(folly::dynamic::object()), box(folly::dynamic::object())});
    const std::map<Tag, Rect> withCanonical =
        commit(folly::dynamic::object("width", 300)("height", 100)("flexDirection", "row")("columnGap", 10),
               {box(folly::dynamic::object()), box(folly::dynamic::object())});

    EXPECT_EQ(frameOf(12, withAlias).origin.x, frameOf(12, withCanonical).origin.x);
    EXPECT_EQ(frameOf(12, withAlias).origin.x, 60);
}

TEST_F(WebPropAliasTest, GapIsTheSameAsRowGapInAColumnContainer) {
    const std::map<Tag, Rect> withAlias = commit(folly::dynamic::object("width", 100)("height", 300)("gap", 10),
                                                 {box(folly::dynamic::object()), box(folly::dynamic::object())});
    const std::map<Tag, Rect> withCanonical =
        commit(folly::dynamic::object("width", 100)("height", 300)("rowGap", 10),
               {box(folly::dynamic::object()), box(folly::dynamic::object())});

    EXPECT_EQ(frameOf(12, withAlias).origin.y, frameOf(12, withCanonical).origin.y);
    EXPECT_EQ(frameOf(12, withAlias).origin.y, 60);
}

#pragma mark - margin and padding logical properties

TEST_F(WebPropAliasTest, MarginInlineIsTheSameAsMarginLeftAndMarginRight) {
    const Rect withAlias =
        frameOf(11, commit(folly::dynamic::object("width", 300)("height", 100)("flexDirection", "row"),
                           {box(folly::dynamic::object("marginInline", 10))}));
    const Rect withCanonical =
        frameOf(11, commit(folly::dynamic::object("width", 300)("height", 100)("flexDirection", "row"),
                           {box(folly::dynamic::object("marginLeft", 10)("marginRight", 10))}));

    EXPECT_EQ(withAlias.origin.x, withCanonical.origin.x);
    EXPECT_EQ(withAlias.origin.x, 10);
}

TEST_F(WebPropAliasTest, MarginBlockIsTheSameAsMarginTopAndMarginBottom) {
    const Rect withAlias = frameOf(11, commit(folly::dynamic::object("width", 100)("height", 300),
                                              {box(folly::dynamic::object("marginBlock", 10))}));
    const Rect withCanonical =
        frameOf(11, commit(folly::dynamic::object("width", 100)("height", 300),
                           {box(folly::dynamic::object("marginTop", 10)("marginBottom", 10))}));

    EXPECT_EQ(withAlias.origin.y, withCanonical.origin.y);
    EXPECT_EQ(withAlias.origin.y, 10);
}

TEST_F(WebPropAliasTest, PaddingInlineIsTheSameAsPaddingLeftAndPaddingRight) {
    const Rect withAlias =
        frameOf(11, commit(folly::dynamic::object("width", 300)("height", 100)("paddingInline", 10),
                           {box(folly::dynamic::object())}));
    const Rect withCanonical =
        frameOf(11, commit(folly::dynamic::object("width", 300)("height", 100)("paddingLeft", 10)("paddingRight", 10),
                           {box(folly::dynamic::object())}));

    EXPECT_EQ(withAlias.origin.x, withCanonical.origin.x);
    EXPECT_EQ(withAlias.origin.x, 10);
}

TEST_F(WebPropAliasTest, PaddingBlockIsTheSameAsPaddingTopAndPaddingBottom) {
    const Rect withAlias =
        frameOf(11, commit(folly::dynamic::object("width", 100)("height", 300)("paddingBlock", 10),
                           {box(folly::dynamic::object())}));
    const Rect withCanonical =
        frameOf(11, commit(folly::dynamic::object("width", 100)("height", 300)("paddingTop", 10)("paddingBottom", 10),
                           {box(folly::dynamic::object())}));

    EXPECT_EQ(withAlias.origin.y, withCanonical.origin.y);
    EXPECT_EQ(withAlias.origin.y, 10);
}

/**
 * Padding shares `computeLeftEdge`/`computeTopEdge` with margin and inset, so the same precedence cascade
 * applies: the physical edge outranks the logical `paddingInline` shorthand.
 */
TEST_F(WebPropAliasTest, PaddingLeftOutranksPaddingInline) {
    const Rect frame =
        frameOf(11, commit(folly::dynamic::object("width", 300)("height", 100)("paddingLeft", 30)("paddingInline",
                                                                                                   5),
                           {box(folly::dynamic::object())}));

    EXPECT_EQ(frame.origin.x, 30);
}

} // namespace
