#include "SceneTestSupport.h"

#include <gtest/gtest.h>

#include <react/renderer/graphics/BoxShadow.h>

// Issue #67. A shadow is the one View feature that paints outside the node's own box, so its two halves in the
// gate are what it reaches — the damage rectangle a shadowed node has to invalidate — and what it is made of: the
// `boxShadow` list as authored, and the legacy iOS quartet resolved onto the same shape so a cross-platform app
// casts one drawing whichever props it wrote. The painter only issues the Skia calls; the picture is
// `shadow.png`.

namespace {

using facebook::react::BoxShadow;
using react_native_linux::SceneShadow;
using react_native_linux::shadowExtent;

constexpr Tag kCardTag = 2;
constexpr uint32_t kInkArgb = 0xCC000000U;

Rect cardFrame() { return makeRect(100, 100, 160, 100); }

SceneShadow outsetShadow(float offsetX, float offsetY, float blur, float spread) {
    return SceneShadow{.offsetX = offsetX,
                       .offsetY = offsetY,
                       .blurRadius = blur,
                       .spreadDistance = spread,
                       .colorArgb = kInkArgb,
                       .isInset = false};
}

SceneSnapshot snapshotOf(const std::shared_ptr<ViewProps>& viewProps) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(kCardTag, cardFrame(), viewProps));

    return scene.snapshot();
}

TEST(ShadowExtentTest, NoShadowsReachNoFurtherThanTheBox) {
    const Rect extent = shadowExtent(cardFrame(), {});

    EXPECT_EQ(extent, cardFrame());
}

TEST(ShadowExtentTest, AnOutsetShadowReachesBlurPlusSpreadPastEverySideAndItsOffsetPastTwoOfThem) {
    // Reach is 16 + 4 = 20 on every side, and the offset of (8, 6) moves that reach: 12 on the left, 28 on the
    // right, 14 on the top, 26 on the bottom.
    const Rect extent = shadowExtent(cardFrame(), {outsetShadow(8, 6, 16, 4)});

    EXPECT_FLOAT_EQ(extent.origin.x, 88);
    EXPECT_FLOAT_EQ(extent.origin.y, 86);
    EXPECT_FLOAT_EQ(extent.size.width, 200);
    EXPECT_FLOAT_EQ(extent.size.height, 140);
}

TEST(ShadowExtentTest, AnOffsetLargerThanTheReachNeverPullsTheExtentInsideTheBox) {
    // Offset 30 right with a reach of 10 reaches 40 past the right edge and nothing past the left.
    const Rect extent = shadowExtent(cardFrame(), {outsetShadow(30, 0, 10, 0)});

    EXPECT_FLOAT_EQ(extent.origin.x, 100);
    EXPECT_FLOAT_EQ(extent.size.width, 200);
}

TEST(ShadowExtentTest, SeveralShadowsReachAsFarAsTheFurthestOfThemOnEachSide) {
    // The first reaches 4 on every side and 24 below; the second reaches 2 on every side and 14 to the left. The
    // top is the larger of the two tops, which is the second shadow's 2 — the first one's offset pulls its own
    // reach off the top entirely.
    const Rect extent = shadowExtent(cardFrame(), {outsetShadow(0, 20, 4, 0), outsetShadow(-12, 0, 2, 0)});

    EXPECT_FLOAT_EQ(extent.origin.x, 86);
    EXPECT_FLOAT_EQ(extent.origin.y, 98);
    EXPECT_FLOAT_EQ(extent.size.width, 178);
    EXPECT_FLOAT_EQ(extent.size.height, 126);
}

TEST(ShadowExtentTest, AnInsetShadowReachesNowhereOutsideTheBox) {
    SceneShadow inset = outsetShadow(8, 8, 16, 4);

    inset.isInset = true;

    EXPECT_EQ(shadowExtent(cardFrame(), {inset}), cardFrame());
}

