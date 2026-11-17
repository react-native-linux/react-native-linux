#include "DimensionsSource.h"
#include "ModalHostComponent.h"
#include "RecordingEventDispatcher.h"
#include "SceneTestSupport.h"

#include <gtest/gtest.h>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <react/renderer/components/modal/ModalHostViewShadowNode.h>
#include <react/renderer/components/modal/ModalHostViewState.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/renderer/core/ShadowNodeFragment.h>
#include <react/renderer/core/StateUpdate.h>
#include <react/utils/ContextContainer.h>

// ADR-0005's first slice, below the window: `ModalHostView` as an overlay of the retained scene — painted after the
// surface's tree from the surface origin, hit-tested before it, and gone from the hit path in the commit that
// removes it — and the Linux descriptor that sizes it from `DimensionsSource` on its first commit and on a resize.
// The pictures are `modal-backdrop.png` and `modal-scrim.png`; a press on the scrim reaching nothing beneath it is
// the `modal-scrim-press` e2e scenario.

namespace {

using facebook::react::ModalHostViewComponentName;
using facebook::react::ModalHostViewShadowNode;
using facebook::react::ModalHostViewState;
using facebook::react::ShadowNode;
using react_native_linux::DimensionsSource;
using react_native_linux::ModalHostComponentDescriptor;
using react_native_linux::SceneHit;

using ShadowNodes = std::vector<std::shared_ptr<const ShadowNode>>;

constexpr Size kSurfaceSize{.width = 800, .height = 600};
constexpr Tag kParentTag = 2;
constexpr Tag kSiblingTag = 3;
constexpr Tag kModalTag = 10;
constexpr Tag kBackdropTag = 11;

ShadowView makeModal(Tag tag, Rect frame) {
    ShadowView shadowView = makeView(tag, frame);

    shadowView.componentName = ModalHostViewComponentName;
    shadowView.surfaceId = kSurfaceTag;

    return shadowView;
}

/**
 * The fixture both goldens draw: a parent at (100, 100) that clips, is scaled to half and translated, is half
 * transparent, and is painted over by a later sibling — Fabric's order for a sibling with the higher `zIndex`. A
 * modal declared inside it is laid out at (20, 30) in that parent.
 */
RetainedScene sceneWithModalInsideATransformedClippedParent() {
    RetainedScene scene;
    const std::shared_ptr<ViewProps> parentProps = propsWithBackground(blue());

    parentProps->yogaStyle.setOverflow(facebook::yoga::Overflow::Hidden);
    parentProps->transform = Transform::Translate(40, 40, 0) * Transform::Scale(0.5, 0.5, 1);
    parentProps->opacity = 0.5;

    scene.createSurfaceRoot(kSurfaceTag, kSurfaceSize);
    addChild(scene, kSurfaceTag, makeStyledView(kParentTag, makeRect(100, 100, 50, 50), parentProps));
    scene.createNode(makePaintedView(kSiblingTag, makeRect(0, 0, 800, 600), red()));
    scene.insertChild(kSurfaceTag, makePaintedView(kSiblingTag, makeRect(0, 0, 800, 600), red()), 1);
    addChild(scene, kParentTag, makeModal(kModalTag, makeRect(20, 30, 800, 600)));
    addChild(scene, kModalTag, makePaintedView(kBackdropTag, makeRect(0, 0, 800, 600), blue()));

    return scene;
}

TEST(ModalOverlayTest, AnOverlayPaintsLastFromTheSurfaceOriginIgnoringItsAncestors) {
    const SceneSnapshot snapshot = sceneWithModalInsideATransformedClippedParent().snapshot();

    ASSERT_FALSE(snapshot.empty());

    const ScenePrimitive& top = snapshot.back();

    EXPECT_EQ(top.tag, kBackdropTag);
    EXPECT_EQ(top.frame, makeRect(0, 0, 800, 600));
    EXPECT_EQ(top.matrix.scaleX, 1.0F);
    EXPECT_EQ(top.matrix.translateX, 0.0F);
    EXPECT_TRUE(top.clips.empty());
    EXPECT_EQ(top.backgroundColorArgb, kBlueArgb);
    EXPECT_TRUE(top.opensLayers.empty());
    EXPECT_EQ(snapshot.at(snapshot.size() - 2).tag, kSiblingTag);
}

TEST(ModalOverlayTest, AnOverlayIsHitTestedFromTheSurfaceOriginIgnoringItsAncestors) {
    const RetainedScene scene = sceneWithModalInsideATransformedClippedParent();

    // Far outside the parent's clip, and over the sibling painted above the parent.
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, Point{.x = 700, .y = 500}).tag, kBackdropTag);
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, Point{.x = 1, .y = 1}).tag, kBackdropTag);
}

