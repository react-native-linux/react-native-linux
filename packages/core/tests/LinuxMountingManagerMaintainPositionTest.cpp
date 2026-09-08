#include "LinuxMountingManager.h"
#include "SceneTestSupport.h"

#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <vector>

#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/components/scrollview/primitives.h>

// `maintainVisibleContentPosition` as a property of the mounting transaction rather than of the frame that
// follows it (#292): the scene `executeMount` produces already holds the content where the prepended children
// leave it, so there is no displaced frame in between for a window to paint. The number itself is
// `maintainedScrollOffset`, which has its own table in ScrollTest.cpp; what these assert is when it is applied,
// against what, and who is told. See *Holding the visible content still* in docs/cpp-toolchain.md.
namespace {

using facebook::react::Point;
using facebook::react::Rect;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::ShadowViewMutationList;
using react_native_linux::LinuxMountingManager;
using react_native_linux::MaintainedScrollOffset;
using react_native_linux::SceneSnapshot;

constexpr facebook::react::Tag kScrollViewTag = 2;
constexpr facebook::react::Tag kContentViewTag = 3;
constexpr facebook::react::Tag kFirstRowTag = 10;
constexpr facebook::react::Tag kFirstPrependedRowTag = 20;
constexpr facebook::react::Tag kSecondPrependedRowTag = 30;
constexpr float kRowHeight = 100.0F;
constexpr float kViewportHeight = 150.0F;
constexpr float kViewportWidth = 200.0F;
constexpr int kRowCount = 3;
constexpr int kPrependedRowCount = 2;

Rect scrollViewFrame() { return makeRect(0, 0, kViewportWidth, kViewportHeight); }

Rect contentBounds(int rowCount) { return makeRect(0, 0, kViewportWidth, static_cast<float>(rowCount) * kRowHeight); }

Rect rowFrame(int position) {
    return makeRect(0, static_cast<float>(position) * kRowHeight, kViewportWidth, kRowHeight);
}

/**
 * The ScrollView as the transaction carries it: the offset in `ScrollViewState`, the prop in `ScrollViewProps`.
 * `maintaining` false is the same node with the prop taken off, which is what an application turning the feature
 * off commits.
 */
ShadowView scrollView(Point contentOffset, int rowCount, bool maintaining,
                      std::optional<int> autoscrollToTopThreshold = std::nullopt) {
    ShadowView shadowView = makeScrollView(kScrollViewTag, scrollViewFrame(), contentOffset, contentBounds(rowCount));
    const std::shared_ptr<facebook::react::ScrollViewProps> props =
        std::make_shared<facebook::react::ScrollViewProps>();

    if (maintaining) {
        facebook::react::ScrollViewMaintainVisibleContentPosition parsed;

        parsed.minIndexForVisible = 0;
        parsed.autoscrollToTopThreshold = autoscrollToTopThreshold;
        props->maintainVisibleContentPosition = parsed;
    }

    shadowView.props = props;

    return shadowView;
}

void appendCreateAndInsert(ShadowViewMutationList& mutations, facebook::react::Tag parentTag, const ShadowView& child,
                           int index) {
    mutations.push_back(ShadowViewMutation::CreateMutation(child));
    mutations.push_back(ShadowViewMutation::InsertMutation(parentTag, child, index));
}

// The parent is a parameter rather than a constant because the whole point of the flattening case is that these
// same rows hang off the content view in one shape and off the ScrollView itself in the other.
void appendRows(ShadowViewMutationList& mutations, facebook::react::Tag parentTag) {
    for (int position = 0; position < kRowCount; position++) {
        appendCreateAndInsert(mutations, parentTag,
                              makePaintedView(kFirstRowTag + position, rowFrame(position), blue()), position);
    }
}

void appendPrependedRows(ShadowViewMutationList& mutations, facebook::react::Tag parentTag,
                         facebook::react::Tag firstTag) {
    for (int position = 0; position < kPrependedRowCount; position++) {
        appendCreateAndInsert(mutations, parentTag, makePaintedView(firstTag + position, rowFrame(position), red()),
                              position);
    }
}

void appendPushedDownRows(ShadowViewMutationList& mutations, facebook::react::Tag parentTag) {
    for (int position = 0; position < kRowCount; position++) {
        mutations.push_back(ShadowViewMutation::UpdateMutation(
            makePaintedView(kFirstRowTag + position, rowFrame(position), blue()),
            makePaintedView(kFirstRowTag + position, rowFrame(position + kPrependedRowCount), blue()), parentTag));
    }
}

void appendGrownScrollView(ShadowViewMutationList& mutations, Point contentOffset, bool maintaining,
                           std::optional<int> autoscrollToTopThreshold = std::nullopt) {
    mutations.push_back(ShadowViewMutation::UpdateMutation(
        scrollView(contentOffset, kRowCount, maintaining, autoscrollToTopThreshold),
        scrollView(contentOffset, kRowCount + kPrependedRowCount, maintaining, autoscrollToTopThreshold), kSurfaceTag));
}

/**
 * The first commit: a maintaining ScrollView holding a content view holding `kRowCount` rows, scrolled to
 * `contentOffset`.
 */
void mountRows(LinuxMountingManager& mountingManager, Point contentOffset, bool maintaining,
               std::optional<int> autoscrollToTopThreshold = std::nullopt) {
    ShadowViewMutationList mutations;

    appendCreateAndInsert(mutations, kSurfaceTag,
                          scrollView(contentOffset, kRowCount, maintaining, autoscrollToTopThreshold), 0);
    appendCreateAndInsert(mutations, kScrollViewTag, makeView(kContentViewTag, contentBounds(kRowCount)), 0);
    appendRows(mutations, kContentViewTag);
    mountingManager.startSurface(kSurfaceTag, facebook::react::Size{.width = kViewportWidth, .height = 600});
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));
    mountingManager.takeMaintainedScrollOffsets();
}

