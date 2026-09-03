#include "SceneTestSupport.h"

#include <gtest/gtest.h>

#include <react/renderer/components/view/primitives.h>
#include <yoga/enums/Edge.h>
#include <yoga/enums/Overflow.h>
#include <yoga/style/StyleLength.h>

// Issues #99 and #100.
//
// #99: the fill, the border ring, the `overflow: hidden` clip, the content clip and the hit region all derive from
// one `SceneRoundedBox`. `roundedBorderBox` produces it, `roundedContentBox` produces the box a ring of given
// widths leaves inside it, and `roundedBoxContainsPoint` is the containment every hit test asks. `ScenePainter`
// draws exactly those three answers, so a press that misses a corner misses a corner no pixel was painted in.
//
// #100: the widths half of the border matrix — what a sub-pixel `StyleSheet.hairlineWidth` edge becomes, and what
// a zero width stays. The colours half is the painter's, and the golden `border-matrix.png` is what reads it.
//
// See *View props fidelity* and *Hit-testing under animation* in docs/cpp-toolchain.md.

namespace {

namespace yoga = facebook::yoga;

using facebook::react::BorderRadii;
using facebook::react::BorderWidths;
using facebook::react::CornerRadii;
using react_native_linux::SceneRoundedBox;
using react_native_linux::roundedBorderBox;
using react_native_linux::roundedBoxContainsPoint;
using react_native_linux::roundedContentBox;

constexpr Tag kBoxTag = 2;
constexpr Tag kInnerTag = 3;

Point pointAt(float x, float y) {
    return Point{.x = x, .y = y};
}

CornerRadii circularCorner(float radius) {
    return CornerRadii{.vertical = radius, .horizontal = radius};
}

BorderRadii uniformRadii(float radius) {
    return BorderRadii{.topLeft = circularCorner(radius),
                       .topRight = circularCorner(radius),
                       .bottomLeft = circularCorner(radius),
                       .bottomRight = circularCorner(radius)};
}

void expectCorner(const CornerRadii& corner, float horizontal, float vertical) {
    EXPECT_FLOAT_EQ(corner.horizontal, horizontal);
    EXPECT_FLOAT_EQ(corner.vertical, vertical);
}

// A frame whose top-left corner arc is centred at (160, 140) with a radius of 60, so the arithmetic in the press
// tests below is readable rather than derived.
Rect boxFrame() {
    return makeRect(100, 80, 200, 120);
}

constexpr float kCornerRadius = 60.0F;

std::shared_ptr<ViewProps> roundedProps(float radius) {
    const std::shared_ptr<ViewProps> viewProps = propsWithBackground(blue());

    viewProps->borderRadii.all = ValueUnit{radius, UnitType::Point};

    return viewProps;
}

RetainedScene sceneWithRoundedBox(const std::shared_ptr<ViewProps>& viewProps) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(kBoxTag, boxFrame(), viewProps));

    return scene;
}

SceneSnapshot snapshotOfBorderWidths(float width, float pointScaleFactor) {
    const std::shared_ptr<ViewProps> viewProps = propsWithBackground(blue());

    viewProps->yogaStyle.setBorder(yoga::Edge::Left, yoga::StyleLength::points(width));
    viewProps->borderColors.all = red();

    ShadowView shadowView = makeStyledView(kBoxTag, boxFrame(), viewProps);

    shadowView.layoutMetrics.pointScaleFactor = pointScaleFactor;

    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, shadowView);

    return scene.snapshot();
}

TEST(BorderGeometryTest, TheContentBoxIsTheBorderBoxInsetByEachSideWithItsCornersReduced) {
    const SceneRoundedBox borderBox = roundedBorderBox(makeRect(10, 20, 200, 100), uniformRadii(30));
    const SceneRoundedBox contentBox = roundedContentBox(
        borderBox, BorderWidths{.left = 4, .top = 6, .right = 8, .bottom = 10});

    EXPECT_FLOAT_EQ(contentBox.bounds.origin.x, 14);
    EXPECT_FLOAT_EQ(contentBox.bounds.origin.y, 26);
    EXPECT_FLOAT_EQ(contentBox.bounds.size.width, 188);
    EXPECT_FLOAT_EQ(contentBox.bounds.size.height, 84);

    // The CSS inner-radius rule: each corner loses the two widths that meet at it, on its own axis.
    expectCorner(contentBox.radii.topLeft, 26, 24);
    expectCorner(contentBox.radii.topRight, 22, 24);
    expectCorner(contentBox.radii.bottomLeft, 26, 20);
    expectCorner(contentBox.radii.bottomRight, 22, 20);
}

TEST(BorderGeometryTest, ABorderWiderThanTheBoxCollapsesTheContentBoxInsteadOfInvertingIt) {
    const SceneRoundedBox borderBox = roundedBorderBox(makeRect(0, 0, 40, 20), uniformRadii(6));
    const SceneRoundedBox contentBox = roundedContentBox(
        borderBox, BorderWidths{.left = 100, .top = 100, .right = 100, .bottom = 100});

    EXPECT_FLOAT_EQ(contentBox.bounds.origin.x, 40);
    EXPECT_FLOAT_EQ(contentBox.bounds.origin.y, 20);
    EXPECT_FLOAT_EQ(contentBox.bounds.size.width, 0);
    EXPECT_FLOAT_EQ(contentBox.bounds.size.height, 0);
    expectCorner(contentBox.radii.topLeft, 0, 0);
}