RetainedScene sceneWithModalOverASibling(Rect modalFrame) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, kSurfaceSize);
    addChild(scene, kSurfaceTag, makePaintedView(kSiblingTag, makeRect(0, 0, 800, 600), red()));
    addChild(scene, kSurfaceTag, makeModal(kModalTag, modalFrame));

    return scene;
}

TEST(ModalOverlayTest, APointThatMissesEveryChildLandsOnTheTopmostOverlayNotBeneathIt) {
    RetainedScene scene = sceneWithModalOverASibling(makeRect(0, 0, 800, 600));

    addChild(scene, kModalTag, makePaintedView(kBackdropTag, makeRect(300, 200, 200, 100), blue()));

    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, Point{.x = 350, .y = 250}).tag, kBackdropTag);
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, Point{.x = 50, .y = 50}).tag, kModalTag);
}

TEST(ModalOverlayTest, AnOverlayStillBlocksThePointsItsOwnFrameDoesNotCover) {
    const RetainedScene scene = sceneWithModalOverASibling(makeRect(0, 0, 0, 0));
    const SceneHit hit = scene.findNodeAtPoint(kSurfaceTag, Point{.x = 400, .y = 300});

    EXPECT_EQ(hit.tag, kModalTag);
    EXPECT_EQ(hit.origin, (Point{.x = 0, .y = 0}));
}

TEST(ModalOverlayTest, OverlaysStackInMountOrderAndTheTopmostAnswers) {
    RetainedScene scene;
    constexpr Tag kSecondModalTag = 20;
    constexpr Tag kSecondBackdropTag = 21;

    scene.createSurfaceRoot(kSurfaceTag, kSurfaceSize);
    addChild(scene, kSurfaceTag, makeModal(kModalTag, makeRect(0, 0, 800, 600)));
    addChild(scene, kModalTag, makePaintedView(kBackdropTag, makeRect(0, 0, 800, 600), blue()));
    addChild(scene, kSurfaceTag, makeModal(kSecondModalTag, makeRect(0, 0, 800, 600)));
    addChild(scene, kSecondModalTag, makePaintedView(kSecondBackdropTag, makeRect(0, 0, 400, 300), red()));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_EQ(snapshot.at(0).tag, kBackdropTag);
    EXPECT_EQ(snapshot.at(1).tag, kSecondBackdropTag);
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, Point{.x = 600, .y = 500}).tag, kSecondModalTag);

    scene.removeChild(kSurfaceTag, makeModal(kSecondModalTag, makeRect(0, 0, 800, 600)));

    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, Point{.x = 600, .y = 500}).tag, kBackdropTag);
}

TEST(ModalOverlayTest, ANestedModalInsertedBeforeItsOuterModalStillStacksAboveIt) {
    RetainedScene scene;
    constexpr Tag kInnerModalTag = 20;
    constexpr Tag kInnerBackdropTag = 21;

    // Fabric's order for a new subtree: every node is created, the inner inserts land, and the outer modal's own
    // insert comes last.
    scene.createSurfaceRoot(kSurfaceTag, kSurfaceSize);
    scene.createNode(makeModal(kModalTag, makeRect(0, 0, 800, 600)));
    addChild(scene, kModalTag, makePaintedView(kBackdropTag, makeRect(0, 0, 800, 600), blue()));
    addChild(scene, kBackdropTag, makeModal(kInnerModalTag, makeRect(0, 0, 800, 600)));
    addChild(scene, kInnerModalTag, makePaintedView(kInnerBackdropTag, makeRect(100, 100, 100, 100), red()));
    scene.insertChild(kSurfaceTag, makeModal(kModalTag, makeRect(0, 0, 800, 600)), 0);

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_EQ(snapshot.at(0).tag, kBackdropTag);
    EXPECT_EQ(snapshot.at(1).tag, kInnerBackdropTag);
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, Point{.x = 500, .y = 500}).tag, kInnerModalTag);
}

