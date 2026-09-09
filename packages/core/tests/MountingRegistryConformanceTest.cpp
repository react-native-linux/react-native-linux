#include "LinuxMountingManager.h"
#include "SceneTestSupport.h"

#include <cstdint>
#include <folly/dynamic.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <utility>

#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/components/scrollview/primitives.h>

// Issue #419: the contracts upstream's `SurfaceMountingManagerTest`, `SurfaceMountingManagerEventOrderingTest`
// and `SurfaceMountingManagerSynchronousMountPropsTest` hold Android to, read against the registry this platform
// actually has. Two of the three translate; the third does not, and the difference is the point:
//
//  * Mount-item idempotence transfers whole. Ours has no `preallocateView` — `Create` is the first the scene
//    hears of a tag and `Insert` creates one too — so the upstream preallocation orderings collapse into the
//    create/delete orderings below.
//  * Per-tag event queueing has no counterpart here and must not grow one. Android queues events because a view
//    and its event emitter are registered separately across the bridge; in C++ Fabric the emitter rides on the
//    committed `ShadowNode`, so `InputDispatcher` reads it off the target it just resolved and there is no window
//    in which a tag exists without one. See `InputDispatcher`'s docblock.
//  * The synchronous-props override is inverted here, deliberately. Android keeps a per-tag store so an
//    animation beats a stale async batch; this platform writes animated props into the same `SceneNode` fields
//    the commit path writes and lets the settling commit re-sync them, because the driver performs zero commits
//    while the animation runs and one when it ends. The cases below pin that as the contract rather than
//    inheriting Android's.
//
// The fixture builders and the shared `using` declarations come from `SceneTestSupport.h`.

namespace {

using react_native_linux::MountDiagnostics;

constexpr Tag kChildTag = 2;
constexpr Rect kChildFrame{.origin = {.x = 0, .y = 0}, .size = {.width = 10, .height = 10}};

void startSurface(LinuxMountingManager& mountingManager) {
    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});
}

void mount(LinuxMountingManager& mountingManager, ShadowViewMutationList&& mutations) {
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));
}

ShadowView childPainted(SharedColor backgroundColor) {
    return makePaintedView(kChildTag, kChildFrame, backgroundColor);
}

// The tag mounted under the surface root, which every ordering case starts from.
ShadowView mountedChild(LinuxMountingManager& mountingManager) {
    const ShadowView child = childPainted(blue());
    ShadowViewMutationList mutations;

    mutations.push_back(ShadowViewMutation::CreateMutation(child));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceTag, child, 0));
    mount(mountingManager, std::move(mutations));

    return child;
}

// Every ordering case ends the same way: exactly one node mounted, painted the colour the last create named, and
// nothing the mounting layer could not explain.
void expectOnlyNodePaintedIs(LinuxMountingManager& mountingManager, uint32_t expectedBackgroundArgb) {
    const SceneSnapshot snapshot = mountingManager.snapshotScene();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot.at(0).backgroundColorArgb, expectedBackgroundArgb);
    EXPECT_EQ(mountingManager.mountDiagnostics().unknownTagOperations, 0U);
}

TEST(MountingRegistryIdempotenceTest, ACreateForATagTheSceneAlreadyHoldsRewritesItInPlace) {
    LinuxMountingManager mountingManager;

    startSurface(mountingManager);
    const ShadowView child = mountedChild(mountingManager);
    ShadowViewMutationList repeated;

    repeated.push_back(ShadowViewMutation::CreateMutation(childPainted(red())));
    repeated.push_back(ShadowViewMutation::CreateMutation(childPainted(red())));
    mount(mountingManager, std::move(repeated));

    expectOnlyNodePaintedIs(mountingManager, kRedArgb);
}