TEST(BorderGeometryTest, ASquareBoxContainsEveryPointInsideItsBoundsAndNothingOutsideThem) {
    const SceneRoundedBox box = roundedBorderBox(makeRect(0, 0, 100, 50), BorderRadii{});

    EXPECT_TRUE(roundedBoxContainsPoint(box, pointAt(0, 0)));
    EXPECT_TRUE(roundedBoxContainsPoint(box, pointAt(50, 25)));
    EXPECT_FALSE(roundedBoxContainsPoint(box, pointAt(-1, 25)));
    EXPECT_FALSE(roundedBoxContainsPoint(box, pointAt(50, 51)));
}

TEST(BorderGeometryTest, EachRoundedCornerExcludesThePointsItsOwnArcDoesNotCover) {
    const SceneRoundedBox box = roundedBorderBox(makeRect(0, 0, 100, 100), uniformRadii(40));

    // Five points at the same offset from each of the four corners: the corner squares are 40x40 and (5, 5) from
    // a corner is 49.5 from that corner's arc centre, so every one of them is outside its own corner and inside
    // the other three.
    EXPECT_FALSE(roundedBoxContainsPoint(box, pointAt(5, 5)));
    EXPECT_FALSE(roundedBoxContainsPoint(box, pointAt(95, 5)));
    EXPECT_FALSE(roundedBoxContainsPoint(box, pointAt(95, 95)));
    EXPECT_FALSE(roundedBoxContainsPoint(box, pointAt(5, 95)));

    // Inside every arc: the middle of the box, and the middle of each edge, which no corner reaches.
    EXPECT_TRUE(roundedBoxContainsPoint(box, pointAt(50, 50)));
    EXPECT_TRUE(roundedBoxContainsPoint(box, pointAt(0, 50)));
    EXPECT_TRUE(roundedBoxContainsPoint(box, pointAt(50, 0)));
}

TEST(BorderGeometryTest, ASubPixelBorderWidthIsPromotedToOneDevicePixelAndAZeroWidthStaysZero) {
    const SceneSnapshot hairline = snapshotOfBorderWidths(0.5F, 1.0F);

    ASSERT_EQ(hairline.size(), 1U);
    EXPECT_FLOAT_EQ(hairline[0].borderWidths.left, 1.0F);
    EXPECT_FLOAT_EQ(hairline[0].borderWidths.top, 0.0F);

    // A denser grid can show the same fraction, so nothing is promoted there.
    const SceneSnapshot dense = snapshotOfBorderWidths(0.5F, 2.0F);

    ASSERT_EQ(dense.size(), 1U);
    EXPECT_FLOAT_EQ(dense[0].borderWidths.left, 0.5F);
}

TEST(BorderGeometryTest, ABorderWidthIsLeftAloneWhenThePointScaleFactorIsNotPositive) {
    const SceneSnapshot snapshot = snapshotOfBorderWidths(0.25F, 0.0F);

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_FLOAT_EQ(snapshot[0].borderWidths.left, 0.25F);
}

TEST(BorderGeometryTest, APressJustInsideARoundedCornerMissesTheNodeNoPixelWasPaintedFor) {
    const RetainedScene scene = sceneWithRoundedBox(roundedProps(kCornerRadius));

    // (105, 85) is 5 points inside the frame on both axes and 77.8 from the arc centre at (160, 140): the corner
    // was not painted there, so it is not pressed there either. The surface root is what is under it instead.
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(105, 85)).tag, kSurfaceTag);

    // (120, 120) is inside the same arc, and the centre of the box is nowhere near it.
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(120, 120)).tag, kBoxTag);
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(200, 140)).tag, kBoxTag);
}

TEST(BorderGeometryTest, TheHitRegionIsTheSameRoundedBoxTheSnapshotIsPaintedWith) {
    const RetainedScene scene = sceneWithRoundedBox(roundedProps(kCornerRadius));
    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);

    // The claim of issue #99, stated as one identity rather than as two similar answers: for every sample, the
    // painter's box — rebuilt here from the primitive the snapshot carries — and the hit test agree.
    const SceneRoundedBox painted = roundedBorderBox(snapshot[0].frame, snapshot[0].borderRadii);

    for (int column = 0; column < 70; column++) {
        for (int row = 0; row < 44; row++) {
            const Point sample = pointAt(96.0F + static_cast<float>(column * 3),
                                         76.0F + static_cast<float>(row * 3));
            const bool isPainted = roundedBoxContainsPoint(painted, sample);

            EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, sample).tag == kBoxTag, isPainted)
                << "at " << sample.x << ", " << sample.y;
        }
    }
}

TEST(BorderGeometryTest, ARoundedOverflowHiddenAncestorClipsAPressToItsOwnCorner) {
    const std::shared_ptr<ViewProps> clippingProps = roundedProps(kCornerRadius);

    clippingProps->yogaStyle.setOverflow(yoga::Overflow::Hidden);

    RetainedScene scene = sceneWithRoundedBox(clippingProps);

    // A square child covering the whole of its rounded parent. Its own frame contains the parent's corner; the
    // clip does not, and the clip is what was painted.
    addChild(scene, kBoxTag, makePaintedView(kInnerTag, makeRect(0, 0, 200, 120), red()));

    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(200, 140)).tag, kInnerTag);
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(105, 85)).tag, kSurfaceTag);
}

} // namespace
