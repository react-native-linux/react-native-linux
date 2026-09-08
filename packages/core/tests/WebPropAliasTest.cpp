#include "FocusModel.h"
#include "ShadowTreeTestSupport.h"

#include <folly/dynamic.h>
#include <gtest/gtest.h>
#include <memory>
#include <utility>

#include <react/renderer/components/view/AccessibilityPrimitives.h>
#include <react/renderer/components/view/AccessibilityProps.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>
#include <react/renderer/components/view/ViewShadowNode.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/renderer/core/LayoutableShadowNode.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/mounting/ShadowTree.h>

namespace {

using react_native_linux::effectiveAccessibilityRole;
using react_native_linux::isActivationKey;
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
/**
 * What a mounted node's role props resolve to: the bitmask every other test suite reads
 * (`RetainedScene`, the golden accessibility tree) and the string `InputDispatcher::accessibilityRoleOf` reads for
 * keyboard activation. The two do not necessarily agree — see `RoleResolutionTest` below.
 */
struct RoleResolution final {
    facebook::react::AccessibilityTraits traits;
    std::string accessibilityRole;
    facebook::react::Role role;
};

class WebPropAliasTest : public ::testing::Test {
protected:
    RoleResolution roleResolutionFor(folly::dynamic props) {
        const std::shared_ptr<const ShadowNode> node = makeConfiguredShadowNode(
            viewDescriptor_, 1, kSurfaceId, contextContainer_, std::move(props), std::make_shared<const ChildList>());
        const auto& viewProps = std::static_pointer_cast<const ViewShadowNode>(node)->getConcreteProps();

        return RoleResolution{.traits = viewProps.accessibilityTraits,
                              .accessibilityRole = viewProps.accessibilityRole,
                              .role = viewProps.role};
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

/**
 * `AccessibilityProps`' constructor reads `role` and `accessibilityRole` for two different outputs, and they do
 * not travel together: `accessibilityTraits` — the bitmask everything else in this platform reads — is built from
 * whichever of the two is present, `role` winning when both are; the `accessibilityRole` *string* —
 * `InputDispatcher::accessibilityRoleOf`'s only source for the activation-key behaviour — is set only from the
 * literal `accessibilityRole` raw prop, never from `role`. A non-default role (`link`, not the `AccessibilityRole`
 * default) is used throughout so a broken conversion cannot hide behind a value that also happens to be the
 * type's zero state.
 */
TEST_F(WebPropAliasTest, RoleAloneSetsTheTraitButLeavesTheAccessibilityRoleStringUnset) {
    const RoleResolution resolution = roleResolutionFor(folly::dynamic::object("role", "link"));

    EXPECT_EQ(resolution.traits, facebook::react::AccessibilityTraits::Link);
    EXPECT_EQ(resolution.accessibilityRole, "");
}

TEST_F(WebPropAliasTest, AccessibilityRoleAloneSetsBothTheTraitAndTheAccessibilityRoleString) {
    const RoleResolution resolution = roleResolutionFor(folly::dynamic::object("accessibilityRole", "link"));

    EXPECT_EQ(resolution.traits, facebook::react::AccessibilityTraits::Link);
    EXPECT_EQ(resolution.accessibilityRole, "link");
}

TEST_F(WebPropAliasTest, WhenBothAreGivenRoleTakesPrecedenceOverAccessibilityRoleForTheTraitButNotForTheString) {
    const RoleResolution resolution =
        roleResolutionFor(folly::dynamic::object("role", "link")("accessibilityRole", "button"));

    EXPECT_EQ(resolution.traits, facebook::react::AccessibilityTraits::Link);
    EXPECT_EQ(resolution.accessibilityRole, "button");
}

/**
 * The activation-key rule (#248, #320): `InputDispatcher::accessibilityRoleOf` feeds `isActivationKey` through
 * `effectiveAccessibilityRole` rather than the raw `accessibilityRole` string, so `role="link"` behaves exactly
 * like `accessibilityRole="link"` — Enter activates, Space does not — even though only the second spelling ever
 * populates the string this suite's earlier tests read directly.
 */
TEST_F(WebPropAliasTest, RoleLinkAloneActivatesOnEnterAndNotOnSpaceLikeAccessibilityRoleLinkDoes) {
    const RoleResolution roleAlone = roleResolutionFor(folly::dynamic::object("role", "link"));
    const RoleResolution accessibilityRoleAlone =
        roleResolutionFor(folly::dynamic::object("accessibilityRole", "link"));

    const std::string roleAloneEffective = effectiveAccessibilityRole(roleAlone.accessibilityRole, roleAlone.role);
    const std::string accessibilityRoleAloneEffective =
        effectiveAccessibilityRole(accessibilityRoleAlone.accessibilityRole, accessibilityRoleAlone.role);

    EXPECT_EQ(roleAloneEffective, accessibilityRoleAloneEffective);
    EXPECT_TRUE(isActivationKey(roleAloneEffective, "Enter"));
    EXPECT_FALSE(isActivationKey(roleAloneEffective, " "));
    EXPECT_TRUE(isActivationKey(accessibilityRoleAloneEffective, "Enter"));
    EXPECT_FALSE(isActivationKey(accessibilityRoleAloneEffective, " "));
}

TEST_F(WebPropAliasTest, WhenBothAreGivenTheStringWinsForActivationJustAsItDoesForTheAccessibilityRoleField) {
    const RoleResolution resolution =
        roleResolutionFor(folly::dynamic::object("role", "link")("accessibilityRole", "button"));
    const std::string effective = effectiveAccessibilityRole(resolution.accessibilityRole, resolution.role);

    EXPECT_EQ(effective, "button");
    EXPECT_TRUE(isActivationKey(effective, "Enter"));
    EXPECT_TRUE(isActivationKey(effective, " "));
}

#pragma mark - inset family (position: absolute)

constexpr bool kOmitDimension = false;
constexpr bool kFixDimension = true;

/**
 * A `position: absolute` leaf, its width and height each either fixed at 50 or left undefined so Yoga derives
 * them from the inset edges instead. A trailing edge (`right`, `bottom`) only ever moves the origin when the
 * matching dimension is fixed — with it undefined, Yoga stretches the box to satisfy both edges, which is the
 * only way a test can observe whether an alias carried the trailing edge at all.
 */
folly::dynamic absoluteBox(folly::dynamic extra, bool fixWidth = kFixDimension, bool fixHeight = kFixDimension) {
    folly::dynamic props = folly::dynamic::object("position", "absolute");

    if (fixWidth) {
        props["width"] = 50;
    }
    if (fixHeight) {
        props["height"] = 50;
    }

    for (const auto& entry : extra.items()) {
        props[entry.first] = entry.second;
    }

    return props;
}

TEST_F(WebPropAliasTest, InsetIsTheSameAsSettingAllFourPhysicalEdges) {
    const Rect withAlias =
        frameOf(11, commit(folly::dynamic::object("width", 300)("height", 300),
                           {absoluteBox(folly::dynamic::object("inset", 20), kOmitDimension, kOmitDimension)}));
    const Rect withCanonical =
        frameOf(11, commit(folly::dynamic::object("width", 300)("height", 300),
                           {absoluteBox(folly::dynamic::object("top", 20)("left", 20)("right", 20)("bottom", 20),
                                        kOmitDimension, kOmitDimension)}));

    EXPECT_EQ(withAlias, withCanonical);
    EXPECT_EQ(withAlias, (Rect{.origin = {.x = 20, .y = 20}, .size = {.width = 260, .height = 260}}));
}

TEST_F(WebPropAliasTest, InsetInlineIsTheSameAsSettingLeftAndRight) {
    const Rect withAlias =
        frameOf(11, commit(folly::dynamic::object("width", 300)("height", 300),
                           {absoluteBox(folly::dynamic::object("insetInline", 15), kOmitDimension, kFixDimension)}));
    const Rect withCanonical = frameOf(
        11, commit(folly::dynamic::object("width", 300)("height", 300),
                   {absoluteBox(folly::dynamic::object("left", 15)("right", 15), kOmitDimension, kFixDimension)}));

    EXPECT_EQ(withAlias, withCanonical);
    EXPECT_EQ(withAlias.origin.x, 15);
    EXPECT_EQ(withAlias.size.width, 270);
}

TEST_F(WebPropAliasTest, InsetBlockIsTheSameAsSettingTopAndBottom) {
    const Rect withAlias =
        frameOf(11, commit(folly::dynamic::object("width", 300)("height", 300),
                           {absoluteBox(folly::dynamic::object("insetBlock", 15), kFixDimension, kOmitDimension)}));
    const Rect withCanonical = frameOf(
        11, commit(folly::dynamic::object("width", 300)("height", 300),
                   {absoluteBox(folly::dynamic::object("top", 15)("bottom", 15), kFixDimension, kOmitDimension)}));

    EXPECT_EQ(withAlias, withCanonical);
    EXPECT_EQ(withAlias.origin.y, 15);
    EXPECT_EQ(withAlias.size.height, 270);
}

TEST_F(WebPropAliasTest, StartIsTheSameAsLeftInLeftToRightDirection) {
    const Rect withAlias = frameOf(11, commit(folly::dynamic::object("width", 300)("height", 300),
                                              {absoluteBox(folly::dynamic::object("start", 25))}));
    const Rect withCanonical = frameOf(11, commit(folly::dynamic::object("width", 300)("height", 300),
                                                  {absoluteBox(folly::dynamic::object("left", 25))}));

    EXPECT_EQ(withAlias.origin.x, withCanonical.origin.x);
    EXPECT_EQ(withAlias.origin.x, 25);
}

TEST_F(WebPropAliasTest, EndIsTheSameAsRightInLeftToRightDirection) {
    const Rect withAlias = frameOf(11, commit(folly::dynamic::object("width", 300)("height", 300),
                                              {absoluteBox(folly::dynamic::object("end", 20))}));
    const Rect withCanonical = frameOf(11, commit(folly::dynamic::object("width", 300)("height", 300),
                                                  {absoluteBox(folly::dynamic::object("right", 20))}));

    EXPECT_EQ(withAlias.origin.x, withCanonical.origin.x);
    EXPECT_EQ(withAlias.origin.x, 300 - 50 - 20);
}

/**
 * `start` and `end` are direction-aware, not fixed physical aliases: Yoga's `layoutAbsoluteChild` resolves an
 * absolutely-positioned child's logical inset edges against its *containing block's* resolved direction, not the
 * child's own, so the container — not the child — is where `direction: "rtl"` has to be set. `computeLeftEdge`
 * and `computeRightEdge` only read `Start`/`End` for the leading/trailing edge in `LTR`; in `RTL` they swap, so a
 * mapping that always sent `start` to `left` and `end` to `right` would still pass every test above.
 */
TEST_F(WebPropAliasTest, StartIsTheSameAsRightInRightToLeftDirection) {
    const Rect withAlias = frameOf(11, commit(folly::dynamic::object("width", 300)("height", 300)("direction", "rtl"),
                                              {absoluteBox(folly::dynamic::object("start", 25))}));
    const Rect withCanonical =
        frameOf(11, commit(folly::dynamic::object("width", 300)("height", 300)("direction", "rtl"),
                           {absoluteBox(folly::dynamic::object("right", 25))}));

    EXPECT_EQ(withAlias.origin.x, withCanonical.origin.x);
    EXPECT_EQ(withAlias.origin.x, 300 - 50 - 25);
}

TEST_F(WebPropAliasTest, EndIsTheSameAsLeftInRightToLeftDirection) {
    const Rect withAlias = frameOf(11, commit(folly::dynamic::object("width", 300)("height", 300)("direction", "rtl"),
                                              {absoluteBox(folly::dynamic::object("end", 20))}));
    const Rect withCanonical =
        frameOf(11, commit(folly::dynamic::object("width", 300)("height", 300)("direction", "rtl"),
                           {absoluteBox(folly::dynamic::object("left", 20))}));

    EXPECT_EQ(withAlias.origin.x, withCanonical.origin.x);
    EXPECT_EQ(withAlias.origin.x, 20);
}

/**
 * The precedence Yoga's `Style::computeLeftEdge` states: the logical, direction-aware `start` beats the physical
 * `left`, which beats the logical shorthand `insetInline`, which beats the `inset` shorthand for all four edges.
 * A test that only ever sets one of the four never proves this — the point of a precedence test is the case where
 * two disagree.
 */
TEST_F(WebPropAliasTest, StartOutranksLeftWhichOutranksInsetInlineWhichOutranksInset) {
    const Rect leftOverInset = frameOf(11, commit(folly::dynamic::object("width", 300)("height", 300),
                                                  {absoluteBox(folly::dynamic::object("left", 30)("inset", 5))}));
    const Rect insetInlineOverInset =
        frameOf(11, commit(folly::dynamic::object("width", 300)("height", 300),
                           {absoluteBox(folly::dynamic::object("insetInline", 12)("inset", 5))}));
    const Rect startOverLeft = frameOf(11, commit(folly::dynamic::object("width", 300)("height", 300),
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
    const std::map<Tag, Rect> withCanonical = commit(folly::dynamic::object("width", 100)("height", 300)("rowGap", 10),
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
    const Rect withCanonical = frameOf(11, commit(folly::dynamic::object("width", 100)("height", 300),
                                                  {box(folly::dynamic::object("marginTop", 10)("marginBottom", 10))}));

    EXPECT_EQ(withAlias.origin.y, withCanonical.origin.y);
    EXPECT_EQ(withAlias.origin.y, 10);
}

TEST_F(WebPropAliasTest, PaddingInlineIsTheSameAsPaddingLeftAndPaddingRight) {
    const Rect withAlias = frameOf(11, commit(folly::dynamic::object("width", 300)("height", 100)("paddingInline", 10),
                                              {box(folly::dynamic::object())}));
    const Rect withCanonical =
        frameOf(11, commit(folly::dynamic::object("width", 300)("height", 100)("paddingLeft", 10)("paddingRight", 10),
                           {box(folly::dynamic::object())}));

    EXPECT_EQ(withAlias.origin.x, withCanonical.origin.x);
    EXPECT_EQ(withAlias.origin.x, 10);
}

TEST_F(WebPropAliasTest, PaddingBlockIsTheSameAsPaddingTopAndPaddingBottom) {
    const Rect withAlias = frameOf(11, commit(folly::dynamic::object("width", 100)("height", 300)("paddingBlock", 10),
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
        frameOf(11, commit(folly::dynamic::object("width", 300)("height", 100)("paddingLeft", 30)("paddingInline", 5),
                           {box(folly::dynamic::object())}));

    EXPECT_EQ(frame.origin.x, 30);
}

} // namespace