TEST(MountingRegistryIdempotenceTest, AnInsertMountsATagTheSceneWasNeverAskedToCreate) {
    LinuxMountingManager mountingManager;

    startSurface(mountingManager);
    ShadowViewMutationList mutations;

    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceTag, childPainted(blue()), 0));
    mount(mountingManager, std::move(mutations));

    expectOnlyNodePaintedIs(mountingManager, kBlueArgb);
}

TEST(MountingRegistryIdempotenceTest, ADeleteRepeatedForOneTagLeavesTheSceneEmptyAndOnlyCountsTheRepeat) {
    LinuxMountingManager mountingManager;

    startSurface(mountingManager);
    const ShadowView child = mountedChild(mountingManager);
    ShadowViewMutationList mutations;

    mutations.push_back(ShadowViewMutation::RemoveMutation(kSurfaceTag, child, 0));
    mutations.push_back(ShadowViewMutation::DeleteMutation(child));
    mutations.push_back(ShadowViewMutation::DeleteMutation(child));
    mutations.push_back(ShadowViewMutation::DeleteMutation(child));
    mount(mountingManager, std::move(mutations));

    const MountDiagnostics diagnostics = mountingManager.mountDiagnostics();

    EXPECT_TRUE(mountingManager.snapshotScene().empty());
    EXPECT_EQ(diagnostics.unknownTagOperations, 2U);
    EXPECT_EQ(diagnostics.firstUnknownOperation, "Delete");
    EXPECT_EQ(diagnostics.firstUnknownTag, kChildTag);
}

// The case a naive implementation gets wrong, and the reason issue #419 asks for it by name rather than folded
// into a larger ordering test: one tag removed, deleted, created and inserted again inside a single transaction
// has to end up mounted exactly once, carrying the props the last create named.
TEST(MountingRegistryIdempotenceTest, RemoveDeleteCreateInsertOfOneTagInOneTransactionRemountsItExactlyOnce) {
    LinuxMountingManager mountingManager;

    startSurface(mountingManager);
    const ShadowView child = mountedChild(mountingManager);
    const ShadowView recycled = childPainted(red());
    ShadowViewMutationList mutations;

    mutations.push_back(ShadowViewMutation::RemoveMutation(kSurfaceTag, child, 0));
    mutations.push_back(ShadowViewMutation::DeleteMutation(child));
    mutations.push_back(ShadowViewMutation::CreateMutation(recycled));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceTag, recycled, 0));
    mount(mountingManager, std::move(mutations));

    expectOnlyNodePaintedIs(mountingManager, kRedArgb);
}

// Regression for the defect this issue's ordering cases found: a `Delete` that never saw its `Remove` used to
// leave the surface root still naming the deleted tag. The entry was inert while the tag stayed gone, so nothing
// showed until a later `Create` reused it — and then the node was painted twice, once under the stale parent and
// once as a detached root, which is a wrong frame with nothing in the diagnostics to point at.
TEST(MountingRegistryIdempotenceTest, ADeleteThatNeverSawItsRemoveLeavesNothingBehindForALaterCreateToReuse) {
    LinuxMountingManager mountingManager;

    startSurface(mountingManager);
    const ShadowView child = mountedChild(mountingManager);
    ShadowViewMutationList bareDelete;

    bareDelete.push_back(ShadowViewMutation::DeleteMutation(child));
    mount(mountingManager, std::move(bareDelete));

    EXPECT_TRUE(mountingManager.snapshotScene().empty());

    ShadowViewMutationList bareCreate;

    bareCreate.push_back(ShadowViewMutation::CreateMutation(childPainted(red())));
    mount(mountingManager, std::move(bareCreate));

    expectOnlyNodePaintedIs(mountingManager, kRedArgb);
}

