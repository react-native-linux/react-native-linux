#include "LinuxMountingManager.h"
#include "RetainedScene.h"
#include "SceneTestSupport.h"
#include "TextInputComponent.h"

#include <cstdint>
#include <folly/dynamic.h>
#include <gtest/gtest.h>
#include <memory>
#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/AttributedStringBox.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>
#include <react/renderer/attributedstring/TextAttributes.h>
#include <react/renderer/components/image/ImageProps.h>
#include <react/renderer/components/image/ImageState.h>
#include <react/renderer/components/scrollview/ScrollViewState.h>
#include <react/renderer/components/text/ParagraphState.h>
#include <react/renderer/components/textinput/TextInputState.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteState.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/core/ShadowNodeFamily.h>
#include <react/renderer/graphics/BackgroundImage.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/ColorStop.h>
#include <react/renderer/graphics/LinearGradient.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/graphics/RectangleEdges.h>
#include <react/renderer/graphics/Size.h>
#include <react/renderer/graphics/Transform.h>
#include <react/renderer/graphics/ValueUnit.h>
#include <react/renderer/imagemanager/ImageRequest.h>
#include <react/renderer/imagemanager/ImageRequestParams.h>
#include <react/renderer/imagemanager/primitives.h>
#include <react/renderer/mounting/MountingTransaction.h>
#include <react/renderer/mounting/ShadowView.h>
#include <react/renderer/mounting/ShadowViewMutation.h>
#include <react/renderer/telemetry/TransactionTelemetry.h>
#include <string>
#include <utility>
#include <vector>
#include <yoga/enums/Edge.h>
#include <yoga/enums/Overflow.h>
#include <yoga/style/StyleLength.h>

namespace {

// A 45-degree Z rotation: its damage and clip boxes are the (w + h) / sqrt(2) rotated half-extents the skew
// terms of its affine matrix produce.
constexpr float kQuarterTurnRadians = 0.78539816F;

namespace yoga = facebook::yoga;

constexpr uint32_t kHalfBlueArgb = 0x803366CCU;
constexpr uint32_t kHalfRedArgb = 0x80CC3333U;
constexpr uint32_t kQuarterRedArgb = 0x40CC3333U;

SharedColor invisibleBlue() { return facebook::react::colorFromRGBA(51, 102, 204, 0); }

/**
 * One `experimental_backgroundImage` layer as `BaseViewProps` parses it: a corner keyword and two stops, the
 * first unpositioned so the scene is proved to copy the authored values rather than a resolved ramp.
 */
facebook::react::LinearGradient blueToRedGradient() {
    return facebook::react::LinearGradient{
        .direction = facebook::react::GradientKeyword::ToBottomRight,
        .colorStops = {facebook::react::ColorStop{.color = blue(), .position = ValueUnit{}},
                       facebook::react::ColorStop{.color = red(), .position = ValueUnit{100.0F, UnitType::Percent}}}};
}

std::shared_ptr<ViewProps> propsWithGradients(std::vector<facebook::react::BackgroundImage> backgroundImage) {
    const std::shared_ptr<ViewProps> viewProps = std::make_shared<ViewProps>();

    viewProps->backgroundImage = std::move(backgroundImage);

    return viewProps;
}

SceneSnapshot snapshotOfSingleChild(const std::shared_ptr<ViewProps>& viewProps, Rect frame) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(2, frame, viewProps));

    return scene.snapshot();
}

RetainedScene sceneWithPaintedChild() {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makePaintedView(2, makeRect(10, 20, 200, 100), blue()));

    return scene;
}

void expectPrimitive(const react_native_linux::ScenePrimitive& primitive, Rect frame, uint32_t colorArgb) {
    EXPECT_FLOAT_EQ(primitive.frame.origin.x, frame.origin.x);
    EXPECT_FLOAT_EQ(primitive.frame.origin.y, frame.origin.y);
    EXPECT_FLOAT_EQ(primitive.frame.size.width, frame.size.width);
    EXPECT_FLOAT_EQ(primitive.frame.size.height, frame.size.height);
    EXPECT_EQ(primitive.backgroundColorArgb, colorArgb);
}

void expectRect(const Rect& rect, Rect expected) {
    EXPECT_FLOAT_EQ(rect.origin.x, expected.origin.x);
    EXPECT_FLOAT_EQ(rect.origin.y, expected.origin.y);
    EXPECT_FLOAT_EQ(rect.size.width, expected.size.width);
    EXPECT_FLOAT_EQ(rect.size.height, expected.size.height);
}

bool sameMatrix(const react_native_linux::SceneMatrix& left, const react_native_linux::SceneMatrix& right) {
    return left.scaleX == right.scaleX && left.scaleY == right.scaleY && left.skewX == right.skewX &&
           left.skewY == right.skewY && left.translateX == right.translateX && left.translateY == right.translateY;
}

bool sameClipList(const std::vector<react_native_linux::SceneClip>& left,
                  const std::vector<react_native_linux::SceneClip>& right) {
    if (left.size() != right.size()) {
        return false;
    }

    for (size_t index = 0; index < left.size(); ++index) {
        if (left[index].frame.origin.x != right[index].frame.origin.x ||
            left[index].frame.origin.y != right[index].frame.origin.y ||
            left[index].frame.size.width != right[index].frame.size.width ||
            left[index].frame.size.height != right[index].frame.size.height ||
            left[index].borderRadii != right[index].borderRadii ||
            !sameMatrix(left[index].matrix, right[index].matrix)) {
            return false;
        }
    }

    return true;
}

/**
 * Snapshot equality in content, not in mount order: the test helper inserts children at index 0, so a list
 * built incrementally can paint its rows in the opposite order from a fresh build while holding identical
 * geometry - and the equivalence contract of issue #47 is about the pixels, not the mount history. Transform
 * and clip state are pixel state too, so the matrix and the inherited clip list are part of the comparison -
 * two primitives that match on frame and colour but carry a different matrix or clips paint differently.
 */
bool sameSnapshot(SceneSnapshot first, SceneSnapshot second) {
    const auto byTag = [](const ScenePrimitive& primitive) { return primitive.tag; };

    std::sort(first.begin(), first.end(),
              [&](const ScenePrimitive& a, const ScenePrimitive& b) { return byTag(a) < byTag(b); });
    std::sort(second.begin(), second.end(),
              [&](const ScenePrimitive& a, const ScenePrimitive& b) { return byTag(a) < byTag(b); });

    if (first.size() != second.size()) {
        return false;
    }

    for (size_t index = 0; index < first.size(); ++index) {
        const ScenePrimitive& left = first[index];
        const ScenePrimitive& right = second[index];

        if (left.tag != right.tag || left.frame.origin.x != right.frame.origin.x ||
            left.frame.origin.y != right.frame.origin.y || left.frame.size.width != right.frame.size.width ||
            left.frame.size.height != right.frame.size.height ||
            left.backgroundColorArgb != right.backgroundColorArgb || !sameMatrix(left.matrix, right.matrix) ||
            !sameClipList(left.clips, right.clips)) {
            return false;
        }
    }

    return true;
}

Rect boundsOf(const SceneDamage& damage) {
    Rect bounds = damage.front();

    for (const Rect& rect : damage) {
        bounds.unionInPlace(rect);
    }

    return bounds;
}

void expectMatrix(const react_native_linux::SceneMatrix& matrix, float scaleX, float scaleY, float translateX,
                  float translateY) {
    EXPECT_FLOAT_EQ(matrix.scaleX, scaleX);
    EXPECT_FLOAT_EQ(matrix.scaleY, scaleY);
    EXPECT_FLOAT_EQ(matrix.translateX, translateX);
    EXPECT_FLOAT_EQ(matrix.translateY, translateY);
    EXPECT_FLOAT_EQ(matrix.skewX, 0);
    EXPECT_FLOAT_EQ(matrix.skewY, 0);
}

SceneDamage damageAfterUpdatingPaintedChild(Rect newFrame) {
    RetainedScene scene = sceneWithPaintedChild();

    scene.takeDamage();
    scene.updateNode(makePaintedView(2, newFrame, red()));

    return scene.takeDamage();
}

RetainedScene sceneWithClippingParent(const std::shared_ptr<ViewProps>& clippingProps, Rect frame) {
    RetainedScene scene;

    clippingProps->yogaStyle.setOverflow(yoga::Overflow::Hidden);
    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(2, frame, clippingProps));

    return scene;
}

TEST(RetainedSceneTest, EmptySceneRendersAndDumpsNothing) {
    const RetainedScene scene;

    EXPECT_TRUE(scene.snapshot().empty());
    EXPECT_EQ(scene.dump(), "");
}

TEST(RetainedSceneTest, SurfaceRootCarriesItsSizeAndPaintsNothing) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});

    EXPECT_EQ(scene.dump(), "RootView #1 frame=(0.00, 0.00, 800.00, 600.00)\n");
    EXPECT_TRUE(scene.snapshot().empty());
}

TEST(RetainedSceneTest, NestedFramesComposeIntoAbsoluteOrigins) {
    RetainedScene scene = sceneWithPaintedChild();

    addChild(scene, 2, makePaintedView(3, makeRect(5, 7, 50, 40), red()));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 2U);
    expectPrimitive(snapshot[0], makeRect(10, 20, 200, 100), kBlueArgb);
    expectPrimitive(snapshot[1], makeRect(15, 27, 50, 40), kRedArgb);
}

TEST(RetainedSceneTest, DumpKeepsParentRelativeFramesAndIndentsByDepth) {
    RetainedScene scene = sceneWithPaintedChild();

    addChild(scene, 2, makeView(3, makeRect(5, 7, 50, 40)));

    EXPECT_EQ(scene.dump(), "RootView #1 frame=(0.00, 0.00, 800.00, 600.00)\n"
                            "  View #2 frame=(10.00, 20.00, 200.00, 100.00) backgroundColor=rgba(51, 102, 204, 1)\n"
                            "    View #3 frame=(5.00, 7.00, 50.00, 40.00)\n");
}

TEST(RetainedSceneTest, UpdateReplacesFrameAndBackgroundColorInPlace) {
    RetainedScene scene = sceneWithPaintedChild();

    scene.updateNode(makePaintedView(2, makeRect(1, 2, 30, 40), red()));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    expectPrimitive(snapshot[0], makeRect(1, 2, 30, 40), kRedArgb);
    EXPECT_NE(scene.dump().find("View #2 frame=(1.00, 2.00, 30.00, 40.00)"), std::string::npos);
}

