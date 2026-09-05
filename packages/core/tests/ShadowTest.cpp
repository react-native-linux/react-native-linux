#include "SceneTestSupport.h"

#include <gtest/gtest.h>

#include <react/renderer/graphics/BoxShadow.h>

// Issue #67. A shadow is the one View feature that paints outside the node's own box, so its two halves in the
// gate are what it reaches — the damage rectangle a shadowed node has to invalidate — and what it is made of: the
// `boxShadow` list as authored, and the legacy iOS quartet resolved onto the same shape so a cross-platform app
// casts one drawing whichever props it wrote. The painter only issues the Skia calls; the picture is
// `shadow.png`.
//
// Issue #102 adds the composition half: the damage of a shadowed node under a transform, the damage of a repaint
// that changes nothing but the colour under the shadow, and the shadow's absence from the hit region. The picture
// for that half is `shadow-composition.png`.

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
    // Reach is 16 * 1.5 + 4 = 28 on every side, and the offset of (8, 6) moves that reach: 20 on the left, 36 on
    // the right, 22 on the top, 34 on the bottom.
    const Rect extent = shadowExtent(cardFrame(), {outsetShadow(8, 6, 16, 4)});

    EXPECT_FLOAT_EQ(extent.origin.x, 80);
    EXPECT_FLOAT_EQ(extent.origin.y, 78);
    EXPECT_FLOAT_EQ(extent.size.width, 216);
    EXPECT_FLOAT_EQ(extent.size.height, 156);
}

TEST(ShadowExtentTest, AnOffsetLargerThanTheReachNeverPullsTheExtentInsideTheBox) {
    // Offset 30 right with a reach of 15 reaches 45 past the right edge and nothing past the left.
    const Rect extent = shadowExtent(cardFrame(), {outsetShadow(30, 0, 10, 0)});

    EXPECT_FLOAT_EQ(extent.origin.x, 100);
    EXPECT_FLOAT_EQ(extent.size.width, 205);
}

TEST(ShadowExtentTest, SeveralShadowsReachAsFarAsTheFurthestOfThemOnEachSide) {
    // The first reaches 6 on every side and 26 below; the second reaches 3 on every side and 15 to the left. The
    // top is the larger of the two tops, which is the second shadow's 3 — the first one's offset pulls its own
    // reach off the top entirely.
    const Rect extent = shadowExtent(cardFrame(), {outsetShadow(0, 20, 4, 0), outsetShadow(-12, 0, 2, 0)});

    EXPECT_FLOAT_EQ(extent.origin.x, 85);
    EXPECT_FLOAT_EQ(extent.origin.y, 97);
    EXPECT_FLOAT_EQ(extent.size.width, 181);
    EXPECT_FLOAT_EQ(extent.size.height, 129);
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

Rect boundsOf(const SceneDamage& damage) {
    Rect covered = damage.front();

    for (const Rect& rect : damage) {
        covered.unionInPlace(rect);
    }

    return covered;
}

// The box is (100, 100) 160x100 and the shadow below reaches 15 out and 10 down-right of it, so whatever the
// scene damages has to run from (95, 95) to (285, 225) — when the shadow arrives, when it leaves, and when only
// the colour under it changed.
void expectDamageCoversTheShadowReach(const SceneDamage& damage) {
    ASSERT_FALSE(damage.empty());

    const Rect covered = boundsOf(damage);

    EXPECT_LE(covered.origin.x, 95);
    EXPECT_LE(covered.origin.y, 95);
    EXPECT_GE(covered.origin.x + covered.size.width, 285);
    EXPECT_GE(covered.origin.y + covered.size.height, 225);
}

std::shared_ptr<ViewProps> propsWithShadow(float offsetX, float offsetY, float blur) {
    const std::shared_ptr<ViewProps> viewProps = propsWithBackground(blue());

    viewProps->boxShadow = {BoxShadow{.offsetX = offsetX,
                                      .offsetY = offsetY,
                                      .blurRadius = blur,
                                      .spreadDistance = 0,
                                      .color = red(),
                                      .inset = false}};

    return viewProps;
}

// The mount damages the whole surface, which would cover any shadow whether or not the extent knew about one. So
// every assertion below is about a *change*, read from a scene whose mount damage has already been taken: the
// card mounts with `before`, the mount damage is drained, and the card is updated to `after`.
SceneDamage damageOfUpdatingTheCard(const std::shared_ptr<ViewProps>& before,
                                    const std::shared_ptr<ViewProps>& after) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(kCardTag, cardFrame(), before));
    scene.takeDamage();
    scene.updateNode(makeStyledView(kCardTag, cardFrame(), after));

    return scene.takeDamage();
}