TEST(ModalOverlayTest, DamageInsideAnOverlayIsInSurfaceCoordinates) {
    RetainedScene scene = sceneWithModalInsideATransformedClippedParent();

    scene.takeDamage();
    scene.updateNode(makePaintedView(kBackdropTag, makeRect(10, 20, 30, 40), red()));

    const SceneDamage damage = scene.takeDamage();

    EXPECT_EQ(damage, (SceneDamage{makeRect(0, 0, 800, 600), makeRect(10, 20, 30, 40)}));
}

MountingTransaction presentTransaction(Tag modalTag) {
    return transactionOf(
        {ShadowViewMutation::CreateMutation(makeModal(modalTag, makeRect(0, 0, 800, 600))),
         ShadowViewMutation::InsertMutation(kSurfaceTag, makeModal(modalTag, makeRect(0, 0, 800, 600)), 1)});
}

MountingTransaction dismissTransaction(Tag modalTag) {
    return transactionOf(
        {ShadowViewMutation::RemoveMutation(kSurfaceTag, makeModal(modalTag, makeRect(0, 0, 800, 600)), 1),
         ShadowViewMutation::DeleteMutation(makeModal(modalTag, makeRect(0, 0, 800, 600)))});
}

LinuxMountingManager& mountedSurfaceWithASibling(LinuxMountingManager& mountingManager) {
    mountingManager.startSurface(kSurfaceTag, kSurfaceSize);
    mountingManager.executeMount(
        kSurfaceTag,
        transactionOf(
            {ShadowViewMutation::CreateMutation(makePaintedView(kSiblingTag, makeRect(0, 0, 800, 600), red())),
             ShadowViewMutation::InsertMutation(kSurfaceTag,
                                                makePaintedView(kSiblingTag, makeRect(0, 0, 800, 600), red()), 0)}));

    return mountingManager;
}

TEST(ModalOverlayTest, APresentAndRemoveInOneTransactionLeavesNothingInTheHitPath) {
    LinuxMountingManager mountingManager;
    MountingTransaction present = presentTransaction(kModalTag);
    ShadowViewMutationList mutations = present.getMutations();

    for (const ShadowViewMutation& mutation : dismissTransaction(kModalTag).getMutations()) {
        mutations.push_back(mutation);
    }

    mountedSurfaceWithASibling(mountingManager).executeMount(kSurfaceTag, transactionOf(std::move(mutations)));

    EXPECT_EQ(mountingManager.findNodeAtPoint(kSurfaceTag, Point{.x = 400, .y = 300}).tag, kSiblingTag);
}

TEST(ModalOverlayTest, RapidPresentsAndRemovesAcrossCommitsLeaveNothingInTheHitPath) {
    LinuxMountingManager mountingManager;

    mountedSurfaceWithASibling(mountingManager);
    EXPECT_FALSE(mountingManager.hasOverlays());

    for (Tag modalTag = kModalTag; modalTag < kModalTag + 4; ++modalTag) {
        mountingManager.executeMount(kSurfaceTag, presentTransaction(modalTag));

        EXPECT_EQ(mountingManager.findNodeAtPoint(kSurfaceTag, Point{.x = 400, .y = 300}).tag, modalTag);
        EXPECT_TRUE(mountingManager.hasOverlays());

        mountingManager.executeMount(kSurfaceTag, dismissTransaction(modalTag));

        EXPECT_EQ(mountingManager.findNodeAtPoint(kSurfaceTag, Point{.x = 400, .y = 300}).tag, kSiblingTag);
        EXPECT_FALSE(mountingManager.hasOverlays());
    }
}