TEST(RetainedSceneTest, UpdateCanDropABackgroundColor) {
    RetainedScene scene = sceneWithPaintedChild();

    scene.updateNode(makeView(2, makeRect(10, 20, 200, 100)));

    EXPECT_TRUE(scene.snapshot().empty());
    EXPECT_EQ(scene.dump().find("backgroundColor"), std::string::npos);
}

TEST(RetainedSceneTest, NodesWithoutAMeaningfulBackgroundColorAreNotPainted) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeView(2, makeRect(0, 0, 10, 10)));
    addChild(scene, kSurfaceTag, makePaintedView(3, makeRect(0, 0, 10, 10), invisibleBlue()));
    addChild(scene, kSurfaceTag, makePaintedView(4, makeRect(0, 0, 10, 10), blue()));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot[0].backgroundColorArgb, kBlueArgb);
}

TEST(RetainedSceneTest, RemoveDetachesTheChildAndLeavesItAsItsOwnRoot) {
    RetainedScene scene = sceneWithPaintedChild();

    scene.removeChild(kSurfaceTag, makePaintedView(2, makeRect(10, 20, 200, 100), blue()));

    EXPECT_EQ(scene.dump(), "RootView #1 frame=(0.00, 0.00, 800.00, 600.00)\n"
                            "View #2 frame=(10.00, 20.00, 200.00, 100.00) backgroundColor=rgba(51, 102, 204, 1)\n");
}

TEST(RetainedSceneTest, ReparentingRecomposesTheAbsoluteOrigin) {
    RetainedScene scene = sceneWithPaintedChild();
    const ShadowView secondParent = makeView(3, makeRect(100, 200, 400, 300));
    const ShadowView moved = makePaintedView(2, makeRect(10, 20, 200, 100), blue());

    scene.createNode(secondParent);
    scene.insertChild(kSurfaceTag, secondParent, 1);
    scene.removeChild(kSurfaceTag, moved);
    scene.insertChild(3, moved, 0);

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    expectPrimitive(snapshot[0], makeRect(110, 220, 200, 100), kBlueArgb);
}

TEST(RetainedSceneTest, InsertPlacesTheChildAtTheRequestedIndex) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 100, .height = 100});

    const ShadowView first = makePaintedView(2, makeRect(0, 0, 1, 1), blue());
    const ShadowView second = makePaintedView(3, makeRect(0, 0, 2, 2), red());
    const ShadowView third = makePaintedView(4, makeRect(0, 0, 3, 3), blue());

    scene.createNode(first);
    scene.insertChild(kSurfaceTag, first, 0);
    scene.createNode(second);
    scene.insertChild(kSurfaceTag, second, 1);
    scene.createNode(third);
    scene.insertChild(kSurfaceTag, third, 1);

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 3U);
    EXPECT_FLOAT_EQ(snapshot[0].frame.size.width, 1);
    EXPECT_FLOAT_EQ(snapshot[1].frame.size.width, 3);
    EXPECT_FLOAT_EQ(snapshot[2].frame.size.width, 2);
}

TEST(RetainedSceneTest, InsertClampsAnIndexOutsideTheChildRange) {
    RetainedScene scene = sceneWithPaintedChild();

    const ShadowView appended = makePaintedView(3, makeRect(0, 0, 3, 3), red());
    const ShadowView prepended = makePaintedView(4, makeRect(0, 0, 4, 4), red());

    scene.createNode(appended);
    scene.insertChild(kSurfaceTag, appended, 99);
    scene.createNode(prepended);
    scene.insertChild(kSurfaceTag, prepended, -5);

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 3U);
    EXPECT_FLOAT_EQ(snapshot[0].frame.size.width, 4);
    EXPECT_FLOAT_EQ(snapshot[1].frame.size.width, 200);
    EXPECT_FLOAT_EQ(snapshot[2].frame.size.width, 3);
}

TEST(RetainedSceneTest, ADeletedNodeStillReferencedByItsParentIsSkipped) {
    RetainedScene scene = sceneWithPaintedChild();

    scene.deleteNode(2);

    EXPECT_TRUE(scene.snapshot().empty());
    EXPECT_EQ(scene.dump(), "RootView #1 frame=(0.00, 0.00, 800.00, 600.00)\n");
}

TEST(RetainedSceneTest, InsertUnderAnUnknownParentLeavesTheChildUnreachable) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 100, .height = 100});

    const ShadowView orphan = makePaintedView(2, makeRect(0, 0, 5, 5), blue());

    scene.createNode(orphan);
    scene.insertChild(404, orphan, 0);

    EXPECT_TRUE(scene.snapshot().empty());
    EXPECT_EQ(scene.dump(), "RootView #1 frame=(0.00, 0.00, 100.00, 100.00)\n");
}

TEST(RetainedSceneTest, RemoveToleratesAnUnknownParentAndAnUnknownChild) {
    RetainedScene scene = sceneWithPaintedChild();

    scene.removeChild(404, makeView(2, makeRect(10, 20, 200, 100)));
    scene.removeChild(kSurfaceTag, makeView(505, makeRect(0, 0, 1, 1)));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    expectPrimitive(snapshot[0], makeRect(10, 20, 200, 100), kBlueArgb);
    EXPECT_EQ(scene.dump(), "RootView #1 frame=(0.00, 0.00, 800.00, 600.00)\n"
                            "View #2 frame=(10.00, 20.00, 200.00, 100.00) backgroundColor=rgba(51, 102, 204, 1)\n");
}

TEST(RetainedSceneTest, ANodeWithoutAComponentNameDumpsAnEmptyName) {
    RetainedScene scene;
    ShadowView nameless;

    nameless.tag = 7;
    scene.createNode(nameless);

    EXPECT_EQ(scene.dump(), " #7 frame=(0.00, 0.00, -1.00, -1.00)\n");
}

TEST(RetainedSceneTest, RootsAreOrderedByTagRegardlessOfCreationOrder) {
    RetainedScene scene;

    scene.createSurfaceRoot(3, Size{.width = 30, .height = 30});
    scene.createSurfaceRoot(2, Size{.width = 20, .height = 20});

    EXPECT_EQ(scene.dump(), "RootView #2 frame=(0.00, 0.00, 20.00, 20.00)\n"
                            "RootView #3 frame=(0.00, 0.00, 30.00, 30.00)\n");
}

TEST(RetainedSceneTest, ANodeWithoutATransformCarriesTheIdentityMatrixAndNoClips) {
    const RetainedScene scene = sceneWithPaintedChild();
    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    expectMatrix(snapshot[0].matrix, 1, 1, 0, 0);
    EXPECT_TRUE(snapshot[0].clips.empty());
}

TEST(RetainedSceneTest, OpacityMultipliesDownTheTree) {
    RetainedScene scene;
    const std::shared_ptr<ViewProps> parentProps = propsWithBackground(blue());
    const std::shared_ptr<ViewProps> childProps = propsWithBackground(red());

    parentProps->opacity = 0.5;
    childProps->opacity = 0.5;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(2, makeRect(0, 0, 100, 100), parentProps));
    addChild(scene, 2, makeStyledView(3, makeRect(0, 0, 50, 50), childProps));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_EQ(snapshot[0].backgroundColorArgb, kHalfBlueArgb);
    EXPECT_EQ(snapshot[1].backgroundColorArgb, kQuarterRedArgb);
}

TEST(RetainedSceneTest, AZeroOpacitySubtreePaintsNothing) {
    RetainedScene scene;
    const std::shared_ptr<ViewProps> parentProps = propsWithBackground(blue());

    parentProps->opacity = 0;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(2, makeRect(0, 0, 100, 100), parentProps));
    addChild(scene, 2, makePaintedView(3, makeRect(0, 0, 50, 50), red()));

    EXPECT_TRUE(scene.snapshot().empty());
}

TEST(RetainedSceneTest, BorderRadiiAreClampedSoAdjacentCornersDoNotOverlap) {
    const std::shared_ptr<ViewProps> viewProps = propsWithBackground(blue());

    viewProps->borderRadii.all = ValueUnit{200.0F, UnitType::Point};

    const SceneSnapshot snapshot = snapshotOfSingleChild(viewProps, makeRect(0, 0, 150, 60));

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_FLOAT_EQ(snapshot[0].borderRadii.topLeft.horizontal, 30);
    EXPECT_FLOAT_EQ(snapshot[0].borderRadii.topLeft.vertical, 30);
    EXPECT_FLOAT_EQ(snapshot[0].borderRadii.bottomRight.horizontal, 30);
    EXPECT_FLOAT_EQ(snapshot[0].borderRadii.bottomRight.vertical, 30);
}

TEST(RetainedSceneTest, PerCornerBorderRadiiOverrideTheShorthand) {
    const std::shared_ptr<ViewProps> viewProps = propsWithBackground(blue());

    viewProps->borderRadii.all = ValueUnit{8.0F, UnitType::Point};
    viewProps->borderRadii.topLeft = ValueUnit{20.0F, UnitType::Point};

    const SceneSnapshot snapshot = snapshotOfSingleChild(viewProps, makeRect(0, 0, 200, 200));

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_FLOAT_EQ(snapshot[0].borderRadii.topLeft.horizontal, 20);
    EXPECT_FLOAT_EQ(snapshot[0].borderRadii.topRight.horizontal, 8);
    EXPECT_FLOAT_EQ(snapshot[0].borderRadii.bottomLeft.vertical, 8);
    EXPECT_FLOAT_EQ(snapshot[0].borderRadii.bottomRight.vertical, 8);
}