/**
 * The commit under test: `kPrependedRowCount` rows arrive above the ones already there, which pushes every old row
 * down by exactly their height and grows the content view by it. Fabric re-lays-out what moved, so the transaction
 * carries an `Update` for every old row and for the container, exactly as this builds it.
 */
ShadowViewMutationList prependMutations(Point contentOffset, bool maintaining,
                                        std::optional<int> autoscrollToTopThreshold = std::nullopt) {
    ShadowViewMutationList mutations;

    appendPrependedRows(mutations, kContentViewTag, kFirstPrependedRowTag);
    mutations.push_back(ShadowViewMutation::UpdateMutation(
        makeView(kContentViewTag, contentBounds(kRowCount)),
        makeView(kContentViewTag, contentBounds(kRowCount + kPrependedRowCount)), kScrollViewTag));
    appendPushedDownRows(mutations, kContentViewTag);
    appendGrownScrollView(mutations, contentOffset, maintaining, autoscrollToTopThreshold);

    return mutations;
}

std::vector<MaintainedScrollOffset> prepend(LinuxMountingManager& mountingManager, Point contentOffset,
                                            bool maintaining,
                                            std::optional<int> autoscrollToTopThreshold = std::nullopt) {
    mountingManager.executeMount(kSurfaceTag,
                                 transactionOf(prependMutations(contentOffset, maintaining, autoscrollToTopThreshold)));

    return mountingManager.takeMaintainedScrollOffsets();
}

TEST(LinuxMountingManagerMaintainPositionTest, TheSceneThePrependProducesAlreadyHoldsTheVisibleRowsStill) {
    LinuxMountingManager mountingManager;

    mountRows(mountingManager, Point{.x = 0, .y = 100}, true);

    const SceneSnapshot before = mountingManager.snapshotScene();
    const std::vector<MaintainedScrollOffset> maintained = prepend(mountingManager, Point{.x = 0, .y = 100}, true);

    // No frame, no beat, no `advanceScroll` between the mount and this snapshot: what a window painting here would
    // have shown is what the mounting transaction left, and every row that was on screen is still in its place.
    EXPECT_FALSE(react_native_linux::findDisplacedPrimitive(before, mountingManager.snapshotScene()).has_value());

    ASSERT_EQ(maintained.size(), 1U);
    EXPECT_EQ(maintained.front().tag, kScrollViewTag);
    EXPECT_EQ(maintained.front().offset, (Point{.x = 0, .y = 300}));
}

TEST(LinuxMountingManagerMaintainPositionTest, AScrollViewThatHasNeverBeenScrolledIsAdjustedToo) {
    LinuxMountingManager mountingManager;

    // No controller has ever touched this one, so before #292 nothing was watching its children at all. Resting at
    // the top is not the same as having nothing to hold still: the first row is what the reader is looking at.
    mountRows(mountingManager, Point{}, true);

    const SceneSnapshot before = mountingManager.snapshotScene();
    const std::vector<MaintainedScrollOffset> maintained = prepend(mountingManager, Point{}, true);

    EXPECT_FALSE(react_native_linux::findDisplacedPrimitive(before, mountingManager.snapshotScene()).has_value());

    ASSERT_EQ(maintained.size(), 1U);
    EXPECT_EQ(maintained.front().offset, (Point{.x = 0, .y = 200}));
}

TEST(LinuxMountingManagerMaintainPositionTest, AutoscrollToTopThresholdTakesTheNewTopInstead) {
    LinuxMountingManager mountingManager;

    mountRows(mountingManager, Point{.x = 0, .y = 100}, true, 200);

    const std::vector<MaintainedScrollOffset> maintained = prepend(mountingManager, Point{.x = 0, .y = 100}, true, 200);

    ASSERT_EQ(maintained.size(), 1U);
    EXPECT_EQ(maintained.front().offset, (Point{}));
}