TEST(ModalOverlayTest, AModalDeletedWithTheSubtreeAroundItLeavesTheHitPath) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, kSurfaceSize);
    addChild(scene, kSurfaceTag, makePaintedView(kSiblingTag, makeRect(0, 0, 800, 600), red()));
    addChild(scene, kSurfaceTag, makeView(kParentTag, makeRect(0, 0, 10, 10)));
    addChild(scene, kParentTag, makeModal(kModalTag, makeRect(0, 0, 800, 600)));
    scene.removeChild(kSurfaceTag, makeView(kParentTag, makeRect(0, 0, 10, 10)));
    scene.deleteNode(kParentTag);
    scene.deleteNode(kModalTag);

    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, Point{.x = 400, .y = 300}).tag, kSiblingTag);
}

/** A descriptor over a `DimensionsSource` in its context container, and the state updates its families dispatch. */
class ModalHostDescriptorTest : public ::testing::Test {
protected:
    ModalHostDescriptorTest() {
        dimensions_->configure(1024, 700, 1);
        contextContainer_->insert(react_native_linux::kDimensionsSourceContextKey,
                                  std::shared_ptr<const DimensionsSource>(dimensions_));
    }

    std::shared_ptr<const ModalHostViewShadowNode> makeModalNode(Tag tag, ShadowNodes children = {}) {
        const facebook::react::ShadowNodeFamily::Shared family =
            descriptor_.createFamily({.tag = tag, .surfaceId = kSurfaceTag, .instanceHandle = nullptr});

        return std::static_pointer_cast<const ModalHostViewShadowNode>(descriptor_.createShadowNode(
            facebook::react::ShadowNodeFragment{
                .props = ModalHostViewShadowNode::defaultSharedProps(),
                .children = std::make_shared<const ShadowNodes>(std::move(children)),
                .state = descriptor_.createInitialState(ModalHostViewShadowNode::defaultSharedProps(), family)},
            family));
    }

    std::shared_ptr<const ShadowNode> makeViewNode(Tag tag, ShadowNodes children) {
        const facebook::react::ShadowNodeFamily::Shared family =
            viewDescriptor_.createFamily({.tag = tag, .surfaceId = kSurfaceTag, .instanceHandle = nullptr});

        return viewDescriptor_.createShadowNode(
            facebook::react::ShadowNodeFragment{.props = facebook::react::ViewShadowNode::defaultSharedProps(),
                                                .children = std::make_shared<const ShadowNodes>(std::move(children))},
            family);
    }

    std::vector<Size> takeRequestedSizes() {
        std::vector<Size> sizes;

        for (const facebook::react::StateUpdate& stateUpdate : *stateUpdates_) {
            const facebook::react::StateData::Shared previous = std::make_shared<const ModalHostViewState>(Size{});

            sizes.push_back(
                std::static_pointer_cast<const ModalHostViewState>(stateUpdate.callback(previous))->screenSize);
        }

        stateUpdates_->clear();

        return sizes;
    }

    std::shared_ptr<DimensionsSource> dimensions_{std::make_shared<DimensionsSource>()};
    std::shared_ptr<const facebook::react::ContextContainer> contextContainer_{
        std::make_shared<const facebook::react::ContextContainer>()};
    std::shared_ptr<std::vector<facebook::react::StateUpdate>> stateUpdates_{
        std::make_shared<std::vector<facebook::react::StateUpdate>>()};
    std::shared_ptr<const facebook::react::EventDispatcher> eventDispatcher_{
        react_native_linux::makeListeningEventDispatcher(
            [](const facebook::react::RawEvent& /*event*/) { return true; },
            [stateUpdates = stateUpdates_](const facebook::react::StateUpdate& stateUpdate) {
                stateUpdates->push_back(stateUpdate);
            })};
    ModalHostComponentDescriptor descriptor_{facebook::react::ComponentDescriptorParameters{
        .eventDispatcher = eventDispatcher_, .contextContainer = contextContainer_, .flavor = nullptr}};
    facebook::react::ViewComponentDescriptor viewDescriptor_{facebook::react::ComponentDescriptorParameters{
        .eventDispatcher = eventDispatcher_, .contextContainer = contextContainer_, .flavor = nullptr}};
};