TEST(RetainedSceneTest, BorderWidthsAndColorsAreReadPerSide) {
    const std::shared_ptr<ViewProps> viewProps = propsWithBackground(blue());

    viewProps->yogaStyle.setBorder(yoga::Edge::Left, yoga::StyleLength::points(1));
    viewProps->yogaStyle.setBorder(yoga::Edge::Top, yoga::StyleLength::points(2));
    viewProps->yogaStyle.setBorder(yoga::Edge::Right, yoga::StyleLength::points(3));
    viewProps->yogaStyle.setBorder(yoga::Edge::Bottom, yoga::StyleLength::points(4));
    viewProps->borderColors.left = red();
    viewProps->borderColors.top = blue();

    const SceneSnapshot snapshot = snapshotOfSingleChild(viewProps, makeRect(0, 0, 100, 100));

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_FLOAT_EQ(snapshot[0].borderWidths.left, 1);
    EXPECT_FLOAT_EQ(snapshot[0].borderWidths.top, 2);
    EXPECT_FLOAT_EQ(snapshot[0].borderWidths.right, 3);
    EXPECT_FLOAT_EQ(snapshot[0].borderWidths.bottom, 4);
    EXPECT_EQ(snapshot[0].borderColorsArgb.left, kRedArgb);
    EXPECT_EQ(snapshot[0].borderColorsArgb.top, kBlueArgb);
    EXPECT_EQ(snapshot[0].borderColorsArgb.right, 0U);
    EXPECT_EQ(snapshot[0].borderColorsArgb.bottom, 0U);
}

TEST(RetainedSceneTest, BorderOpacityFollowsTheInheritedOpacity) {
    const std::shared_ptr<ViewProps> viewProps = std::make_shared<ViewProps>();

    viewProps->opacity = 0.5;
    viewProps->yogaStyle.setBorder(yoga::Edge::All, yoga::StyleLength::points(2));
    viewProps->borderColors.all = blue();

    const SceneSnapshot snapshot = snapshotOfSingleChild(viewProps, makeRect(0, 0, 100, 100));

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot[0].backgroundColorArgb, 0U);
    EXPECT_EQ(snapshot[0].borderColorsArgb.left, kHalfBlueArgb);
}

TEST(RetainedSceneTest, EachBorderSideAloneIsEnoughToPaintANode) {
    RetainedScene scene;
    const std::shared_ptr<ViewProps> transparentBorder = std::make_shared<ViewProps>();

    transparentBorder->yogaStyle.setBorder(yoga::Edge::All, yoga::StyleLength::points(5));
    transparentBorder->borderColors.all = invisibleBlue();

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeView(2, makeRect(0, 0, 10, 10)));
    addChild(scene, kSurfaceTag, makeStyledView(3, makeRect(0, 0, 10, 10), transparentBorder));

    Tag nextTag = 4;

    for (const yoga::Edge edge : {yoga::Edge::Bottom, yoga::Edge::Right, yoga::Edge::Top, yoga::Edge::Left}) {
        const std::shared_ptr<ViewProps> viewProps = std::make_shared<ViewProps>();

        viewProps->yogaStyle.setBorder(edge, yoga::StyleLength::points(3));
        viewProps->borderColors.all = red();
        addChild(scene, kSurfaceTag, makeStyledView(nextTag, makeRect(0, 0, 10, 10), viewProps));
        nextTag++;
    }

    EXPECT_EQ(scene.snapshot().size(), 4U);
}

TEST(RetainedSceneTest, ABackgroundImageGradientTravelsToThePrimitiveWithTheInheritedOpacity) {
    const std::shared_ptr<ViewProps> viewProps = propsWithGradients({blueToRedGradient()});

    viewProps->opacity = 0.5;

    const SceneSnapshot snapshot = snapshotOfSingleChild(viewProps, makeRect(0, 0, 120, 80));

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_EQ(snapshot[0].backgroundImage.size(), 1U);
    EXPECT_TRUE(snapshot[0].backgroundImage.front() == facebook::react::BackgroundImage{blueToRedGradient()});
    EXPECT_FLOAT_EQ(snapshot[0].backgroundImageOpacity, 0.5F);
    EXPECT_EQ(snapshot[0].backgroundColorArgb, 0U);
}

TEST(RetainedSceneTest, ANodeWithoutABackgroundImageCarriesNoGradientLayers) {
    const SceneSnapshot snapshot = snapshotOfSingleChild(propsWithBackground(blue()), makeRect(0, 0, 40, 40));

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_TRUE(snapshot[0].backgroundImage.empty());
    EXPECT_FLOAT_EQ(snapshot[0].backgroundImageOpacity, 1.0F);
}

TEST(RetainedSceneTest, AGradientAloneIsEnoughToPaintAndDamageANodeButAnEmptyListIsNot) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    scene.takeDamage();
    addChild(scene, kSurfaceTag, makeStyledView(2, makeRect(0, 0, 40, 40), propsWithGradients({})));
    addChild(scene, kSurfaceTag,
             makeStyledView(3, makeRect(10, 20, 200, 100), propsWithGradients({blueToRedGradient()})));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot[0].backgroundImage.size(), 1U);
    expectRect(boundsOf(scene.takeDamage()), makeRect(10, 20, 200, 100));
}

TEST(RetainedSceneTest, ATransformIsAppliedAboutTheCenterOfTheAbsoluteFrame) {
    const std::shared_ptr<ViewProps> viewProps = propsWithBackground(blue());

    viewProps->transform = Transform::Scale(2, 2, 1);

    const SceneSnapshot snapshot = snapshotOfSingleChild(viewProps, makeRect(100, 50, 200, 100));

    ASSERT_EQ(snapshot.size(), 1U);
    expectMatrix(snapshot[0].matrix, 2, 2, -200, -100);
    expectPrimitive(snapshot[0], makeRect(100, 50, 200, 100), kBlueArgb);
}

TEST(RetainedSceneTest, TransformsComposeFromAncestorToDescendant) {
    RetainedScene scene;
    const std::shared_ptr<ViewProps> parentProps = propsWithBackground(blue());
    const std::shared_ptr<ViewProps> childProps = propsWithBackground(red());

    parentProps->transform = Transform::Translate(30, 40, 0);
    childProps->transform = Transform::Scale(2, 2, 1);

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(2, makeRect(10, 20, 200, 100), parentProps));
    addChild(scene, 2, makeStyledView(3, makeRect(5, 5, 50, 50), childProps));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 2U);
    expectMatrix(snapshot[0].matrix, 1, 1, 30, 40);
    expectMatrix(snapshot[1].matrix, 2, 2, -10, -10);
}

TEST(RetainedSceneTest, OverflowHiddenClipsDescendantsToTheRoundedBorderBox) {
    RetainedScene scene;
    const std::shared_ptr<ViewProps> clippingProps = propsWithBackground(blue());

    clippingProps->yogaStyle.setOverflow(yoga::Overflow::Hidden);
    clippingProps->borderRadii.all = ValueUnit{16.0F, UnitType::Point};

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(2, makeRect(20, 30, 200, 150), clippingProps));
    addChild(scene, 2, makePaintedView(3, makeRect(10, 10, 400, 400), red()));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_TRUE(snapshot[0].clips.empty());
    ASSERT_EQ(snapshot[1].clips.size(), 1U);
    EXPECT_FLOAT_EQ(snapshot[1].clips[0].frame.origin.x, 20);
    EXPECT_FLOAT_EQ(snapshot[1].clips[0].frame.origin.y, 30);
    EXPECT_FLOAT_EQ(snapshot[1].clips[0].frame.size.width, 200);
    EXPECT_FLOAT_EQ(snapshot[1].clips[0].frame.size.height, 150);
    EXPECT_FLOAT_EQ(snapshot[1].clips[0].borderRadii.topLeft.horizontal, 16);
    expectMatrix(snapshot[1].clips[0].matrix, 1, 1, 0, 0);
}

TEST(RetainedSceneTest, AClipCarriesTheTransformOfTheClippingAncestor) {
    RetainedScene scene;
    const std::shared_ptr<ViewProps> clippingProps = std::make_shared<ViewProps>();

    clippingProps->yogaStyle.setOverflow(yoga::Overflow::Hidden);
    clippingProps->transform = Transform::Scale(2, 2, 1);

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(2, makeRect(0, 0, 100, 100), clippingProps));
    addChild(scene, 2, makePaintedView(3, makeRect(10, 10, 10, 10), red()));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_EQ(snapshot[0].clips.size(), 1U);
    expectMatrix(snapshot[0].clips[0].matrix, 2, 2, -50, -50);
    expectMatrix(snapshot[0].matrix, 2, 2, -50, -50);
}

TEST(RetainedSceneDamageTest, ANewSurfaceRootDamagesTheWholeSurface) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});

    const SceneDamage damage = scene.takeDamage();

    ASSERT_EQ(damage.size(), 1U);
    expectRect(damage[0], makeRect(0, 0, 800, 600));
}

TEST(RetainedSceneDamageTest, TakingTheDamageClearsIt) {
    RetainedScene scene = sceneWithPaintedChild();

    EXPECT_FALSE(scene.takeDamage().empty());
    EXPECT_TRUE(scene.takeDamage().empty());
}

TEST(RetainedSceneDamageTest, CreatingANodeDamagesWhereItStands) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    scene.takeDamage();
    scene.createNode(makePaintedView(2, makeRect(10, 20, 30, 40), blue()));

    const SceneDamage damage = scene.takeDamage();

    ASSERT_EQ(damage.size(), 1U);
    expectRect(damage[0], makeRect(10, 20, 30, 40));
}

TEST(RetainedSceneDamageTest, InsertingAChildDamagesTheOldAndTheNewPosition) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makePaintedView(2, makeRect(100, 200, 400, 300), blue()));

    const ShadowView child = makePaintedView(3, makeRect(10, 20, 50, 60), red());

    scene.createNode(child);
    scene.takeDamage();
    scene.insertChild(2, child, 0);

    const SceneDamage damage = scene.takeDamage();

    ASSERT_EQ(damage.size(), 2U);
    expectRect(damage[0], makeRect(10, 20, 50, 60));
    expectRect(damage[1], makeRect(110, 220, 50, 60));
}

TEST(RetainedSceneDamageTest, MovingANodeDamagesTheOldAndTheNewPosition) {
    const SceneDamage damage = damageAfterUpdatingPaintedChild(makeRect(300, 400, 50, 60));

    ASSERT_EQ(damage.size(), 2U);
    expectRect(damage[0], makeRect(10, 20, 200, 100));
    expectRect(damage[1], makeRect(300, 400, 50, 60));
}

TEST(RetainedSceneDamageTest, AColorOnlyChangeDamagesThePrimitiveBoundsTwice) {
    const SceneDamage damage = damageAfterUpdatingPaintedChild(makeRect(10, 20, 200, 100));

    ASSERT_EQ(damage.size(), 2U);
    expectRect(damage[0], makeRect(10, 20, 200, 100));
    expectRect(damage[1], makeRect(10, 20, 200, 100));
}