TEST(ShadowResolutionTest, TheBoxShadowListReachesThePrimitiveInPaintOrder) {
    const std::shared_ptr<ViewProps> viewProps = propsWithBackground(blue());

    viewProps->boxShadow = {
        BoxShadow{.offsetX = 0, .offsetY = 2, .blurRadius = 4, .spreadDistance = 0, .color = red(), .inset = false},
        BoxShadow{.offsetX = 0, .offsetY = 12, .blurRadius = 32, .spreadDistance = 1, .color = blue(), .inset = true},
    };

    const SceneSnapshot snapshot = snapshotOf(viewProps);

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_EQ(snapshot[0].shadows.size(), 2U);
    EXPECT_FLOAT_EQ(snapshot[0].shadows[0].offsetY, 2);
    EXPECT_EQ(snapshot[0].shadows[0].colorArgb, kRedArgb);
    EXPECT_FALSE(snapshot[0].shadows[0].isInset);
    EXPECT_FLOAT_EQ(snapshot[0].shadows[1].spreadDistance, 1);
    EXPECT_TRUE(snapshot[0].shadows[1].isInset);
}

TEST(ShadowResolutionTest, TheLegacyQuartetIsOneOutsetShadowWithTheRadiusDoubledAndTheOpacityFoldedIn) {
    const std::shared_ptr<ViewProps> viewProps = propsWithBackground(blue());

    viewProps->shadowColor = facebook::react::colorFromRGBA(0, 0, 0, 255);
    viewProps->shadowOffset = Size{.width = 8, .height = 6};
    viewProps->shadowOpacity = 0.5F;
    viewProps->shadowRadius = 8;

    const SceneSnapshot snapshot = snapshotOf(viewProps);

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_EQ(snapshot[0].shadows.size(), 1U);
    EXPECT_FLOAT_EQ(snapshot[0].shadows[0].offsetX, 8);
    EXPECT_FLOAT_EQ(snapshot[0].shadows[0].offsetY, 6);
    // iOS' radius is a sigma; the CSS blur it becomes is two of them.
    EXPECT_FLOAT_EQ(snapshot[0].shadows[0].blurRadius, 16);
    EXPECT_EQ(snapshot[0].shadows[0].colorArgb, 0x80000000U);
}

TEST(ShadowResolutionTest, AQuartetWithNoOpacityCastsNothingAndSoDoesOneWithNoColour) {
    const std::shared_ptr<ViewProps> coloured = propsWithBackground(blue());

    coloured->shadowColor = red();

    const std::shared_ptr<ViewProps> opaque = propsWithBackground(blue());

    opaque->shadowOpacity = 1.0F;

    EXPECT_TRUE(snapshotOf(coloured)[0].shadows.empty());
    EXPECT_TRUE(snapshotOf(opaque)[0].shadows.empty());
}

TEST(ShadowResolutionTest, TheInheritedOpacityFoldsIntoTheShadowColourLikeEveryOtherColour) {
    const std::shared_ptr<ViewProps> viewProps = propsWithBackground(blue());

    viewProps->opacity = 0.5;
    viewProps->boxShadow = {
        BoxShadow{.offsetX = 0, .offsetY = 0, .blurRadius = 8, .spreadDistance = 0, .color = red(), .inset = false}};

    const SceneSnapshot snapshot = snapshotOf(viewProps);

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot[0].shadows[0].colorArgb, 0x80CC3333U);
}

TEST(ShadowDamageTest, AShadowedNodeDamagesWhereItsShadowReaches) {
    const std::shared_ptr<ViewProps> viewProps = propsWithBackground(blue());

    viewProps->boxShadow = {
        BoxShadow{.offsetX = 10, .offsetY = 10, .blurRadius = 10, .spreadDistance = 0, .color = red(), .inset = false}};

    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(kCardTag, cardFrame(), viewProps));

    const SceneDamage damage = scene.takeDamage();

    // The box is (100, 100) 160x100; the shadow reaches 20 down-right of it, so the damage runs to 280 and 220.
    ASSERT_FALSE(damage.empty());

    Rect covered = damage.front();

    for (const Rect& rect : damage) {
        covered.unionInPlace(rect);
    }

    EXPECT_LE(covered.origin.x, 100);
    EXPECT_LE(covered.origin.y, 100);
    EXPECT_GE(covered.origin.x + covered.size.width, 280);
    EXPECT_GE(covered.origin.y + covered.size.height, 220);
}

} // namespace