TEST(LinuxMountingManagerMaintainPositionTest, AScrollViewWithoutThePropIsNotAdjusted) {
    LinuxMountingManager mountingManager;

    mountRows(mountingManager, Point{.x = 0, .y = 100}, false);

    EXPECT_TRUE(prepend(mountingManager, Point{.x = 0, .y = 100}, false).empty());
}

TEST(LinuxMountingManagerMaintainPositionTest, TurningThePropOffForgetsTheChildrenItWasWatching) {
    LinuxMountingManager mountingManager;

    mountRows(mountingManager, Point{.x = 0, .y = 100}, true);

    // The commit that turns the prop off is also the commit that prepends, so nothing may be adjusted for it.
    EXPECT_TRUE(prepend(mountingManager, Point{.x = 0, .y = 100}, false).empty());

    // Back on, against the children it is turned back on with. Measuring those against the ones from before it was
    // turned off would adjust the offset by the prepend a second time.
    const int rowCount = kRowCount + kPrependedRowCount;
    ShadowViewMutationList mutations;

    mutations.push_back(ShadowViewMutation::UpdateMutation(scrollView(Point{.x = 0, .y = 100}, rowCount, false),
                                                           scrollView(Point{.x = 0, .y = 100}, rowCount, true),
                                                           kSurfaceTag));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));

    EXPECT_TRUE(mountingManager.takeMaintainedScrollOffsets().empty());
}

TEST(LinuxMountingManagerMaintainPositionTest, TwoPrependsBeforeTheStateCatchesUpCompound) {
    LinuxMountingManager mountingManager;

    mountRows(mountingManager, Point{.x = 0, .y = 100}, true);

    // The offset is written back through `updateState`, so the `ScrollViewState` the *second* prepend arrives with
    // still says 100: the write-back for the first is a commit that has not landed. Measuring the second from that
    // stale number would adjust 100 to 300 twice instead of reaching 500, and the reader would be moved back to
    // where the first prepend already left them.
    const std::vector<MaintainedScrollOffset> first = prepend(mountingManager, Point{.x = 0, .y = 100}, true);

    ASSERT_EQ(first.size(), 1U);
    EXPECT_EQ(first.front().offset, (Point{.x = 0, .y = 300}));

    const SceneSnapshot beforeSecond = mountingManager.snapshotScene();
    ShadowViewMutationList mutations;
    const int rowCount = kRowCount + kPrependedRowCount;

    appendPrependedRows(mutations, kContentViewTag, kSecondPrependedRowTag);
    mutations.push_back(ShadowViewMutation::UpdateMutation(
        makeView(kContentViewTag, contentBounds(rowCount)),
        makeView(kContentViewTag, contentBounds(rowCount + kPrependedRowCount)), kScrollViewTag));

    for (int position = 0; position < rowCount; position++) {
        const facebook::react::Tag movedTag = position < kPrependedRowCount
                                                  ? kFirstPrependedRowTag + position
                                                  : kFirstRowTag + position - kPrependedRowCount;

        mutations.push_back(ShadowViewMutation::UpdateMutation(
            makePaintedView(movedTag, rowFrame(position), blue()),
            makePaintedView(movedTag, rowFrame(position + kPrependedRowCount), blue()), kContentViewTag));
    }

    mutations.push_back(ShadowViewMutation::UpdateMutation(
        scrollView(Point{.x = 0, .y = 100}, rowCount, true),
        scrollView(Point{.x = 0, .y = 100}, rowCount + kPrependedRowCount, true), kSurfaceTag));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));

    const std::vector<MaintainedScrollOffset> second = mountingManager.takeMaintainedScrollOffsets();

    ASSERT_EQ(second.size(), 1U);
    EXPECT_EQ(second.front().offset, (Point{.x = 0, .y = 500}));
    EXPECT_FALSE(react_native_linux::findDisplacedPrimitive(beforeSecond, mountingManager.snapshotScene()).has_value());
}

TEST(LinuxMountingManagerMaintainPositionTest, TheStateCatchingUpRetiresTheAdoptedOffset) {
    LinuxMountingManager mountingManager;

    mountRows(mountingManager, Point{.x = 0, .y = 100}, true);
    prepend(mountingManager, Point{.x = 0, .y = 100}, true);

    // The write-back lands: the state now carries the offset the mount decided, so the scene has nothing left to
    // outrank and a later commit that moves the offset for its own reasons is believed again.
    const int rowCount = kRowCount + kPrependedRowCount;
    ShadowViewMutationList acknowledged;

    acknowledged.push_back(ShadowViewMutation::UpdateMutation(scrollView(Point{.x = 0, .y = 100}, rowCount, true),
                                                              scrollView(Point{.x = 0, .y = 300}, rowCount, true),
                                                              kSurfaceTag));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(acknowledged)));

    ShadowViewMutationList scrolledByJavaScript;

    scrolledByJavaScript.push_back(
        ShadowViewMutation::UpdateMutation(scrollView(Point{.x = 0, .y = 300}, rowCount, true),
                                           scrollView(Point{.x = 0, .y = 40}, rowCount, true), kSurfaceTag));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(scrolledByJavaScript)));

    EXPECT_EQ(mountingManager.dumpScene().find("contentOffset=(0.00, 40.00)") != std::string::npos, true);
}