TEST(RetainedSceneDamageTest, RemovingAChildDamagesBothPlacesItOccupies) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeView(2, makeRect(100, 200, 400, 300)));

    const ShadowView child = makePaintedView(3, makeRect(10, 20, 50, 60), red());

    addChild(scene, 2, child);
    scene.takeDamage();
    scene.removeChild(2, child);

    const SceneDamage removal = scene.takeDamage();

    ASSERT_EQ(removal.size(), 2U);
    expectRect(removal[0], makeRect(110, 220, 50, 60));
    expectRect(removal[1], makeRect(10, 20, 50, 60));

    scene.deleteNode(3);

    const SceneDamage deletion = scene.takeDamage();

    ASSERT_EQ(deletion.size(), 1U);
    expectRect(deletion[0], makeRect(10, 20, 50, 60));
}

TEST(RetainedSceneDamageTest, DeletingAnUnknownNodeDamagesNothing) {
    RetainedScene scene = sceneWithPaintedChild();

    scene.takeDamage();
    scene.deleteNode(404);

    EXPECT_TRUE(scene.takeDamage().empty());
}

TEST(RetainedSceneDamageTest, UpdatingANodeThatPaintsNothingDamagesNothing) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeView(2, makeRect(0, 0, 50, 50)));
    scene.takeDamage();
    scene.updateNode(makeView(2, makeRect(10, 10, 50, 50)));

    EXPECT_TRUE(scene.takeDamage().empty());
}

TEST(RetainedSceneDamageTest, ATransformIsMappedIntoTheDamageBounds) {
    RetainedScene scene;
    const std::shared_ptr<ViewProps> viewProps = propsWithBackground(blue());

    viewProps->transform = Transform::Scale(2, 2, 1);

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    scene.takeDamage();
    addChild(scene, kSurfaceTag, makeStyledView(2, makeRect(100, 50, 200, 100), viewProps));

    const SceneDamage damage = scene.takeDamage();

    ASSERT_FALSE(damage.empty());
    expectRect(boundsOf(damage), makeRect(0, 0, 400, 200));
}

TEST(RetainedSceneDamageTest, AnAncestorClipCutsTheDamageBounds) {
    RetainedScene scene = sceneWithClippingParent(propsWithBackground(blue()), makeRect(25, 35, 200, 150));

    scene.takeDamage();
    addChild(scene, 2, makePaintedView(3, makeRect(10, 10, 400, 400), red()));

    const SceneDamage damage = scene.takeDamage();

    ASSERT_FALSE(damage.empty());
    expectRect(damage.back(), makeRect(35, 45, 190, 140));
}

TEST(RetainedSceneDamageTest, APrimitiveEntirelyOutsideItsClipDamagesNothing) {
    RetainedScene scene = sceneWithClippingParent(std::make_shared<ViewProps>(), makeRect(0, 0, 100, 100));

    const ShadowView hidden = makePaintedView(3, makeRect(200, 200, 50, 50), red());

    addChild(scene, 2, hidden);
    scene.takeDamage();
    scene.updateNode(hidden);

    EXPECT_TRUE(scene.takeDamage().empty());
}

TEST(RetainedSceneDamageTest, AParentUpdateDamagesItsWholeSubtree) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeView(2, makeRect(0, 0, 300, 300)));
    addChild(scene, 2, makePaintedView(3, makeRect(10, 10, 50, 50), blue()));
    addChild(scene, 2, makePaintedView(4, makeRect(100, 100, 50, 50), red()));
    scene.takeDamage();
    scene.updateNode(makeView(2, makeRect(200, 0, 300, 300)));

    const SceneDamage damage = scene.takeDamage();

    ASSERT_EQ(damage.size(), 2U);
    expectRect(damage[0], makeRect(10, 10, 140, 140));
    expectRect(damage[1], makeRect(210, 10, 140, 140));
}

TEST(RetainedSceneDamageTest, ANodeUnderAMissingParentIsMeasuredFromTheRoot) {
    RetainedScene scene;
    const ShadowView orphan = makePaintedView(2, makeRect(5, 6, 7, 8), blue());

    scene.createNode(orphan);
    scene.takeDamage();
    scene.insertChild(404, orphan, 0);

    const SceneDamage damage = scene.takeDamage();

    ASSERT_EQ(damage.size(), 2U);
    expectRect(damage[1], makeRect(5, 6, 7, 8));
}

TEST(RetainedSceneDamageTest, TheNinthRectangleCollapsesTheListIntoItsBoundingRectangle) {
    RetainedScene scene = sceneWithPaintedChild();

    scene.takeDamage();

    for (int step = 0; step < 5; ++step) {
        scene.updateNode(makePaintedView(2, makeRect(static_cast<float>(step) * 100, 0, 10, 10), blue()));
    }

    const SceneDamage damage = scene.takeDamage();

    ASSERT_EQ(damage.size(), 2U);
    expectRect(damage[0], makeRect(0, 0, 310, 120));
    expectRect(damage[1], makeRect(400, 0, 10, 10));
}

TEST(RetainedSceneDamageTest, MergingDamageFollowsTheSameCapPolicy) {
    SceneDamage damage;
    SceneDamage additions;

    for (int step = 0; step < 5; ++step) {
        additions.push_back(makeRect(static_cast<float>(step) * 10, 0, 5, 5));
    }

    react_native_linux::mergeDamage(damage, additions);

    EXPECT_EQ(damage.size(), 5U);

    react_native_linux::mergeDamage(damage, additions);

    ASSERT_EQ(damage.size(), 2U);
    expectRect(damage[0], makeRect(0, 0, 45, 5));
    expectRect(damage[1], makeRect(40, 0, 5, 5));
}

SceneSnapshot snapshotOfParagraph(const ShadowView& paragraphView) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, paragraphView);

    return scene.snapshot();
}

TEST(RetainedSceneTextTest, ParagraphStateBecomesTheTextOnTheNode) {
    const SceneSnapshot snapshot = snapshotOfParagraph(makeParagraph(2, makeRect(40, 60, 300, 48), "Hello Linux", 2));

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].text.has_value());
    EXPECT_EQ(snapshot[0].text.value().attributedString.getString(), "Hello Linux");
    EXPECT_EQ(snapshot[0].text.value().paragraphAttributes.maximumNumberOfLines, 2);
    expectRect(snapshot[0].text.value().frame, makeRect(40, 60, 300, 48));
    expectPrimitive(snapshot[0], makeRect(40, 60, 300, 48), 0);
}

TEST(RetainedSceneTextTest, TextIsLaidOutInTheContentBoxRatherThanTheFrame) {
    ShadowView paragraphView = makeParagraph(2, makeRect(40, 60, 300, 48), "inset", 0);

    paragraphView.layoutMetrics.contentInsets =
        facebook::react::EdgeInsets{.left = 10, .top = 4, .right = 6, .bottom = 2};

    const SceneSnapshot snapshot = snapshotOfParagraph(paragraphView);

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].text.has_value());
    expectRect(snapshot[0].text.value().frame, makeRect(50, 64, 284, 42));
    expectRect(snapshot[0].frame, makeRect(40, 60, 300, 48));
}

TEST(RetainedSceneTextTest, TextIsPaintedWithoutABackgroundColorOrABorder) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeView(2, makeRect(0, 0, 100, 20)));
    addChild(scene, kSurfaceTag, makeParagraph(3, makeRect(0, 40, 100, 20), "painted", 0));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    expectRect(snapshot[0].frame, makeRect(0, 40, 100, 20));
}

TEST(RetainedSceneTextTest, ANodeWithoutParagraphStateCarriesNoText) {
    RetainedScene scene = sceneWithPaintedChild();
    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_FALSE(snapshot[0].text.has_value());
}

TEST(RetainedSceneTextTest, AnEmptyAttributedStringPaintsNothing) {
    EXPECT_TRUE(snapshotOfParagraph(makeParagraph(2, makeRect(40, 60, 300, 48), "", 0)).empty());
}

TEST(RetainedSceneTextTest, UpdateReplacesTheAttributedStringInPlace) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeParagraph(2, makeRect(40, 60, 300, 48), "before", 0));
    scene.updateNode(makeParagraph(2, makeRect(40, 60, 300, 48), "after", 0));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].text.has_value());
    EXPECT_EQ(snapshot[0].text.value().attributedString.getString(), "after");
}

/**
 * A half-opaque parent with nothing of its own to paint, so the only primitive a snapshot produces is the child's
 * and every colour on it carries the inherited opacity.
 */
RetainedScene sceneWithTranslucentParent() {
    const std::shared_ptr<ViewProps> translucent = std::make_shared<ViewProps>();

    translucent->opacity = 0.5;

    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(2, makeRect(0, 0, 400, 200), translucent));

    return scene;
}

TEST(RetainedSceneTextTest, OpacityMultipliesIntoTheFragmentColors) {
    RetainedScene scene = sceneWithTranslucentParent();

    addChild(scene, 2, makeParagraph(3, makeRect(0, 0, 400, 40), "faded", 0));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].text.has_value());

    const facebook::react::AttributedString::Fragment& fragment =
        snapshot[0].text.value().attributedString.getFragments().front();

    EXPECT_EQ(facebook::react::alphaFromColor(fragment.textAttributes.foregroundColor), 128U);
    EXPECT_EQ(facebook::react::redFromColor(fragment.textAttributes.foregroundColor), 0U);
    EXPECT_FALSE(facebook::react::isColorMeaningful(fragment.textAttributes.backgroundColor));
}

TEST(RetainedSceneTextTest, AParagraphDamagesItsOwnFrame) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    scene.takeDamage();
    addChild(scene, kSurfaceTag, makeParagraph(2, makeRect(40, 60, 300, 48), "damaging", 0));

    const SceneDamage damage = scene.takeDamage();

    ASSERT_FALSE(damage.empty());
    expectRect(boundsOf(damage), makeRect(40, 60, 300, 48));
}

TEST(RetainedSceneTextTest, DumpCarriesTheParagraphText) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeParagraph(2, makeRect(40, 60, 300, 48), "dumped", 0));

    EXPECT_EQ(scene.dump(), "RootView #1 frame=(0.00, 0.00, 800.00, 600.00)\n"
                            "  Paragraph #2 frame=(40.00, 60.00, 300.00, 48.00) text=\"dumped\"\n");
}

ShadowView makeTile(Tag tag, Rect frame, const std::string& uri) {
    return makeImage(tag, frame, uri, facebook::react::ImageResizeMode::Cover, SharedColor{});
}