// Upstream's per-surface allocation registry, read as the one thing it means here: the scene is keyed by tag
// across surfaces, so tearing one surface's tree down must leave the other's mounted.
TEST(MountingRegistryIdempotenceTest, DeletingOneSurfacesTreeLeavesAnotherSurfacesMounted) {
    constexpr Tag kOtherSurfaceTag = 100;
    constexpr Tag kOtherChildTag = 101;

    LinuxMountingManager mountingManager;

    startSurface(mountingManager);
    const ShadowView child = mountedChild(mountingManager);
    const ShadowView otherChild = makePaintedView(kOtherChildTag, kChildFrame, red());
    ShadowViewMutationList otherMutations;

    mountingManager.startSurface(kOtherSurfaceTag, Size{.width = 400, .height = 300});
    otherMutations.push_back(ShadowViewMutation::CreateMutation(otherChild));
    otherMutations.push_back(ShadowViewMutation::InsertMutation(kOtherSurfaceTag, otherChild, 0));
    mountingManager.executeMount(kOtherSurfaceTag, MountingTransaction{kOtherSurfaceTag, 2, std::move(otherMutations),
                                                                       facebook::react::TransactionTelemetry{}});

    ShadowViewMutationList teardown;

    teardown.push_back(ShadowViewMutation::RemoveMutation(kSurfaceTag, child, 0));
    teardown.push_back(ShadowViewMutation::DeleteMutation(child));
    mount(mountingManager, std::move(teardown));

    expectOnlyNodePaintedIs(mountingManager, kRedArgb);
}

// A ScrollView that holds its visible content still, which is the one reader that walks a content view's children
// on every transaction rather than only when a frame is painted.
ShadowView maintainingScrollView(Tag tag, Rect frame) {
    ShadowView shadowView = makeScrollView(tag, frame, Point{.x = 0, .y = 0}, frame);
    const std::shared_ptr<facebook::react::ScrollViewProps> props =
        std::make_shared<facebook::react::ScrollViewProps>();
    facebook::react::ScrollViewMaintainVisibleContentPosition maintaining;

    maintaining.minIndexForVisible = 0;
    props->maintainVisibleContentPosition = maintaining;
    shadowView.props = props;

    return shadowView;
}

// A maintaining ScrollView holding one content view, which is the arrange both dangling-reference cases share.
// The ScrollView is what makes them worth asserting: its content walk runs at the end of every transaction,
// where the painted readers run only when a frame is taken.
constexpr Tag kScrollViewTag = 2;
constexpr Tag kContentViewTag = 3;
constexpr Tag kRowTag = 10;
constexpr Rect kViewport{.origin = {.x = 0, .y = 0}, .size = {.width = 200, .height = 150}};

void mountScrollViewWithContentView(LinuxMountingManager& mountingManager) {
    const ShadowView scrollView = maintainingScrollView(kScrollViewTag, kViewport);
    const ShadowView contentView = makeView(kContentViewTag, kViewport);
    ShadowViewMutationList mutations;

    startSurface(mountingManager);
    mutations.push_back(ShadowViewMutation::CreateMutation(scrollView));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceTag, scrollView, 0));
    mutations.push_back(ShadowViewMutation::CreateMutation(contentView));
    mutations.push_back(ShadowViewMutation::InsertMutation(kScrollViewTag, contentView, 0));
    mount(mountingManager, std::move(mutations));
}

// Inserting a tag under a second parent and then deleting it, which is the one ordering that still leaves the
// scene naming a node it no longer holds: the insert records only the second parent, so the delete unlinks that
// one and the first is left dangling.
void reparentAndDelete(LinuxMountingManager& mountingManager, const ShadowView& child) {
    ShadowViewMutationList mutations;

    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceTag, child, 1));
    mutations.push_back(ShadowViewMutation::DeleteMutation(child));
    mount(mountingManager, std::move(mutations));
}