TEST(ShadowDamageTest, GainingAShadowDamagesWhereTheShadowWillReach) {
    expectDamageCoversTheShadowReach(damageOfUpdatingTheCard(propsWithBackground(blue()), propsWithShadow(10, 10, 10)));
}

TEST(ShadowDamageTest, LosingAShadowDamagesWhereTheShadowWas) {
    // The pixels the shadow occupied have to be repainted even though the node that owns them no longer reaches
    // there, so the vacated extent is the same rectangle the shadow damaged when it arrived.
    expectDamageCoversTheShadowReach(damageOfUpdatingTheCard(propsWithShadow(10, 10, 10), propsWithBackground(blue())));
}

// Issue #102, core#47920: a shadowed field repainting on every keystroke alternated visible and hidden because the
// repaint covered the frame and the shadow lived outside it. Nothing about the shadow changes here — only the
// background colour does — and the reach still has to be in the damage.
TEST(ShadowDamageTest, RepaintingAShadowedNodeDamagesTheShadowAndNotOnlyTheFrame) {
    const std::shared_ptr<ViewProps> repainted = propsWithShadow(10, 10, 10);

    repainted->backgroundColor = red();

    expectDamageCoversTheShadowReach(damageOfUpdatingTheCard(propsWithShadow(10, 10, 10), repainted));
}

// Issue #102, core#50775 and core#34320: the shadow is grown in the node's own space and *then* mapped, so a
// doubled node's shadow reaches twice as far. Growing the already-mapped frame would reach half as far, and a
// shadow drawn in screen space after the fact would not grow with the node at all.
TEST(ShadowDamageTest, AScaledNodesShadowIsGrownBeforeTheMatrixRatherThanAfterIt) {
    const std::shared_ptr<ViewProps> scaled = propsWithBackground(blue());

    scaled->transform = Transform::Scale(2, 2, 1);

    const std::shared_ptr<ViewProps> scaledAndShadowed = propsWithShadow(0, 0, 0);

    scaledAndShadowed->transform = scaled->transform;
    scaledAndShadowed->boxShadow[0].spreadDistance = 10;

    // The card is (100, 100) 160x100, so scale 2 about its centre paints it over (20, 50) to (340, 250). A spread
    // of 10 in the card's own space is 20 in the surface's, which is (0, 30) to (360, 270).
    const Rect covered = boundsOf(damageOfUpdatingTheCard(scaled, scaledAndShadowed));

    EXPECT_FLOAT_EQ(covered.origin.x, 0);
    EXPECT_FLOAT_EQ(covered.origin.y, 30);
    EXPECT_FLOAT_EQ(covered.size.width, 360);
    EXPECT_FLOAT_EQ(covered.size.height, 240);
}

// Issue #102's sixth requirement: a shadow is paint and never a target. The point below is well inside the
// rectangle the shadow damages and well outside the box, which is exactly where a hit test that reused
// `shadowExtent` would answer with the card.
TEST(ShadowHitTest, APointInTheShadowButOutsideTheBoxHitsNothing) {
    RetainedScene scene;
    const std::shared_ptr<ViewProps> viewProps = propsWithShadow(40, 40, 0);

    viewProps->boxShadow[0].spreadDistance = 20;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(kCardTag, cardFrame(), viewProps));

    // The card is (100, 100) 160x100 and its shadow reaches to (320, 240), so (300, 220) is shadow and only that:
    // the press falls through the shadow to the surface behind it.
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, Point{.x = 300, .y = 220}).tag, kSurfaceTag);
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, Point{.x = 200, .y = 150}).tag, kCardTag);
}

} // namespace