TEST(LinuxMountingManagerMaintainPositionTest, AListOfOneRowIsAnchoredOnThatRow) {
    LinuxMountingManager mountingManager;
    ShadowViewMutationList mutations;

    // The content container is always in the mounting tree, so one row is one anchor rather than a node to look
    // inside of. Descending into it would find no children and maintain nothing.
    appendCreateAndInsert(mutations, kSurfaceTag, scrollView(Point{}, 1, true), 0);
    appendCreateAndInsert(mutations, kScrollViewTag, makeView(kContentViewTag, contentBounds(1)), 0);
    appendCreateAndInsert(mutations, kContentViewTag, makePaintedView(kFirstRowTag, rowFrame(0), blue()), 0);
    mountingManager.startSurface(kSurfaceTag, facebook::react::Size{.width = kViewportWidth, .height = 600});
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));
    mountingManager.takeMaintainedScrollOffsets();

    ShadowViewMutationList prepended;

    appendPrependedRows(prepended, kContentViewTag, kFirstPrependedRowTag);
    prepended.push_back(ShadowViewMutation::UpdateMutation(
        makeView(kContentViewTag, contentBounds(1)), makeView(kContentViewTag, contentBounds(1 + kPrependedRowCount)),
        kScrollViewTag));
    prepended.push_back(ShadowViewMutation::UpdateMutation(
        makePaintedView(kFirstRowTag, rowFrame(0), blue()),
        makePaintedView(kFirstRowTag, rowFrame(kPrependedRowCount), blue()), kContentViewTag));
    prepended.push_back(ShadowViewMutation::UpdateMutation(
        scrollView(Point{}, 1, true), scrollView(Point{}, 1 + kPrependedRowCount, true), kSurfaceTag));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(prepended)));

    const std::vector<MaintainedScrollOffset> maintained = mountingManager.takeMaintainedScrollOffsets();

    // The row moved 200 points down and the offset follows it, then clamps: three rows of content under a
    // 150-point viewport cannot scroll past 150, so the row is held as still as the new content allows. Reporting
    // nothing at all is what an implementation that looked *inside* the single child would do.
    ASSERT_EQ(maintained.size(), 1U);
    EXPECT_EQ(maintained.front().offset, (Point{.x = 0, .y = 150}));
}

TEST(LinuxMountingManagerMaintainPositionTest, AMaintainingScrollViewWithNoChildrenYetAdjustsNothing) {
    LinuxMountingManager mountingManager;
    ShadowViewMutationList mutations;

    appendCreateAndInsert(mutations, kSurfaceTag, scrollView(Point{.x = 0, .y = 100}, kRowCount, true), 0);
    mountingManager.startSurface(kSurfaceTag, facebook::react::Size{.width = kViewportWidth, .height = 600});
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));

    EXPECT_TRUE(mountingManager.takeMaintainedScrollOffsets().empty());
}

TEST(LinuxMountingManagerMaintainPositionTest, ARowDeletedWithoutBeingRemovedIsNotAnAnchorAndIsNotMeasured) {
    LinuxMountingManager mountingManager;

    mountRows(mountingManager, Point{.x = 0, .y = 100}, true);

    // A `Delete` without its `Remove` leaves the tag in the content view's child list — the missing-tag case the
    // mounting layer counts. The walk skips it rather than inventing a frame for it, so the anchor is gone and
    // nothing is adjusted.
    ShadowViewMutationList mutations;

    mutations.push_back(ShadowViewMutation::DeleteMutation(makePaintedView(kFirstRowTag + 1, rowFrame(1), blue())));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));

    EXPECT_TRUE(mountingManager.takeMaintainedScrollOffsets().empty());
}

TEST(LinuxMountingManagerMaintainPositionTest, AContentViewDeletedWithoutBeingRemovedLeavesNothingToMeasure) {
    LinuxMountingManager mountingManager;

    mountRows(mountingManager, Point{.x = 0, .y = 100}, true);

    ShadowViewMutationList mutations;

    mutations.push_back(ShadowViewMutation::DeleteMutation(makeView(kContentViewTag, contentBounds(kRowCount))));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));

    EXPECT_TRUE(mountingManager.takeMaintainedScrollOffsets().empty());
}

} // namespace