// Every reader has to walk past that dangling reference: the painted snapshot, the dump, and the ScrollView
// content walk. This is why `deleteNode` unlinking from its recorded parent is not the whole story.
TEST(MountingRegistryIdempotenceTest, ATagInsertedUnderTwoParentsAndThenDeletedIsSkippedByEveryReader) {
    LinuxMountingManager mountingManager;
    const ShadowView row = makePaintedView(kRowTag, kChildFrame, blue());
    ShadowViewMutationList rowMutations;

    mountScrollViewWithContentView(mountingManager);
    rowMutations.push_back(ShadowViewMutation::CreateMutation(row));
    rowMutations.push_back(ShadowViewMutation::InsertMutation(kContentViewTag, row, 0));
    mount(mountingManager, std::move(rowMutations));

    reparentAndDelete(mountingManager, row);

    EXPECT_TRUE(mountingManager.snapshotScene().empty());
    EXPECT_EQ(mountingManager.dumpScene().find("#10"), std::string::npos);
    EXPECT_TRUE(mountingManager.takeMaintainedScrollOffsets().empty());
}

// The same dangling reference one level up: the content view is what the content walk reads first, so a content
// view taken out from under the ScrollView must leave it maintaining nothing rather than reading a node that is
// gone.
TEST(MountingRegistryIdempotenceTest, AMaintainingScrollViewWhoseContentViewWasReparentedAwayMaintainsNothing) {
    LinuxMountingManager mountingManager;

    mountScrollViewWithContentView(mountingManager);
    reparentAndDelete(mountingManager, makeView(kContentViewTag, kViewport));

    EXPECT_EQ(mountingManager.dumpScene().find("#3"), std::string::npos);
    EXPECT_TRUE(mountingManager.takeMaintainedScrollOffsets().empty());
}

// Android keeps synchronous opacity/transform in a per-tag store so a stale `updateProps` batch cannot clobber
// them. This platform does the opposite on purpose: the animated value is written into the same field the commit
// path writes, and the commit that ends the animation is what re-syncs it. Asserting it here is what stops the
// Android behaviour being ported in by reflex.
TEST(MountingRegistrySynchronousPropsTest, ACommitThatFollowsASynchronousUpdateReSyncsTheAnimatedProp) {
    LinuxMountingManager mountingManager;

    startSurface(mountingManager);
    const ShadowView child = mountedChild(mountingManager);

    mountingManager.synchronouslyUpdateViewOnUIThread(kChildTag, folly::dynamic::object("opacity", 0.5));

    const SceneSnapshot animated = mountingManager.snapshotScene();

    ASSERT_EQ(animated.size(), 1U);
    EXPECT_NE(animated.at(0).backgroundColorArgb, kBlueArgb);

    ShadowViewMutationList settling;

    settling.push_back(ShadowViewMutation::UpdateMutation(child, childPainted(blue()), kSurfaceTag));
    mount(mountingManager, std::move(settling));

    const SceneSnapshot settled = mountingManager.snapshotScene();

    ASSERT_EQ(settled.size(), 1U);
    EXPECT_EQ(settled.at(0).backgroundColorArgb, kBlueArgb);
}

// The half of upstream's store contract that does survive: deleting the tag drops its synchronous values, so a
// tag mounted again does not inherit the opacity an animation left on the node that used to hold it.
TEST(MountingRegistrySynchronousPropsTest, DeletingAnAnimatedTagDropsItsSynchronousValuesWithTheNode) {
    LinuxMountingManager mountingManager;

    startSurface(mountingManager);
    const ShadowView child = mountedChild(mountingManager);

    mountingManager.synchronouslyUpdateViewOnUIThread(kChildTag, folly::dynamic::object("opacity", 0.5));

    ShadowViewMutationList remount;

    remount.push_back(ShadowViewMutation::RemoveMutation(kSurfaceTag, child, 0));
    remount.push_back(ShadowViewMutation::DeleteMutation(child));
    remount.push_back(ShadowViewMutation::CreateMutation(childPainted(blue())));
    remount.push_back(ShadowViewMutation::InsertMutation(kSurfaceTag, childPainted(blue()), 0));
    mount(mountingManager, std::move(remount));

    const SceneSnapshot snapshot = mountingManager.snapshotScene();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot.at(0).backgroundColorArgb, kBlueArgb);
}

} // namespace
