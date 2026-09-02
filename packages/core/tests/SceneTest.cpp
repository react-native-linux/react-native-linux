#include "LinuxMountingManager.h"
#include "RetainedScene.h"

#include <gtest/gtest.h>

#include <folly/dynamic.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/graphics/Size.h>
#include <react/renderer/graphics/Transform.h>
#include <react/renderer/graphics/ValueUnit.h>
#include <react/renderer/mounting/MountingTransaction.h>
#include <react/renderer/mounting/ShadowView.h>
#include <react/renderer/mounting/ShadowViewMutation.h>
#include <react/renderer/telemetry/TransactionTelemetry.h>
#include <yoga/enums/Edge.h>
#include <yoga/enums/Overflow.h>
#include <yoga/style/StyleLength.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace yoga = facebook::yoga;

using facebook::react::MountingTransaction;
using facebook::react::Point;
using facebook::react::Rect;
using facebook::react::SharedColor;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::ShadowViewMutationList;
using facebook::react::Size;
using facebook::react::Tag;
using facebook::react::Transform;
using facebook::react::UnitType;
using facebook::react::ValueUnit;
using facebook::react::ViewProps;
using react_native_linux::LinuxMountingManager;
using react_native_linux::RetainedScene;
using react_native_linux::SceneDamage;
using react_native_linux::SceneFrame;
using react_native_linux::SceneSnapshot;

constexpr Tag kSurfaceTag = 1;
constexpr uint32_t kBlueArgb = 0xFF3366CCU;
constexpr uint32_t kRedArgb = 0xFFCC3333U;
constexpr uint32_t kHalfBlueArgb = 0x803366CCU;
constexpr uint32_t kQuarterRedArgb = 0x40CC3333U;

Rect makeRect(float x, float y, float width, float height) {
    return Rect{.origin = Point{.x = x, .y = y}, .size = Size{.width = width, .height = height}};
}

SharedColor blue() {
    return facebook::react::colorFromRGBA(51, 102, 204, 255);
}

SharedColor red() {
    return facebook::react::colorFromRGBA(204, 51, 51, 255);
}

SharedColor invisibleBlue() {
    return facebook::react::colorFromRGBA(51, 102, 204, 0);
}

ShadowView makeView(Tag tag, Rect frame) {
    ShadowView shadowView;

    shadowView.tag = tag;
    shadowView.componentName = "View";
    shadowView.layoutMetrics.frame = frame;

    return shadowView;
}

ShadowView makeStyledView(Tag tag, Rect frame, const std::shared_ptr<ViewProps>& viewProps) {
    ShadowView shadowView = makeView(tag, frame);

    shadowView.props = viewProps;

    return shadowView;
}

std::shared_ptr<ViewProps> propsWithBackground(SharedColor backgroundColor) {
    const std::shared_ptr<ViewProps> viewProps = std::make_shared<ViewProps>();

    viewProps->backgroundColor = backgroundColor;

    return viewProps;
}

ShadowView makePaintedView(Tag tag, Rect frame, SharedColor backgroundColor) {
    return makeStyledView(tag, frame, propsWithBackground(backgroundColor));
}

void addChild(RetainedScene& scene, Tag parentTag, const ShadowView& child) {
    scene.createNode(child);
    scene.insertChild(parentTag, child, 0);
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

MountingTransaction transactionOf(ShadowViewMutationList&& mutations) {
    return MountingTransaction{kSurfaceTag, 1, std::move(mutations), facebook::react::TransactionTelemetry{}};
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

    EXPECT_EQ(scene.dump(),
              "RootView #1 frame=(0.00, 0.00, 800.00, 600.00)\n"
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

    EXPECT_EQ(scene.dump(),
              "RootView #1 frame=(0.00, 0.00, 800.00, 600.00)\n"
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
    EXPECT_EQ(scene.dump(),
              "RootView #1 frame=(0.00, 0.00, 800.00, 600.00)\n"
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

    EXPECT_EQ(scene.dump(),
              "RootView #2 frame=(0.00, 0.00, 20.00, 20.00)\n"
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
    RetainedScene scene = sceneWithPaintedChild();

    scene.takeDamage();
    scene.updateNode(makePaintedView(2, makeRect(300, 400, 50, 60), red()));

    const SceneDamage damage = scene.takeDamage();

    ASSERT_EQ(damage.size(), 2U);
    expectRect(damage[0], makeRect(10, 20, 200, 100));
    expectRect(damage[1], makeRect(300, 400, 50, 60));
}

TEST(RetainedSceneDamageTest, AColorOnlyChangeDamagesThePrimitiveBoundsTwice) {
    RetainedScene scene = sceneWithPaintedChild();

    scene.takeDamage();
    scene.updateNode(makePaintedView(2, makeRect(10, 20, 200, 100), red()));

    const SceneDamage damage = scene.takeDamage();

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
    RetainedScene scene;
    const std::shared_ptr<ViewProps> clippingProps = propsWithBackground(blue());

    clippingProps->yogaStyle.setOverflow(yoga::Overflow::Hidden);

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(2, makeRect(25, 35, 200, 150), clippingProps));
    scene.takeDamage();
    addChild(scene, 2, makePaintedView(3, makeRect(10, 10, 400, 400), red()));

    const SceneDamage damage = scene.takeDamage();

    ASSERT_FALSE(damage.empty());
    expectRect(damage.back(), makeRect(35, 45, 190, 140));
}

TEST(RetainedSceneDamageTest, APrimitiveEntirelyOutsideItsClipDamagesNothing) {
    RetainedScene scene;
    const std::shared_ptr<ViewProps> clippingProps = std::make_shared<ViewProps>();

    clippingProps->yogaStyle.setOverflow(yoga::Overflow::Hidden);

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(2, makeRect(0, 0, 100, 100), clippingProps));

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

ShadowView mountBlueChild(LinuxMountingManager& mountingManager) {
    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});

    const ShadowView child = makePaintedView(2, makeRect(24, 24, 120, 80), blue());
    ShadowViewMutationList mutations;

    mutations.push_back(ShadowViewMutation::CreateMutation(child));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceTag, child, 0));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));

    return child;
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

TEST(LinuxMountingManagerTest, DispatchCommandLeavesTheSceneUntouched) {
    LinuxMountingManager mountingManager;

    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});

    const std::string dumpBefore = mountingManager.dumpScene();

    mountingManager.dispatchCommand(makeView(2, makeRect(0, 0, 1, 1)), "focus", folly::dynamic::array());

    EXPECT_EQ(mountingManager.dumpScene(), dumpBefore);
}

} // namespace