TEST_F(ModalHostDescriptorTest, TheFirstCommitIsAlreadySurfaceSized) {
    const std::shared_ptr<const ModalHostViewShadowNode> modal = makeModalNode(kModalTag);
    constexpr float kUnbounded = std::numeric_limits<float>::infinity();

    EXPECT_EQ(modal->getStateData().screenSize, (Size{.width = 1024, .height = 700}));
    EXPECT_EQ(modal->measure(
                  facebook::react::LayoutContext{},
                  facebook::react::LayoutConstraints{.minimumSize = Size{.width = 0, .height = 0},
                                                     .maximumSize = Size{.width = kUnbounded, .height = kUnbounded}}),
              (Size{.width = 1024, .height = 700}));
}

TEST_F(ModalHostDescriptorTest, AResizeUpdatesEveryModalNotAlreadyAtTheNewSize) {
    const std::shared_ptr<const ModalHostViewShadowNode> nested = makeModalNode(kModalTag + 1);
    const std::shared_ptr<const ModalHostViewShadowNode> outer =
        makeModalNode(kModalTag, {makeViewNode(kBackdropTag, {nested})});

    dimensions_->configure(640, 480, 1);

    const std::shared_ptr<const ModalHostViewShadowNode> alreadyResized = makeModalNode(kModalTag + 2);
    const std::shared_ptr<const ShadowNode> root = makeViewNode(kParentTag, {outer, alreadyResized});

    react_native_linux::resizePresentedModals(*root, Size{.width = 640, .height = 480});

    EXPECT_EQ(takeRequestedSizes(), (std::vector<Size>{{.width = 640, .height = 480}, {.width = 640, .height = 480}}));
}

/**
 * The third half of the resize contract: a modal created before a resize's configure, so with the old size, and
 * committed after the resize's walk, so out of its reach. Its mount asks for the current size at the next beat;
 * an immediate update of another modal is what flushes that beat's queue here.
 */
TEST_F(ModalHostDescriptorTest, AModalCreatedBeforeAResizeAndMountedAfterItsWalkAsksForTheNewSize) {
    const std::shared_ptr<const ModalHostViewShadowNode> createdBeforeTheResize = makeModalNode(kModalTag);
    const std::shared_ptr<const ModalHostViewShadowNode> flushing = makeModalNode(kModalTag + 1);
    LinuxMountingManager mountingManager;
    const ShadowView modal{*createdBeforeTheResize};
    const ShadowView sibling = makeView(kSiblingTag, makeRect(0, 0, 10, 10));

    mountingManager.setDimensionsSource(dimensions_);
    mountingManager.startSurface(kSurfaceTag, kSurfaceSize);
    dimensions_->configure(640, 480, 1);
    react_native_linux::resizePresentedModals(*makeViewNode(kParentTag, {}), Size{.width = 640, .height = 480});
    mountingManager.executeMount(kSurfaceTag,
                                 transactionOf({ShadowViewMutation::CreateMutation(sibling),
                                                ShadowViewMutation::InsertMutation(kSurfaceTag, sibling, 0),
                                                ShadowViewMutation::CreateMutation(modal),
                                                ShadowViewMutation::InsertMutation(kSurfaceTag, modal, 1)}));

    ASSERT_TRUE(stateUpdates_->empty());

    react_native_linux::resizePresentedModals(*flushing, Size{.width = 640, .height = 480});

    ASSERT_EQ(stateUpdates_->size(), 2U);
    EXPECT_EQ(stateUpdates_->front().family->getTag(), kModalTag);
    EXPECT_EQ(takeRequestedSizes(), (std::vector<Size>{{.width = 640, .height = 480}, {.width = 640, .height = 480}}));
}

} // namespace