RetainedScene sceneWithTile(const ShadowView& imageView) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, imageView);

    return scene;
}

react_native_linux::SceneImageResizeMode snapshotResizeMode(facebook::react::ImageResizeMode resizeMode) {
    return sceneWithTile(makeImage(2, makeRect(0, 0, 64, 48), "tile.png", resizeMode, SharedColor{}))
        .snapshot()
        .front()
        .image.value()
        .resizeMode;
}

TEST(RetainedSceneImageTest, ImageStateBecomesTheImageOnTheNode) {
    const SceneSnapshot snapshot = sceneWithTile(makeTile(2, makeRect(40, 60, 120, 90), "tile.png")).snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].image.has_value());
    EXPECT_EQ(snapshot[0].image.value().uri, "tile.png");
    EXPECT_EQ(snapshot[0].image.value().tintColorArgb, 0U);
    EXPECT_FLOAT_EQ(snapshot[0].image.value().opacity, 1.0F);
    expectPrimitive(snapshot[0], makeRect(40, 60, 120, 90), 0);
}

TEST(RetainedSceneImageTest, EveryResizeModeMapsOntoASceneResizeMode) {
    EXPECT_EQ(snapshotResizeMode(facebook::react::ImageResizeMode::Cover),
              react_native_linux::SceneImageResizeMode::Cover);
    EXPECT_EQ(snapshotResizeMode(facebook::react::ImageResizeMode::Contain),
              react_native_linux::SceneImageResizeMode::Contain);
    EXPECT_EQ(snapshotResizeMode(facebook::react::ImageResizeMode::Stretch),
              react_native_linux::SceneImageResizeMode::Stretch);
    EXPECT_EQ(snapshotResizeMode(facebook::react::ImageResizeMode::Repeat),
              react_native_linux::SceneImageResizeMode::Repeat);
    EXPECT_EQ(snapshotResizeMode(facebook::react::ImageResizeMode::Center),
              react_native_linux::SceneImageResizeMode::Center);
    EXPECT_EQ(snapshotResizeMode(facebook::react::ImageResizeMode::None),
              react_native_linux::SceneImageResizeMode::Center);
}

TEST(RetainedSceneImageTest, AnImageAloneIsEnoughToPaintANode) {
    EXPECT_FALSE(sceneWithTile(makeTile(2, makeRect(0, 0, 64, 48), "tile.png")).snapshot().empty());
}

TEST(RetainedSceneImageTest, ANodeWithoutImagePropsCarriesNoImage) {
    EXPECT_FALSE(sceneWithPaintedChild().snapshot().front().image.has_value());
}

TEST(RetainedSceneImageTest, ANodeWithImagePropsButNoStateCarriesNoImage) {
    ShadowView statelessImage = makeTile(2, makeRect(0, 0, 64, 48), "tile.png");

    statelessImage.state = nullptr;

    EXPECT_TRUE(sceneWithTile(statelessImage).snapshot().empty());
}

TEST(RetainedSceneImageTest, AnUnrequestedSourcePaintsNothing) {
    EXPECT_TRUE(sceneWithTile(makeTile(2, makeRect(0, 0, 64, 48), "")).snapshot().empty());
}

TEST(RetainedSceneImageTest, OpacityMultipliesIntoTheTintAlphaAndTheImageAlpha) {
    RetainedScene scene = sceneWithTranslucentParent();

    addChild(scene, 2,
             makeImage(3, makeRect(0, 0, 64, 48), "tile.png", facebook::react::ImageResizeMode::Cover, red()));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].image.has_value());
    EXPECT_EQ(snapshot[0].image.value().tintColorArgb, kHalfRedArgb);
    EXPECT_FLOAT_EQ(snapshot[0].image.value().opacity, 0.5F);
}

TEST(RetainedSceneImageTest, ADecodedSourceDamagesEveryNodeDrawingIt) {
    RetainedScene scene = sceneWithTile(makeTile(2, makeRect(40, 60, 120, 90), "tile.png"));

    addChild(scene, kSurfaceTag, makeTile(3, makeRect(400, 300, 100, 100), "other.png"));
    scene.takeDamage();
    scene.damageImageSource("tile.png");

    const SceneDamage damage = scene.takeDamage();

    ASSERT_FALSE(damage.empty());
    expectRect(boundsOf(damage), makeRect(40, 60, 120, 90));
}

TEST(RetainedSceneImageTest, ASourceNothingDrawsDamagesNothing) {
    RetainedScene scene = sceneWithTile(makeTile(2, makeRect(40, 60, 120, 90), "tile.png"));

    scene.takeDamage();
    scene.damageImageSource("missing.png");

    EXPECT_TRUE(scene.takeDamage().empty());
}

TEST(RetainedSceneImageTest, DumpCarriesTheImageSource) {
    EXPECT_EQ(sceneWithTile(makeTile(2, makeRect(40, 60, 120, 90), "tile.png")).dump(),
              "RootView #1 frame=(0.00, 0.00, 800.00, 600.00)\n"
              "  Image #2 frame=(40.00, 60.00, 120.00, 90.00) image=\"tile.png\"\n");
}

// The fixture every scroll test below shares: a 200x150 viewport at (60, 60) holding 470 points of content, one
// 200x70 row of which sits 80 points down.
Rect scrollViewFrame() { return makeRect(60, 60, 200, 150); }

Rect scrollContentBounds() { return makeRect(0, 0, 200, 470); }

RetainedScene sceneWithScrollView(Point contentOffset) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeScrollView(2, scrollViewFrame(), contentOffset, scrollContentBounds()));
    addChild(scene, 2, makePaintedView(3, makeRect(0, 80, 200, 70), blue()));

    return scene;
}

// Issue #98. #16 owns the offset and #35 proved static hit/paint agreement; this is the intersection — a subtree
// translated by a scroll, or laid out outside its parent — where upstream has two open, heavily-upvoted
// regressions (core#51763, core#51290) and the cause is always the hit path reading the untranslated frame.
// The scene applies `contentOffset` in `visitNode`, which both the painter and the hit test walk, so these are
// the assertions that it stays that way.

// At an offset of 120 the row laid out 80 points down the content paints from y = 20 of the viewport, clipped to
// the viewport's own top edge at 60, so its visible band is 60..90 and its unscrolled position, 140..210, is
// nothing but ScrollView.
constexpr Point kScrolledRowPoint{.x = 160, .y = 75};
constexpr Point kUnscrolledRowPoint{.x = 160, .y = 175};

TEST(RetainedSceneScrollTest, APressLandsOnTheRowWhereItIsPaintedAndNotWhereItWasLaidOut) {
    const RetainedScene scene = sceneWithScrollView(Point{.x = 0, .y = 120});

    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, kScrolledRowPoint).tag, 3);
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, kUnscrolledRowPoint).tag, 2);
}

TEST(RetainedSceneScrollTest, ThePressFollowsTheOffsetTheSceneHoldsThisCommit) {
    RetainedScene scene = sceneWithScrollView(Point{.x = 0, .y = 120});

    ASSERT_EQ(scene.findNodeAtPoint(kSurfaceTag, kScrolledRowPoint).tag, 3);

    // The controller writes a new offset back every frame it moves; the frame's hit test reads what it wrote.
    scene.updateNode(makeScrollView(2, scrollViewFrame(), Point{.x = 0, .y = 0}, scrollContentBounds()));

    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, kScrolledRowPoint).tag, 2);
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, kUnscrolledRowPoint).tag, 3);
}

TEST(RetainedSceneScrollTest, AHorizontalScrollerInsideAVerticalOneComposesBothOffsets) {
    RetainedScene scene = sceneWithScrollView(Point{.x = 0, .y = 120});

    // A 200x40 horizontal scroller 170 points down the content, scrolled 50 to the right, holding a 60-wide cell
    // 100 points in. Composed: y = 60 + 170 - 120 = 110, x = 60 + 100 - 50 = 110.
    addChild(scene, 2, makeScrollView(4, makeRect(0, 170, 200, 40), Point{.x = 50, .y = 0}, makeRect(0, 0, 400, 40)));
    addChild(scene, 4, makePaintedView(5, makeRect(100, 0, 60, 40), red()));

    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, Point{.x = 140, .y = 130}).tag, 5);
    // Where the cell would be without the horizontal offset is the scroller itself.
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, Point{.x = 190, .y = 130}).tag, 4);
}

std::shared_ptr<ViewProps> overflowProps(facebook::yoga::Overflow overflow) {
    const std::shared_ptr<ViewProps> viewProps = propsWithBackground(blue());

    viewProps->yogaStyle.setOverflow(overflow);

    return viewProps;
}

RetainedScene sceneWithOutOfFlowChild(facebook::yoga::Overflow parentOverflow) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(2, makeRect(300, 300, 100, 100), overflowProps(parentOverflow)));
    // Laid out entirely to the right of its parent's bounds: (450, 300) to (500, 350).
    addChild(scene, 2, makePaintedView(3, makeRect(150, 0, 50, 50), red()));

    return scene;
}

TEST(RetainedSceneScrollTest, AChildPaintedOutsideItsParentIsPressableThereUnlessTheParentClips) {
    constexpr Point kOutsideTheParent{.x = 475, .y = 325};

    // core#34542 and core#37181: an absolutely positioned child outside its parent's bounds receives no gestures,
    // because the parent's bounds were used as a hit gate although the child painted past them.
    EXPECT_EQ(sceneWithOutOfFlowChild(facebook::yoga::Overflow::Visible).findNodeAtPoint(kSurfaceTag, kOutsideTheParent).tag, 3);
    EXPECT_EQ(sceneWithOutOfFlowChild(facebook::yoga::Overflow::Hidden).findNodeAtPoint(kSurfaceTag, kOutsideTheParent).tag,
              kSurfaceTag);
}

TEST(RetainedSceneScrollTest, ContentOffsetTranslatesTheChildrenOnBothAxes) {
    const SceneSnapshot snapshot = sceneWithScrollView(Point{.x = 25, .y = 120}).snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    expectPrimitive(snapshot[0], makeRect(35, 20, 200, 70), kBlueArgb);
}

TEST(RetainedSceneScrollTest, AScrollViewClipsItsChildrenWithNoOverflowPropInvolved) {
    const SceneSnapshot snapshot = sceneWithScrollView(Point{.x = 0, .y = 120}).snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_EQ(snapshot[0].clips.size(), 1U);
    expectRect(snapshot[0].clips.front().frame, scrollViewFrame());
}

