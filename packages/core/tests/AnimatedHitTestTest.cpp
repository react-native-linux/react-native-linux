#include "LinuxMountingManager.h"
#include "RetainedScene.h"
#include "SceneTestSupport.h"

#include <gtest/gtest.h>

#include <array>

#include <folly/dynamic.h>
#include <react/renderer/animated/NativeAnimatedNodesManager.h>
#include <react/renderer/components/view/primitives.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <yoga/enums/Overflow.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

// Issues #97 and #121: a native-driven animation writes the retained scene every frame and commits nothing, so
// the shadow tree's LayoutMetrics describe where a moving node started rather than where it is. Hit testing
// therefore reads the scene — the same nodes, the same composed matrices and the same clips the painter reads —
// and this file is what that promises: the node a press lands on is the node the frame painted, on every frame of
// an animation and not only after it settles. See *Hit-testing under animation* in docs/cpp-toolchain.md.
//
// The second half drives a real NativeAnimatedNodesManager, with a real frames driver and an injected clock, into
// LinuxMountingManager::synchronouslyUpdateViewOnUIThread. Per tick it asserts that the scene holds exactly the
// value the driver just handed over, that the painter's matrix and the hit test agree with it inside that same
// tick, and that no Fabric commit happened at all.

namespace {

using facebook::react::NativeAnimatedNodesManager;
using facebook::react::PointerEventsMode;
using react_native_linux::SceneHit;

constexpr Tag kBoxTag = 2;
constexpr Tag kInnerTag = 3;
constexpr Tag kCoverTag = 4;
constexpr Tag kUnmountedTag = 404;
constexpr Tag kValueNodeTag = 101;
constexpr Tag kTransformNodeTag = 102;
constexpr Tag kStyleNodeTag = 103;
constexpr Tag kPropsNodeTag = 104;
constexpr int kAnimationId = 1;
constexpr double kTranslationEnd = 240.0;
constexpr double kTranslation = 120.0;
constexpr size_t kTickCount = 3;
constexpr double kFrameIntervalMilliseconds = 1000.0 / 60.0;

Size surfaceSize() {
    return Size{.width = 800, .height = 600};
}

Rect boxFrame() {
    return makeRect(100, 80, 200, 120);
}

Point pointAt(float x, float y) {
    return Point{.x = x, .y = y};
}

// The centre of the box as it was laid out, which is also where a press lands before anything animates it.
Point boxCentre() {
    return pointAt(200, 140);
}

std::shared_ptr<ViewProps> propsWithPointerEvents(PointerEventsMode pointerEvents) {
    const std::shared_ptr<ViewProps> viewProps = std::make_shared<ViewProps>();

    viewProps->pointerEvents = pointerEvents;

    return viewProps;
}

std::shared_ptr<ViewProps> clippingProps() {
    const std::shared_ptr<ViewProps> viewProps = std::make_shared<ViewProps>();

    viewProps->yogaStyle.setOverflow(facebook::yoga::Overflow::Hidden);

    return viewProps;
}

std::shared_ptr<ViewProps> scaledToNothingProps() {
    const std::shared_ptr<ViewProps> viewProps = std::make_shared<ViewProps>();

    viewProps->transform = Transform::Scale(0, 0, 1);

    return viewProps;
}

void appendChild(RetainedScene& scene, Tag parentTag, const ShadowView& child, int index) {
    scene.createNode(child);
    scene.insertChild(parentTag, child, index);
}

RetainedScene sceneWithBox(const std::shared_ptr<ViewProps>& viewProps) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, surfaceSize());
    appendChild(scene, kSurfaceTag, makeStyledView(kBoxTag, boxFrame(), viewProps), 0);

    return scene;
}

// A child that pokes out of its parent on the right, so a clip and the absence of one give different answers.
Rect innerFrame() {
    return makeRect(150, 20, 200, 80);
}

RetainedScene sceneWithNestedBox(const std::shared_ptr<ViewProps>& viewProps) {
    RetainedScene scene = sceneWithBox(viewProps);

    appendChild(scene, kBoxTag, makePaintedView(kInnerTag, innerFrame(), red()), 0);

    return scene;
}

// The payload `TransformAnimatedNode::collectViewUpdates` produces for one animated `translateX`, which is what
// reaches `synchronouslyUpdateViewOnUIThread` under the `transform` key.
folly::dynamic translationProps(double translateX) {
    return folly::dynamic::object("transform",
                                  folly::dynamic::array(folly::dynamic::object("translateX", translateX)));
}

