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
#include <react/renderer/mounting/MountingTransaction.h>
#include <react/renderer/mounting/ShadowView.h>
#include <react/renderer/mounting/ShadowViewMutation.h>
#include <react/renderer/telemetry/TransactionTelemetry.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using facebook::react::MountingTransaction;
using facebook::react::Point;
using facebook::react::Rect;
using facebook::react::SharedColor;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::ShadowViewMutationList;
using facebook::react::Size;
using facebook::react::Tag;
using react_native_linux::LinuxMountingManager;
using react_native_linux::RetainedScene;
using react_native_linux::SceneSnapshot;

constexpr Tag kSurfaceTag = 1;
constexpr uint32_t kBlueArgb = 0xFF3366CCU;
constexpr uint32_t kRedArgb = 0xFFCC3333U;

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

ShadowView makePaintedView(Tag tag, Rect frame, SharedColor backgroundColor) {
    ShadowView shadowView = makeView(tag, frame);
    const std::shared_ptr<facebook::react::ViewProps> viewProps = std::make_shared<facebook::react::ViewProps>();

    viewProps->backgroundColor = backgroundColor;
    shadowView.props = viewProps;

    return shadowView;
}

RetainedScene sceneWithPaintedChild() {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});

    const ShadowView child = makePaintedView(2, makeRect(10, 20, 200, 100), blue());

    scene.createNode(child);
    scene.insertChild(kSurfaceTag, child, 0);

    return scene;
}

void expectRectangle(const react_native_linux::SceneRectangle& rectangle, Rect frame, uint32_t colorArgb) {
    EXPECT_FLOAT_EQ(rectangle.frame.origin.x, frame.origin.x);
    EXPECT_FLOAT_EQ(rectangle.frame.origin.y, frame.origin.y);
    EXPECT_FLOAT_EQ(rectangle.frame.size.width, frame.size.width);
    EXPECT_FLOAT_EQ(rectangle.frame.size.height, frame.size.height);
    EXPECT_EQ(rectangle.colorArgb, colorArgb);
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
    const ShadowView grandChild = makePaintedView(3, makeRect(5, 7, 50, 40), red());

    scene.createNode(grandChild);
    scene.insertChild(2, grandChild, 0);

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 2U);
    expectRectangle(snapshot[0], makeRect(10, 20, 200, 100), kBlueArgb);
    expectRectangle(snapshot[1], makeRect(15, 27, 50, 40), kRedArgb);
}

TEST(RetainedSceneTest, DumpKeepsParentRelativeFramesAndIndentsByDepth) {
    RetainedScene scene = sceneWithPaintedChild();
    const ShadowView grandChild = makeView(3, makeRect(5, 7, 50, 40));

    scene.createNode(grandChild);
    scene.insertChild(2, grandChild, 0);

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
    expectRectangle(snapshot[0], makeRect(1, 2, 30, 40), kRedArgb);
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

    const ShadowView withoutProps = makeView(2, makeRect(0, 0, 10, 10));
    const ShadowView fullyTransparent = makePaintedView(3, makeRect(0, 0, 10, 10), invisibleBlue());
    const ShadowView opaque = makePaintedView(4, makeRect(0, 0, 10, 10), blue());

    for (const ShadowView& child : {withoutProps, fullyTransparent, opaque}) {
        scene.createNode(child);
        scene.insertChild(kSurfaceTag, child, 0);
    }

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot[0].colorArgb, kBlueArgb);
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
    expectRectangle(snapshot[0], makeRect(110, 220, 200, 100), kBlueArgb);
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
    expectRectangle(snapshot[0], makeRect(10, 20, 200, 100), kBlueArgb);
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
    expectRectangle(snapshot[0], makeRect(24, 24, 120, 80), kBlueArgb);
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
    expectRectangle(updated[0], makeRect(8, 8, 120, 80), kRedArgb);

    ShadowViewMutationList unmount;

    unmount.push_back(ShadowViewMutation::RemoveMutation(kSurfaceTag, moved, 0));
    unmount.push_back(ShadowViewMutation::DeleteMutation(moved));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(unmount)));

    EXPECT_TRUE(mountingManager.snapshotScene().empty());
    EXPECT_EQ(mountingManager.dumpScene(), "RootView #1 frame=(0.00, 0.00, 800.00, 600.00)\n");
}

TEST(LinuxMountingManagerTest, DispatchCommandLeavesTheSceneUntouched) {
    LinuxMountingManager mountingManager;

    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});

    const std::string dumpBefore = mountingManager.dumpScene();

    mountingManager.dispatchCommand(makeView(2, makeRect(0, 0, 1, 1)), "focus", folly::dynamic::array());

    EXPECT_EQ(mountingManager.dumpScene(), dumpBefore);
}

} // namespace