TEST(RetainedSceneScrollTest, ANodeThatIsNotAScrollViewTranslatesAndClipsNothing) {
    const SceneSnapshot snapshot = sceneWithPaintedChild().snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_TRUE(snapshot[0].clips.empty());
    expectPrimitive(snapshot[0], makeRect(10, 20, 200, 100), kBlueArgb);
}

TEST(RetainedSceneScrollTest, AnOffsetChangeDamagesExactlyTheScrollViewFrame) {
    RetainedScene scene = sceneWithScrollView(Point{});

    scene.takeDamage();
    scene.updateNode(makeScrollView(2, scrollViewFrame(), Point{.x = 0, .y = 120}, scrollContentBounds()));

    const SceneDamage damage = scene.takeDamage();

    ASSERT_FALSE(damage.empty());
    expectRect(boundsOf(damage), scrollViewFrame());
}

TEST(RetainedSceneScrollTest, DumpCarriesTheContentOffset) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeScrollView(2, scrollViewFrame(), Point{.x = 0, .y = 120}, scrollContentBounds()));

    EXPECT_EQ(scene.dump(), "RootView #1 frame=(0.00, 0.00, 800.00, 600.00)\n"
                            "  ScrollView #2 frame=(60.00, 60.00, 200.00, 150.00) contentOffset=(0.00, 120.00)\n");
}

ShadowView mountBlueChild(LinuxMountingManager& mountingManager) {
    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});

    const ShadowView child = makePaintedView(2, makeRect(24, 24, 120, 80), blue());
    ShadowViewMutationList mutations;

    mutations.push_back(ShadowViewMutation::CreateMutation(child));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceTag, child, 0));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));

    return child;
}

TEST(RetainedSceneFocusTest, AKeyboardFocusMarksThePrimitiveAndAPointerFocusDoesNot) {
    RetainedScene scene = sceneWithPaintedChild();

    scene.setFocus(2, true);

    const SceneSnapshot focused = scene.snapshot();

    ASSERT_EQ(focused.size(), 1U);
    EXPECT_TRUE(focused[0].focusRing);

    scene.setFocus(2, false);

    const SceneSnapshot clicked = scene.snapshot();

    ASSERT_EQ(clicked.size(), 1U);
    EXPECT_FALSE(clicked[0].focusRing);
}

TEST(RetainedSceneFocusTest, AFocusChangeDamagesExactlyTheFocusedFrame) {
    RetainedScene scene = sceneWithPaintedChild();

    scene.takeDamage();
    scene.setFocus(2, true);

    const SceneDamage damage = scene.takeDamage();

    ASSERT_FALSE(damage.empty());
    expectRect(boundsOf(damage), makeRect(10, 20, 200, 100));
}

TEST(RetainedSceneFocusTest, MovingFocusDamagesBothTheOldAndTheNewNode) {
    RetainedScene scene = sceneWithPaintedChild();

    addChild(scene, kSurfaceTag, makePaintedView(3, makeRect(400, 300, 100, 50), red()));
    scene.setFocus(2, true);
    scene.takeDamage();

    scene.setFocus(3, true);

    const SceneSnapshot moved = scene.snapshot();

    ASSERT_EQ(moved.size(), 2U);
    EXPECT_TRUE(moved[0].focusRing);
    EXPECT_FALSE(moved[1].focusRing);

    const SceneDamage damage = scene.takeDamage();

    ASSERT_FALSE(damage.empty());
    expectRect(boundsOf(damage), makeRect(10, 20, 490, 330));
}

TEST(RetainedSceneFocusTest, ReMarkingTheSameFocusDamagesNothing) {
    RetainedScene scene = sceneWithPaintedChild();

    scene.setFocus(2, true);
    scene.takeDamage();

    scene.setFocus(2, true);

    EXPECT_TRUE(scene.takeDamage().empty());
}

// A <Pressable> with no background of its own paints nothing until it is focused, and a ring that only appears on
// nodes the scene already had a reason to paint would be missing on exactly the controls that need it most.
TEST(RetainedSceneFocusTest, ANodeThatPaintsNothingElseIsPaintedForItsRingAlone) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeView(2, makeRect(10, 20, 200, 100)));

    EXPECT_TRUE(scene.snapshot().empty());

    scene.takeDamage();
    scene.setFocus(2, true);

    const SceneSnapshot focused = scene.snapshot();

    ASSERT_EQ(focused.size(), 1U);
    EXPECT_TRUE(focused[0].focusRing);
    expectRect(boundsOf(scene.takeDamage()), makeRect(10, 20, 200, 100));
}

TEST(RetainedSceneFocusTest, BlurringAnUnpaintedNodeStillDamagesTheRingItDrew) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeView(2, makeRect(10, 20, 200, 100)));
    scene.setFocus(2, true);
    scene.takeDamage();

    scene.setFocus(0, false);

    const SceneDamage damage = scene.takeDamage();

    ASSERT_FALSE(damage.empty());
    expectRect(boundsOf(damage), makeRect(10, 20, 200, 100));
    EXPECT_TRUE(scene.snapshot().empty());
}

TEST(LinuxMountingManagerTest, FocusIsMarkedUnderTheSceneMutex) {
    LinuxMountingManager mountingManager;

    mountBlueChild(mountingManager);
    mountingManager.takeFrame();

    mountingManager.setFocus(2, true);

    const SceneFrame frame = mountingManager.takeFrame();

    ASSERT_EQ(frame.scene.size(), 1U);
    EXPECT_TRUE(frame.scene[0].focusRing);
    ASSERT_FALSE(frame.damage.empty());
    expectRect(boundsOf(frame.damage), makeRect(24, 24, 120, 80));
}

TEST(LinuxMountingManagerTest, EditorStateIsAppliedUnderTheSceneMutex) {
    LinuxMountingManager mountingManager;

    mountBlueChild(mountingManager);
    mountingManager.takeFrame();

    mountingManager.setEditorState(2, SceneEditorState{.caretUtf16 = 1, .isCaretVisible = true});

    const SceneFrame frame = mountingManager.takeFrame();

    ASSERT_EQ(frame.scene.size(), 1U);
    ASSERT_FALSE(frame.damage.empty());
    expectRect(boundsOf(frame.damage), makeRect(24, 24, 120, 80));
}

TEST(LinuxMountingManagerTest, StartSurfaceCreatesTheRootTheDifferNeverEmits) {
    LinuxMountingManager mountingManager;

    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});

    EXPECT_EQ(mountingManager.dumpScene(), "RootView #1 frame=(0.00, 0.00, 800.00, 600.00)\n");
    EXPECT_TRUE(mountingManager.snapshotScene().empty());
}

TEST(LinuxMountingManagerTest, CreateAndInsertMutationsReachTheScene) {
    LinuxMountingManager mountingManager;

    mountBlueChild(mountingManager);

    const SceneSnapshot snapshot = mountingManager.snapshotScene();

    ASSERT_EQ(snapshot.size(), 1U);
    expectPrimitive(snapshot[0], makeRect(24, 24, 120, 80), kBlueArgb);
}

TEST(LinuxMountingManagerTest, UpdateRemoveAndDeleteMutationsReachTheScene) {
    LinuxMountingManager mountingManager;

    const ShadowView child = mountBlueChild(mountingManager);
    const ShadowView moved = makePaintedView(2, makeRect(8, 8, 120, 80), red());
    ShadowViewMutationList update;

    update.push_back(ShadowViewMutation::UpdateMutation(child, moved, kSurfaceTag));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(update)));

    const SceneSnapshot updated = mountingManager.snapshotScene();

    ASSERT_EQ(updated.size(), 1U);
    expectPrimitive(updated[0], makeRect(8, 8, 120, 80), kRedArgb);

    ShadowViewMutationList unmount;

    unmount.push_back(ShadowViewMutation::RemoveMutation(kSurfaceTag, moved, 0));
    unmount.push_back(ShadowViewMutation::DeleteMutation(moved));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(unmount)));

    EXPECT_TRUE(mountingManager.snapshotScene().empty());
    EXPECT_EQ(mountingManager.dumpScene(), "RootView #1 frame=(0.00, 0.00, 800.00, 600.00)\n");
}

TEST(LinuxMountingManagerTest, TakeFramePairsTheSceneWithItsDamageAndClearsThatDamage) {
    LinuxMountingManager mountingManager;

    mountBlueChild(mountingManager);

    const SceneFrame frame = mountingManager.takeFrame();

    ASSERT_EQ(frame.scene.size(), 1U);
    expectPrimitive(frame.scene[0], makeRect(24, 24, 120, 80), kBlueArgb);
    ASSERT_FALSE(frame.damage.empty());
    expectRect(boundsOf(frame.damage), makeRect(0, 0, 800, 600));

    const SceneFrame second = mountingManager.takeFrame();

    EXPECT_EQ(second.scene.size(), 1U);
    EXPECT_TRUE(second.damage.empty());
}

TEST(LinuxMountingManagerTest, ADecodedImageDamagesTheFrameUnderTheSceneMutex) {
    LinuxMountingManager mountingManager;
    const ShadowView imageView = makeTile(2, makeRect(24, 24, 120, 80), "tile.png");
    ShadowViewMutationList mutations;

    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});
    mutations.push_back(ShadowViewMutation::CreateMutation(imageView));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceTag, imageView, 0));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));
    mountingManager.takeFrame();

    mountingManager.damageImageSource("tile.png");

    const SceneFrame frame = mountingManager.takeFrame();

    ASSERT_FALSE(frame.damage.empty());
    expectRect(boundsOf(frame.damage), makeRect(24, 24, 120, 80));
}

TEST(LinuxMountingManagerTest, DispatchCommandLeavesTheSceneUntouched) {
    LinuxMountingManager mountingManager;

    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});

    const std::string dumpBefore = mountingManager.dumpScene();

    mountingManager.dispatchCommand(makeView(2, makeRect(0, 0, 1, 1)), "focus", folly::dynamic::array());

    EXPECT_EQ(mountingManager.dumpScene(), dumpBefore);
}

constexpr uint32_t kDefaultCaretArgb = 0xFF599EFFU;
constexpr uint32_t kDefaultSelectionArgb = 0x59599EFFU;