// Mounts a painted box under a fresh surface and consumes the frame that mounting it produced.
void mountBox(LinuxMountingManager& mountingManager) {
    const ShadowView box = makePaintedView(kBoxTag, boxFrame(), blue());

    mountingManager.startSurface(kSurfaceTag, surfaceSize());
    mountingManager.executeMount(
        kSurfaceTag, transactionOf(ShadowViewMutationList{ShadowViewMutation::CreateMutation(box),
                                                          ShadowViewMutation::InsertMutation(kSurfaceTag, box, 0)}));
    mountingManager.takeFrame();
}

/**
 * The tick the animated node graph is stepped at. `g_setNativeAnimatedNowTimestampFunction` takes a plain function
 * pointer, so the value it reads cannot travel in a capture.
 */
std::chrono::steady_clock::time_point& frameTime() {
    static std::chrono::steady_clock::time_point tickTime{};

    return tickTime;
}

std::chrono::steady_clock::time_point frameTimeNow() {
    return frameTime();
}

TEST(AnimatedHitTestTest, APointOnANodeFindsThatNodeAndTheOriginItWasPaintedAt) {
    const RetainedScene scene = sceneWithBox(propsWithBackground(blue()));
    const SceneHit hit = scene.findNodeAtPoint(kSurfaceTag, boxCentre());

    EXPECT_EQ(hit.tag, kBoxTag);
    EXPECT_FLOAT_EQ(hit.origin.x, boxFrame().origin.x);
    EXPECT_FLOAT_EQ(hit.origin.y, boxFrame().origin.y);
}

TEST(AnimatedHitTestTest, APointOutsideEveryChildIsTheSurfaceRoot) {
    const RetainedScene scene = sceneWithBox(propsWithBackground(blue()));
    const SceneHit hit = scene.findNodeAtPoint(kSurfaceTag, pointAt(700, 500));

    EXPECT_EQ(hit.tag, kSurfaceTag);
    EXPECT_FLOAT_EQ(hit.origin.x, 0);
    EXPECT_FLOAT_EQ(hit.origin.y, 0);
}

TEST(AnimatedHitTestTest, APointOutsideTheSurfaceFindsNothing) {
    const RetainedScene scene = sceneWithBox(propsWithBackground(blue()));

    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(900, 700)).tag, 0);
}

TEST(AnimatedHitTestTest, ATagTheSceneDoesNotHoldFindsNothing) {
    const RetainedScene scene = sceneWithBox(propsWithBackground(blue()));

    EXPECT_EQ(scene.findNodeAtPoint(kUnmountedTag, boxCentre()).tag, 0);
}

TEST(AnimatedHitTestTest, TheSiblingPaintedLastIsTheOneThePointFinds) {
    RetainedScene scene = sceneWithBox(propsWithBackground(blue()));

    appendChild(scene, kSurfaceTag, makePaintedView(kCoverTag, boxFrame(), red()), 1);

    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, boxCentre()).tag, kCoverTag);
}

TEST(AnimatedHitTestTest, PointerEventsNoneRemovesTheNodeAndItsChildren) {
    const RetainedScene scene = sceneWithNestedBox(propsWithPointerEvents(PointerEventsMode::None));

    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(260, 140)).tag, kSurfaceTag);
}

TEST(AnimatedHitTestTest, PointerEventsBoxNoneKeepsTheChildrenAndDropsTheBox) {
    const RetainedScene scene = sceneWithNestedBox(propsWithPointerEvents(PointerEventsMode::BoxNone));

    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(260, 140)).tag, kInnerTag);
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(120, 190)).tag, kSurfaceTag);
}

TEST(AnimatedHitTestTest, PointerEventsBoxOnlyKeepsTheBoxAndDropsTheChildren) {
    const RetainedScene scene = sceneWithNestedBox(propsWithPointerEvents(PointerEventsMode::BoxOnly));

    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(260, 140)).tag, kBoxTag);
}

// Issue #64. `pointerEvents` is not a boolean, and react-native-windows spent a decade finding the four values'
// combinations one report at a time (#8496). The table below is the contract at nesting depth two: the parent's
// value crossed with the child's, probed at a point inside the child and at a point inside the parent only.
//
// The inherited case is the one that matters most and is easiest to get wrong: a `none` parent makes an `auto`
// child unreachable, because the walk never descends into it. `box-only` does the same to children and keeps
// itself; `box-none` is the mirror. Nothing here is a special case — `isPointerTarget` and
// `arePointerChildrenTargets` are two predicates, and the table is what the two compose to.

struct PointerEventsCase {
    PointerEventsMode parent;
    PointerEventsMode child;
    Tag expectedInsideChild;
    Tag expectedInsideParentOnly;
};

RetainedScene sceneWithNestedPointerEvents(PointerEventsMode parent, PointerEventsMode child) {
    RetainedScene scene = sceneWithBox(propsWithPointerEvents(parent));

    appendChild(scene, kBoxTag, makeStyledView(kInnerTag, innerFrame(), propsWithPointerEvents(child)), 0);

    return scene;
}

TEST(AnimatedHitTestTest, PointerEventsComposeAcrossTwoLevelsAsTheTableSays) {
    constexpr PointerEventsMode kAuto = PointerEventsMode::Auto;
    constexpr PointerEventsMode kNone = PointerEventsMode::None;
    constexpr PointerEventsMode kBoxNone = PointerEventsMode::BoxNone;
    constexpr PointerEventsMode kBoxOnly = PointerEventsMode::BoxOnly;
    const std::array<PointerEventsCase, 16> table{{
        {kAuto, kAuto, kInnerTag, kBoxTag},       {kAuto, kNone, kBoxTag, kBoxTag},
        {kAuto, kBoxNone, kBoxTag, kBoxTag},      {kAuto, kBoxOnly, kInnerTag, kBoxTag},
        {kNone, kAuto, kSurfaceTag, kSurfaceTag}, {kNone, kNone, kSurfaceTag, kSurfaceTag},
        {kNone, kBoxNone, kSurfaceTag, kSurfaceTag}, {kNone, kBoxOnly, kSurfaceTag, kSurfaceTag},
        {kBoxNone, kAuto, kInnerTag, kSurfaceTag}, {kBoxNone, kNone, kSurfaceTag, kSurfaceTag},
        {kBoxNone, kBoxNone, kSurfaceTag, kSurfaceTag}, {kBoxNone, kBoxOnly, kInnerTag, kSurfaceTag},
        {kBoxOnly, kAuto, kBoxTag, kBoxTag},      {kBoxOnly, kNone, kBoxTag, kBoxTag},
        {kBoxOnly, kBoxNone, kBoxTag, kBoxTag},   {kBoxOnly, kBoxOnly, kBoxTag, kBoxTag},
    }};

    for (const PointerEventsCase& entry : table) {
        const RetainedScene scene = sceneWithNestedPointerEvents(entry.parent, entry.child);
        const int parentValue = static_cast<int>(entry.parent);
        const int childValue = static_cast<int>(entry.child);

        EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(260, 140)).tag, entry.expectedInsideChild)
            << "inside the child, parent " << parentValue << " child " << childValue;
        EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(120, 190)).tag, entry.expectedInsideParentOnly)
            << "inside the parent only, parent " << parentValue << " child " << childValue;
    }
}

// react-native-windows#10493: changing `pointerEvents` from `none` back to `box-none` never restored hit testing,
// because the prop was applied once at mount and never invalidated. Here the prop is re-read on every
// `updateNode`, so the commit that changes it is the commit that changes the answer.
TEST(AnimatedHitTestTest, ChangingPointerEventsInALaterCommitChangesTheAnswerInThatCommit) {
    RetainedScene scene = sceneWithNestedPointerEvents(PointerEventsMode::None, PointerEventsMode::Auto);

    ASSERT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(260, 140)).tag, kSurfaceTag);

    scene.updateNode(makeStyledView(kBoxTag, boxFrame(), propsWithPointerEvents(PointerEventsMode::BoxNone)));

    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(260, 140)).tag, kInnerTag);
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(120, 190)).tag, kSurfaceTag);

    scene.updateNode(makeStyledView(kBoxTag, boxFrame(), propsWithPointerEvents(PointerEventsMode::Auto)));

    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(120, 190)).tag, kBoxTag);
}