Rect textInputFrame() { return makeRect(10, 20, 200, 40); }

SceneSnapshot snapshotOfTextInput(const ShadowView& field) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, field);

    return scene.snapshot();
}

SceneSnapshot snapshotOfFieldWith(const std::string& value,
                                  const std::shared_ptr<react_native_linux::TextInputProps>& textInputProps) {
    return snapshotOfTextInput(makeTextInput(2, textInputFrame(), value, textInputProps));
}

TEST(RetainedSceneTextInputTest, TheStateBecomesTheTextAndTheNodeBecomesAnEditor) {
    const SceneSnapshot snapshot = snapshotOfFieldWith("hi", textInputProps());

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].text.has_value());
    EXPECT_EQ(snapshot[0].text.value().attributedString.getString(), "hi");
    ASSERT_TRUE(snapshot[0].editor.has_value());
    EXPECT_FALSE(snapshot[0].editor.value().isPlaceholder);
    EXPECT_FALSE(snapshot[0].editor.value().isMultiline);
    EXPECT_EQ(snapshot[0].editor.value().caretColorArgb, kDefaultCaretArgb);
    EXPECT_EQ(snapshot[0].editor.value().selectionColorArgb, kDefaultSelectionArgb);

    // Nothing has published a caret yet, so the field draws its text and no caret at all.
    EXPECT_EQ(snapshot[0].editor.value().state.caretUtf16, 0U);
    EXPECT_FALSE(snapshot[0].editor.value().state.isCaretVisible);
}

TEST(RetainedSceneTextInputTest, AnEmptyValueDrawsThePlaceholderInItsOwnColour) {
    const std::shared_ptr<react_native_linux::TextInputProps> withPlaceholder = textInputProps();

    withPlaceholder->placeholder = "Type here";
    withPlaceholder->placeholderTextColor = red();

    const SceneSnapshot snapshot = snapshotOfFieldWith({}, withPlaceholder);

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].text.has_value());
    EXPECT_EQ(snapshot[0].text.value().attributedString.getString(), "Type here");
    EXPECT_TRUE(snapshot[0].editor.value().isPlaceholder);
}

TEST(RetainedSceneTextInputTest, AnEmptyFieldWithNoPlaceholderIsStillPainted) {
    const SceneSnapshot snapshot = snapshotOfFieldWith({}, textInputProps());

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot[0].text.value().attributedString.getString(), "");
    EXPECT_TRUE(snapshot[0].editor.value().isPlaceholder);
}

// #255: the placeholder is a second paragraph painted with the field's own style, not a stand-in with one of its
// own — every attribute `getEffectiveTextAttributes` resolves from the field's props reaches the placeholder
// fragment unchanged, and `placeholderTextColor` is the one substitution `readEditorContent` makes.
TEST(RetainedSceneTextInputTest, ThePlaceholderIsPaintedWithTheFieldsOwnFontWeightLetterSpacingAndAlignment) {
    // Semi-transparent, so the assertion below cannot pass by accident: `placeholderTextColor` has to replace
    // the whole foreground colour, alpha included, not just the RGB channels a fully opaque swap would share
    // with the default.
    const facebook::react::SharedColor translucentRed = facebook::react::colorFromRGBA(204, 51, 51, 128);
    const std::shared_ptr<react_native_linux::TextInputProps> styled = textInputProps();

    styled->textAttributes.fontFamily = "Georgia";
    styled->textAttributes.fontWeight = facebook::react::FontWeight::Bold;
    styled->textAttributes.letterSpacing = 2.5F;
    styled->textAttributes.alignment = facebook::react::TextAlignment::Center;
    styled->textAttributes.foregroundColor = blue();
    styled->placeholder = "Type here";
    styled->placeholderTextColor = translucentRed;

    const SceneSnapshot snapshot = snapshotOfFieldWith({}, styled);

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].text.has_value());

    const auto& fragments = snapshot[0].text.value().attributedString.getFragments();

    ASSERT_EQ(fragments.size(), 1U);

    const facebook::react::TextAttributes& placeholderAttributes = fragments[0].textAttributes;

    EXPECT_EQ(placeholderAttributes.fontFamily, "Georgia");
    EXPECT_EQ(placeholderAttributes.fontWeight, facebook::react::FontWeight::Bold);
    EXPECT_FLOAT_EQ(placeholderAttributes.letterSpacing, 2.5F);
    EXPECT_EQ(placeholderAttributes.alignment, facebook::react::TextAlignment::Center);

    // The one attribute that is not the field's own: the placeholder is drawn in its hint colour, not the
    // colour the value would be — RGB and alpha alike, since a swap that dropped the alpha channel would still
    // pass an RGB-only assertion.
    EXPECT_NE(placeholderAttributes.foregroundColor, styled->textAttributes.foregroundColor);
    EXPECT_EQ(facebook::react::redFromColor(placeholderAttributes.foregroundColor),
             facebook::react::redFromColor(translucentRed));
    EXPECT_EQ(facebook::react::greenFromColor(placeholderAttributes.foregroundColor),
             facebook::react::greenFromColor(translucentRed));
    EXPECT_EQ(facebook::react::blueFromColor(placeholderAttributes.foregroundColor),
             facebook::react::blueFromColor(translucentRed));
    EXPECT_EQ(facebook::react::alphaFromColor(placeholderAttributes.foregroundColor),
             facebook::react::alphaFromColor(translucentRed));
}

// The alignment a wrapped placeholder would wrap with is the same `ParagraphAttributes` object the value wraps
// with — never a copy — so a multiline field's `maximumNumberOfLines` and break strategy apply identically to
// the hint and to whatever the user goes on to type.
TEST(RetainedSceneTextInputTest, TheMultilinePlaceholderSharesTheFieldsParagraphAttributes) {
    const std::shared_ptr<react_native_linux::TextInputProps> multiline = textInputProps();

    multiline->multiline = true;
    multiline->placeholder = "Wraps across several lines of a narrow field";
    multiline->paragraphAttributes.maximumNumberOfLines = 3;

    const SceneSnapshot snapshot = snapshotOfFieldWith({}, multiline);

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].text.has_value());
    EXPECT_EQ(snapshot[0].text.value().paragraphAttributes.maximumNumberOfLines, 3);
    EXPECT_TRUE(snapshot[0].editor.value().isMultiline);
    EXPECT_TRUE(snapshot[0].editor.value().isPlaceholder);
}

// #255's caret invariant, at the level this file can prove it: the caret an untouched field reports is index 0,
// and the paragraph it is measured against — `measureEditorGeometry` runs on `node.text`, not on a second string
// — is the placeholder itself. Index 0 of that paragraph *is* the placeholder's first glyph by definition, so
// the two can never disagree; `--type`'s `text-input-placeholder.png` is the pixel proof of the same fact.
TEST(RetainedSceneTextInputTest, AnUntouchedFieldsCaretIndexesThePlaceholdersFirstGlyph) {
    const std::shared_ptr<react_native_linux::TextInputProps> withPlaceholder = textInputProps();

    withPlaceholder->placeholder = "Type here";

    const SceneSnapshot snapshot = snapshotOfFieldWith({}, withPlaceholder);

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot[0].text.value().attributedString.getString(), "Type here");
    EXPECT_EQ(snapshot[0].editor.value().state.caretUtf16, 0U);
}

// The placeholder is a hint, not a name: it vanishes the instant the buffer holds one grapheme and returns the
// instant the last one is deleted, because `isPlaceholder` is a pure function of the current value and nothing
// remembers having shown it before.
TEST(RetainedSceneTextInputTest, ThePlaceholderDisappearsOnTheFirstCharacterAndReturnsOnTheLastDeletion) {
    const std::shared_ptr<react_native_linux::TextInputProps> withPlaceholder = textInputProps();

    withPlaceholder->placeholder = "Type here";

    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeTextInput(2, textInputFrame(), {}, withPlaceholder));

    ASSERT_TRUE(scene.snapshot()[0].editor.value().isPlaceholder);

    scene.updateNode(makeTextInput(2, textInputFrame(), "h", withPlaceholder));

    SceneSnapshot afterFirstCharacter = scene.snapshot();

    ASSERT_EQ(afterFirstCharacter.size(), 1U);
    EXPECT_FALSE(afterFirstCharacter[0].editor.value().isPlaceholder);
    EXPECT_EQ(afterFirstCharacter[0].text.value().attributedString.getString(), "h");

    scene.updateNode(makeTextInput(2, textInputFrame(), {}, withPlaceholder));

    SceneSnapshot afterLastDeletion = scene.snapshot();

    ASSERT_EQ(afterLastDeletion.size(), 1U);
    EXPECT_TRUE(afterLastDeletion[0].editor.value().isPlaceholder);
    EXPECT_EQ(afterLastDeletion[0].text.value().attributedString.getString(), "Type here");
}

TEST(RetainedSceneTextInputTest, CursorAndSelectionColoursOverrideTheAccent) {
    const std::shared_ptr<react_native_linux::TextInputProps> coloured = textInputProps();

    coloured->cursorColor = red();
    coloured->selectionColor = blue();
    coloured->multiline = true;

    const SceneSnapshot snapshot = snapshotOfFieldWith("hi", coloured);

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot[0].editor.value().caretColorArgb, kRedArgb);
    EXPECT_EQ(snapshot[0].editor.value().selectionColorArgb, kBlueArgb);
    EXPECT_TRUE(snapshot[0].editor.value().isMultiline);
}

TEST(RetainedSceneTextInputTest, CaretHiddenRemovesTheCaretColourAndTheCaretWithIt) {
    const std::shared_ptr<react_native_linux::TextInputProps> hidden = textInputProps();

    hidden->caretHidden = true;

    const SceneSnapshot snapshot = snapshotOfFieldWith("hi", hidden);

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot[0].editor.value().caretColorArgb, 0U);
}

TEST(RetainedSceneTextInputTest, ANodeWithoutTextInputStateCarriesNoEditor) {
    ShadowView field = makeTextInput(2, textInputFrame(), "hi", textInputProps());

    field.state = nullptr;

    EXPECT_TRUE(snapshotOfTextInput(field).empty());
}