TEST(AnimatedHitTestTest, AnOverflowHiddenAncestorClipsWhatCanBePressed) {
    const RetainedScene scene = sceneWithNestedBox(clippingProps());

    // Inside the child and inside the clip: the child. Inside the child's own frame but past the clip: nothing
    // paints there, so nothing is pressed there either.
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(260, 140)).tag, kInnerTag);
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, pointAt(320, 140)).tag, kSurfaceTag);
}

TEST(AnimatedHitTestTest, ANodeScaledToNothingCannotBePressed) {
    const RetainedScene scene = sceneWithBox(scaledToNothingProps());

    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, boxCentre()).tag, kSurfaceTag);
}

TEST(AnimatedHitTestTest, ANodeAnimatedToFullTransparencyPaintsNothingAndIsStillPressable) {
    LinuxMountingManager mountingManager;

    mountBox(mountingManager);
    mountingManager.synchronouslyUpdateViewOnUIThread(kBoxTag, folly::dynamic::object("opacity", 0.0));

    EXPECT_TRUE(mountingManager.snapshotScene().empty());
    EXPECT_EQ(mountingManager.findNodeAtPoint(kSurfaceTag, boxCentre()).tag, kBoxTag);
}

TEST(AnimatedHitTestTest, ATranslationAppliedWithoutACommitMovesWhereAPressLands) {
    LinuxMountingManager mountingManager;

    mountBox(mountingManager);
    mountingManager.synchronouslyUpdateViewOnUIThread(kBoxTag, translationProps(kTranslation));

    const Point translatedCentre = pointAt(boxCentre().x + static_cast<float>(kTranslation), boxCentre().y);
    const SceneHit translatedHit = mountingManager.findNodeAtPoint(kSurfaceTag, translatedCentre);

    EXPECT_EQ(mountingManager.findNodeAtPoint(kSurfaceTag, boxCentre()).tag, kSurfaceTag);
    EXPECT_EQ(translatedHit.tag, kBoxTag);
    EXPECT_FLOAT_EQ(translatedHit.origin.x, boxFrame().origin.x + static_cast<float>(kTranslation));
    EXPECT_FLOAT_EQ(translatedHit.origin.y, boxFrame().origin.y);
}

// The one-computation claim of #97: the origin a press reports and the geometry the painter draws are the same
// matrix applied to the same frame, so changing either half alone fails here.
TEST(AnimatedHitTestTest, ThePressedOriginIsThePaintedFrameMappedByThePaintedMatrix) {
    LinuxMountingManager mountingManager;

    mountBox(mountingManager);
    mountingManager.synchronouslyUpdateViewOnUIThread(kBoxTag, translationProps(kTranslation));

    const ScenePrimitive painted = mountingManager.snapshotScene().at(0);
    const Point translatedCentre = pointAt(boxCentre().x + static_cast<float>(kTranslation), boxCentre().y);
    const SceneHit hit = mountingManager.findNodeAtPoint(kSurfaceTag, translatedCentre);

    EXPECT_FLOAT_EQ(hit.origin.x, (painted.matrix.scaleX * painted.frame.origin.x) +
                                      (painted.matrix.skewX * painted.frame.origin.y) + painted.matrix.translateX);
    EXPECT_FLOAT_EQ(hit.origin.y, (painted.matrix.skewY * painted.frame.origin.x) +
                                      (painted.matrix.scaleY * painted.frame.origin.y) + painted.matrix.translateY);
}

/**
 * The injected clock the animated node graph is stepped with. The timestamp function is process-global, so the
 * default is restored afterwards for whatever runs next in this binary.
 */
class AnimatedFrameAgreementTest : public testing::Test {
protected:
    void SetUp() override {
        frameTime() = std::chrono::steady_clock::time_point{};
        facebook::react::g_setNativeAnimatedNowTimestampFunction(&frameTimeNow);
    }

    void TearDown() override {
        facebook::react::g_setNativeAnimatedNowTimestampFunction(&std::chrono::steady_clock::now);
    }

    static void advanceOneFrame() {
        frameTime() += std::chrono::microseconds(static_cast<int64_t>(kFrameIntervalMilliseconds * 1000.0));
    }
};