TEST(RetainedSceneTextInputTest, TheCaretAndSelectionReachTheSnapshotAndDamageOnlyWhenTheyMove) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeTextInput(2, textInputFrame(), "hi", textInputProps()));
    scene.takeDamage();

    const SceneEditorState editorState{.caretUtf16 = 1,
                                       .selectionBeginUtf16 = 0,
                                       .selectionEndUtf16 = 1,
                                       .compositionBeginUtf16 = 0,
                                       .compositionEndUtf16 = 1,
                                       .scrollOffsetX = 4.0F,
                                       .isCaretVisible = true};

    scene.setEditorState(2, editorState);

    const SceneDamage moved = scene.takeDamage();

    ASSERT_EQ(moved.size(), 1U);
    expectRect(moved[0], textInputFrame());

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].editor.has_value());
    EXPECT_EQ(snapshot[0].editor.value().state.caretUtf16, 1U);
    EXPECT_EQ(snapshot[0].editor.value().state.selectionEndUtf16, 1U);
    EXPECT_EQ(snapshot[0].editor.value().state.compositionEndUtf16, 1U);
    EXPECT_TRUE(snapshot[0].editor.value().state.isCaretVisible);
    EXPECT_EQ(snapshot[0].editor.value().state.scrollOffsetX, 4.0F);

    // A caret that has not moved is not a repaint, which is what keeps a field nobody is typing in idle.
    scene.setEditorState(2, editorState);

    EXPECT_TRUE(scene.takeDamage().empty());
}

TEST(RetainedSceneTextInputTest, DeletingAFieldForgetsItsEditorState) {
    RetainedScene scene;
    const ShadowView field = makeTextInput(2, textInputFrame(), "hi", textInputProps());

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, field);
    scene.setEditorState(2, SceneEditorState{.caretUtf16 = 1, .isCaretVisible = true});
    scene.removeChild(kSurfaceTag, field);
    scene.deleteNode(2);
    addChild(scene, kSurfaceTag, field);

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_FALSE(snapshot[0].editor.value().state.isCaretVisible);
}

#pragma mark - transformed rows under scroll (#47)

// The fixture both scroll tests build: a ScrollView clipping its rows, plus two transformed rows inside it.
// The scroll offset travels the production path - `ScrollViewState` on the node - so `readScrollContent`,
// `contentOrigin` and the clip stack are what the assertions exercise.
facebook::react::ShadowView makeScrollViewAt(facebook::react::Tag tag, facebook::react::Point contentOffset) {
    facebook::react::ShadowView shadowView;

    shadowView.tag = tag;
    shadowView.componentName = "ScrollView";
    shadowView.layoutMetrics.frame = facebook::react::Rect{.origin = {0, 0}, .size = {.width = 400, .height = 600}};

    const auto scrollState = std::make_shared<facebook::react::ConcreteState<facebook::react::ScrollViewState>>(
        std::make_shared<facebook::react::ScrollViewState>(facebook::react::ScrollViewState{contentOffset, {}, 0}),
        facebook::react::ShadowNodeFamily::Weak{});

    shadowView.state = scrollState;

    return shadowView;
}

struct ScrolledListFixture {
    RetainedScene scene;
    std::shared_ptr<ViewProps> rowProps = propsWithBackground(blue());

    // The surface, a clipping ScrollView, and the clearing take: every scroll test starts from this state.
    void buildScrollList() {
        scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
        scene.createNode(makeScrollViewAt(2, {0, 0}));
        scene.insertChild(kSurfaceTag, makeScrollViewAt(2, {0, 0}), 0);
    }
};

TEST(RetainedSceneDamageTest, AScrolledRotatedRowDamagesTheTransformedBoundsAtBothOffsets) {
    ScrolledListFixture fixture;

    fixture.rowProps->transform = facebook::react::Transform::RotateZ(kQuarterTurnRadians);

    fixture.buildScrollList();
    fixture.scene.takeDamage();

    const facebook::react::ShadowView row = makeStyledView(3, makeRect(100, 250, 200, 100), fixture.rowProps);

    fixture.scene.createNode(row);
    fixture.scene.insertChild(2, row, 0);

    fixture.scene.takeDamage();

    // The scroll: the content offset moves by 150, which the scene reads off the scroll view's state - the row
    // frame itself is untouched, exactly as a real scroll leaves it.
    fixture.scene.updateNode(makeScrollViewAt(2, {0, -150}));

    const SceneDamage damage = fixture.scene.takeDamage();

    ASSERT_FALSE(damage.empty());

    // A 200x100 row rotated 45 degrees has an axis-aligned bounding box of (w + h)/sqrt(2) per side around its
    // centre (200, 300): x 93.93..306.07, y 193.93..406.07. Independently computed, and the same half-extents
    // the assert below pins.
    //
    // The scroll offset then moves the painted row down by 150 - the row centre paints at 450 - and the frame
    // update damages BOTH paints: the union spans the old box top (193.93) to the new box bottom (556.07),
    // which is one rotated box wide and one rotated box tall plus the scroll distance.
    const float rotatedSide = (200.0F + 100.0F) * 0.70710678F;

    expectRect(boundsOf(damage),
               makeRect(200.0F - rotatedSide / 2.0F, 300.0F - rotatedSide / 2.0F, rotatedSide, rotatedSide + 150.0F));
}

TEST(RetainedSceneDamageTest, ATransformedRowOverhangingAClipIsCutTheSameScrolledOrLaidOut) {
    const std::shared_ptr<ViewProps> rowProps = propsWithBackground(blue());
    rowProps->transform = facebook::react::Transform::Scale(2, 2, 1);

    // Laid out inside the clip: created directly at the final painted position (contentOffset 0, row frame
    // y = 250, which hangs below the 150-tall viewport).
    RetainedScene laidOut;
    laidOut.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    laidOut.createNode(makeScrollViewAt(2, {0, 0}));
    laidOut.insertChild(kSurfaceTag, makeScrollViewAt(2, {0, 0}), 0);

    laidOut.takeDamage();

    const facebook::react::ShadowView row = makeStyledView(3, makeRect(50, 250, 200, 100), rowProps);

    laidOut.createNode(row);
    laidOut.insertChild(2, row, 0);

    const SceneSnapshot laidOutSnapshot = laidOut.snapshot();
    const SceneDamage laidOutDamage = laidOut.takeDamage();

    // The same painted position reached by scrolling: the row is created at content frame y = 100 and the
    // contentOffset (0, -150) shifts it down to the same painted 250 - row frame untouched.
    RetainedScene scrolled;
    scrolled.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    scrolled.createNode(makeScrollViewAt(2, {0, 0}));
    scrolled.insertChild(kSurfaceTag, makeScrollViewAt(2, {0, 0}), 0);

    scrolled.takeDamage();

    const facebook::react::ShadowView scrolledRow = makeStyledView(3, makeRect(50, 100, 200, 100), rowProps);

    scrolled.createNode(scrolledRow);
    scrolled.insertChild(2, scrolledRow, 0);

    scrolled.updateNode(makeScrollViewAt(2, {0, -150}));

    EXPECT_TRUE(sameSnapshot(scrolled.snapshot(), laidOutSnapshot));

    const SceneDamage scrolledDamage = scrolled.takeDamage();
    ASSERT_FALSE(scrolledDamage.empty());

    // The scrolled scene damages the pre-scroll paint too; its final rect is the laid-out twin's clipped row.
    ASSERT_FALSE(laidOutDamage.empty());
    EXPECT_EQ(scrolledDamage.back(), laidOutDamage.back());
}

/**
 * Point 4: scrolling down 150 and back 150 restores a snapshot identical to the start, and the return trip
 * damages exactly the band the outward trip did.
 */
TEST(RetainedSceneDamageTest, ScrollByNThenMinusNRestoresTheSnapshotAndMirrorsTheDamage) {
    ScrolledListFixture fixture;

    fixture.buildScrollList();
    fixture.scene.takeDamage();

    const facebook::react::ShadowView row = makeStyledView(3, makeRect(50, 200, 300, 100), fixture.rowProps);

    fixture.scene.createNode(row);
    fixture.scene.insertChild(2, row, 0);

    const SceneSnapshot original = fixture.scene.snapshot();

    fixture.scene.takeDamage();

    fixture.scene.updateNode(makeScrollViewAt(2, {0, -150}));

    const SceneDamage outwardDamage = fixture.scene.takeDamage();
    ASSERT_FALSE(outwardDamage.empty());

    fixture.scene.updateNode(makeScrollViewAt(2, {0, 0}));

    const SceneDamage returnDamage = fixture.scene.takeDamage();
    ASSERT_FALSE(returnDamage.empty());

    EXPECT_TRUE(sameSnapshot(fixture.scene.snapshot(), original));
    EXPECT_EQ(boundsOf(returnDamage), boundsOf(outwardDamage));
}

/**
 * Point 1: an incrementally scrolled transformed list ends where a freshly committed scene with the same rows
 * and the same final offset stands - the full-repaint equivalence, in content including transforms and clips.
 */
TEST(RetainedSceneDamageTest, AnIncrementallyScrolledTransformedListMatchesAFreshBuild) {
    const std::shared_ptr<ViewProps> rowProps = propsWithBackground(blue());
    rowProps->transform = facebook::react::Transform::RotateZ(kQuarterTurnRadians);

    RetainedScene incremental;
    incremental.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    incremental.createNode(makeScrollViewAt(2, {0, 0}));
    incremental.insertChild(kSurfaceTag, makeScrollViewAt(2, {0, 0}), 0);

    incremental.takeDamage();

    const facebook::react::ShadowView firstRow = makeStyledView(3, makeRect(100, 100, 200, 100), rowProps);
    const facebook::react::ShadowView secondRow = makeStyledView(4, makeRect(100, 300, 200, 100), rowProps);

    incremental.createNode(firstRow);
    incremental.insertChild(2, firstRow, 0);
    incremental.createNode(secondRow);
    incremental.insertChild(2, secondRow, 0);

    for (const float offset : {0.0F, -150.0F, -300.0F}) {
        incremental.updateNode(makeScrollViewAt(2, {0, offset}));
    }

    RetainedScene fresh;
    fresh.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    fresh.createNode(makeScrollViewAt(2, {0, -300}));
    fresh.insertChild(kSurfaceTag, makeScrollViewAt(2, {0, -300}), 0);

    fresh.createNode(firstRow);
    fresh.insertChild(2, firstRow, 0);
    fresh.createNode(secondRow);
    fresh.insertChild(2, secondRow, 0);

    EXPECT_TRUE(sameSnapshot(incremental.snapshot(), fresh.snapshot()));
}

} // namespace