TEST_F(AnimatedFrameAgreementTest, EveryTickLeavesTheSceneAtTheValueTheDriverJustHandedOver) {
    LinuxMountingManager mountingManager;

    mountBox(mountingManager);

    std::vector<double> handedOverTranslations;
    size_t fabricCommitCount = 0;
    const std::shared_ptr<NativeAnimatedNodesManager> nodesManager = std::make_shared<NativeAnimatedNodesManager>(
        [&handedOverTranslations, &mountingManager](Tag tag, const folly::dynamic& props) {
            handedOverTranslations.push_back(props["transform"][0]["translateX"].asDouble());
            mountingManager.synchronouslyUpdateViewOnUIThread(tag, props);
        },
        [&fabricCommitCount](std::unordered_map<Tag, folly::dynamic>& /*updates*/) { ++fabricCommitCount; },
        nullptr);

    // The batch AnimatedModule::finishOperationBatch would have queued: the graph is built and the animation
    // started on the render thread, inside the first tick, exactly as it is in the window.
    NativeAnimatedNodesManager* manager = nodesManager.get();

    nodesManager->scheduleOnUI([manager]() {
        manager->createAnimatedNode(kValueNodeTag,
                                    folly::dynamic::object("type", "value")("value", 0)("offset", 0));
        manager->createAnimatedNode(
            kTransformNodeTag,
            folly::dynamic::object("type", "transform")(
                "transforms", folly::dynamic::array(folly::dynamic::object("type", "animated")(
                                  "property", "translateX")("nodeTag", kValueNodeTag))));
        manager->createAnimatedNode(
            kStyleNodeTag,
            folly::dynamic::object("type", "style")("style",
                                                    folly::dynamic::object("transform", kTransformNodeTag)));
        manager->createAnimatedNode(
            kPropsNodeTag,
            folly::dynamic::object("type", "props")("props", folly::dynamic::object("style", kStyleNodeTag)));
        manager->connectAnimatedNodes(kValueNodeTag, kTransformNodeTag);
        manager->connectAnimatedNodes(kTransformNodeTag, kStyleNodeTag);
        manager->connectAnimatedNodes(kStyleNodeTag, kPropsNodeTag);
        manager->connectAnimatedNodeToView(kPropsNodeTag, kBoxTag);
        manager->startAnimatingNode(
            kAnimationId, kValueNodeTag,
            folly::dynamic::object("type", "frames")("frames", folly::dynamic::array(0.0, 0.5, 1.0))(
                "toValue", kTranslationEnd),
            std::nullopt);
    });

    std::vector<size_t> commitsPerTick;

    for (size_t tick = 0; tick < kTickCount; ++tick) {
        const size_t commitsBeforeTick = fabricCommitCount;

        nodesManager->onRender();
        advanceOneFrame();
        commitsPerTick.push_back(fabricCommitCount - commitsBeforeTick);

        ASSERT_EQ(handedOverTranslations.size(), tick + 1);

        const double handedOver = handedOverTranslations.back();
        const ScenePrimitive painted = mountingManager.snapshotScene().at(0);
        const Point paintedCentre = pointAt(boxCentre().x + static_cast<float>(handedOver), boxCentre().y);
        const SceneHit hit = mountingManager.findNodeAtPoint(kSurfaceTag, paintedCentre);

        EXPECT_FLOAT_EQ(painted.matrix.translateX, static_cast<float>(handedOver));
        EXPECT_EQ(hit.tag, kBoxTag);
        EXPECT_FLOAT_EQ(hit.origin.x, boxFrame().origin.x + static_cast<float>(handedOver));
    }

    // The commit contract, which is the whole of what #121 is about. No commit while the animation is in flight:
    // every one of those frames is the scene fast path and nothing else. Exactly one on the tick the frames driver
    // reports complete, because `updateNodes` hands the finished value node's downstream `PropsAnimatedNode` to
    // `update(forceFabricCommit = true)` — upstream's react-native#43374 end-of-animation re-sync, which is what
    // eventually puts the settled value back where `measure()` reads it. The last tick is the completing one
    // because `FrameAnimationDriver::update` rounds the elapsed time to a frame index and a three-entry ramp runs
    // out on the third; the final value being exactly `toValue` is what pins that down.
    EXPECT_EQ(commitsPerTick, (std::vector<size_t>{0, 0, 1}));
    EXPECT_EQ(fabricCommitCount, 1U);
    EXPECT_DOUBLE_EQ(handedOverTranslations.back(), kTranslationEnd);
    EXPECT_EQ(mountingManager.mountDiagnostics().rejectedAnimatedProps, 0U);
    EXPECT_EQ(mountingManager.mountDiagnostics().unknownTagOperations, 0U);
}

} // namespace
