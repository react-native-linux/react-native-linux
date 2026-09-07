#include "AutomationProtocol.h"
#include "LinuxMountingManager.h"
#include "SceneTestSupport.h"

#include <gtest/gtest.h>

#include <react/renderer/components/view/AccessibilityPrimitives.h>
#include <react/renderer/components/view/ViewProps.h>

#include <memory>
#include <vector>

// #264: an `accessibilityState`/`accessibilityValue` change is the smallest observable of "an AT-SPI
// `state-changed`/`property-change` event would fire here" this platform can assert on before the AT-SPI bridge
// (#27) exists. This is the change-detection table `executeMount`'s `Update` case has to satisfy: a real change
// is recorded once, an unrelated `Update` records nothing, and a remount — `Delete` then `Create`, never an
// `Update` — is not mistaken for one, because a remount is a new identity and not the update this channel is
// about. See *The accessibility tree as an assertion surface* in docs/cpp-toolchain.md.

namespace {

using facebook::react::AccessibilityState;
using facebook::react::AccessibilityValue;
using react_native_linux::AccessibilityChange;

constexpr facebook::react::Tag kMountedTag = 2;

std::shared_ptr<ViewProps> propsWithState(AccessibilityState::CheckedState checked) {
    const std::shared_ptr<ViewProps> viewProps = std::make_shared<ViewProps>();

    viewProps->accessible = true;
    viewProps->accessibilityState = AccessibilityState{.checked = checked};

    return viewProps;
}

std::shared_ptr<ViewProps> propsWithValue(int now) {
    const std::shared_ptr<ViewProps> viewProps = std::make_shared<ViewProps>();

    viewProps->accessible = true;
    viewProps->accessibilityValue = AccessibilityValue{.now = now};

    return viewProps;
}

void mountWithAccessibilityProps(LinuxMountingManager& mountingManager, const std::shared_ptr<ViewProps>& initialProps) {
    const ShadowView child = makeStyledView(kMountedTag, makeRect(0, 0, 10, 10), initialProps);
    ShadowViewMutationList mutations{ShadowViewMutation::CreateMutation(child),
                                     ShadowViewMutation::InsertMutation(kSurfaceTag, child, 0)};

    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));
    mountingManager.takeAccessibilityChanges();
}

// Mounts a checked node and hands back the `ShadowView` an `Update` mutation would carry as its `oldChildShadowView`
// — the shared starting point for every case that mounts once and then asks what one more `Update` does to it.
ShadowView mountCheckedView(LinuxMountingManager& mountingManager) {
    const std::shared_ptr<ViewProps> checkedProps = propsWithState(AccessibilityState::CheckedState::Checked);

    mountWithAccessibilityProps(mountingManager, checkedProps);

    return makeStyledView(kMountedTag, makeRect(0, 0, 10, 10), checkedProps);
}

// The `Update` mutation a toggle produces: same tag, same frame, `accessibilityState.checked` moved from `from`
// to `to`. `mountingManager` must already hold a node mounted with `propsWithState(from)`.
ShadowViewMutation toggleCheckedMutation(AccessibilityState::CheckedState from, AccessibilityState::CheckedState to) {
    const ShadowView previous = makeStyledView(kMountedTag, makeRect(0, 0, 10, 10), propsWithState(from));

    return ShadowViewMutation::UpdateMutation(
        previous, makeStyledView(kMountedTag, makeRect(0, 0, 10, 10), propsWithState(to)), kSurfaceTag);
}

// Mounts unchecked, then toggles to checked in its own commit — the one real change every case in this file that
// asks what recording a change looks like starts from.
void mountAndToggleChecked(LinuxMountingManager& mountingManager) {
    mountWithAccessibilityProps(mountingManager, propsWithState(AccessibilityState::CheckedState::Unchecked));
    mountingManager.executeMount(
        kSurfaceTag, transactionOf({toggleCheckedMutation(AccessibilityState::CheckedState::Unchecked,
                                                          AccessibilityState::CheckedState::Checked)}));
}

TEST(LinuxMountingManagerAccessibilityChangesTest, StartsWithNoChanges) {
    LinuxMountingManager mountingManager;

    EXPECT_TRUE(mountingManager.takeAccessibilityChanges().empty());
}

TEST(LinuxMountingManagerAccessibilityChangesTest, ACheckedStateChangeIsRecordedOnTheSameTag) {
    LinuxMountingManager mountingManager;

    mountAndToggleChecked(mountingManager);

    const std::vector<AccessibilityChange> changes = mountingManager.takeAccessibilityChanges();

    ASSERT_EQ(changes.size(), 1U);
    EXPECT_EQ(changes[0].tag, kMountedTag);
    EXPECT_TRUE(changes[0].stateChanged);
    EXPECT_FALSE(changes[0].valueChanged);
}

TEST(LinuxMountingManagerAccessibilityChangesTest, AValueChangeIsRecordedWithoutAStateChange) {
    LinuxMountingManager mountingManager;
    mountWithAccessibilityProps(mountingManager, propsWithValue(1));
    const ShadowView previous = makeStyledView(kMountedTag, makeRect(0, 0, 10, 10), propsWithValue(1));

    mountingManager.executeMount(kSurfaceTag,
                                 transactionOf({ShadowViewMutation::UpdateMutation(
                                     previous, makeStyledView(kMountedTag, makeRect(0, 0, 10, 10), propsWithValue(2)),
                                     kSurfaceTag)}));

    const std::vector<AccessibilityChange> changes = mountingManager.takeAccessibilityChanges();

    ASSERT_EQ(changes.size(), 1U);
    EXPECT_EQ(changes[0].tag, kMountedTag);
    EXPECT_FALSE(changes[0].stateChanged);
    EXPECT_TRUE(changes[0].valueChanged);
}

TEST(LinuxMountingManagerAccessibilityChangesTest, AStateAndValueChangeInOneCommitReportsBoth) {
    const std::shared_ptr<ViewProps> initialProps = propsWithState(AccessibilityState::CheckedState::Unchecked);

    initialProps->accessibilityValue = AccessibilityValue{.now = 1};

    LinuxMountingManager mountingManager;
    mountWithAccessibilityProps(mountingManager, initialProps);
    const ShadowView previous = makeStyledView(kMountedTag, makeRect(0, 0, 10, 10), initialProps);
    const std::shared_ptr<ViewProps> nextProps = propsWithState(AccessibilityState::CheckedState::Checked);

    nextProps->accessibilityValue = AccessibilityValue{.now = 2};

    mountingManager.executeMount(
        kSurfaceTag, transactionOf({ShadowViewMutation::UpdateMutation(
                        previous, makeStyledView(kMountedTag, makeRect(0, 0, 10, 10), nextProps), kSurfaceTag)}));

    const std::vector<AccessibilityChange> changes = mountingManager.takeAccessibilityChanges();

    ASSERT_EQ(changes.size(), 1U);
    EXPECT_TRUE(changes[0].stateChanged);
    EXPECT_TRUE(changes[0].valueChanged);
}

TEST(LinuxMountingManagerAccessibilityChangesTest, AnUpdateThatLeavesStateAndValueAloneRecordsNothing) {
    LinuxMountingManager mountingManager;
    const ShadowView previous = mountCheckedView(mountingManager);

    // Only the frame differs — a plain relayout, not an accessibility change.
    mountingManager.executeMount(
        kSurfaceTag, transactionOf({ShadowViewMutation::UpdateMutation(
                        previous,
                        makeStyledView(kMountedTag, makeRect(0, 0, 20, 20),
                                      propsWithState(AccessibilityState::CheckedState::Checked)),
                        kSurfaceTag)}));

    EXPECT_TRUE(mountingManager.takeAccessibilityChanges().empty());
}

TEST(LinuxMountingManagerAccessibilityChangesTest, AnUpdateCarryingNoViewPropsRecordsNothing) {
    LinuxMountingManager mountingManager;
    const ShadowView previous = mountCheckedView(mountingManager);
    ShadowView bare = makeView(kMountedTag, makeRect(0, 0, 10, 10));

    mountingManager.executeMount(kSurfaceTag,
                                 transactionOf({ShadowViewMutation::UpdateMutation(previous, bare, kSurfaceTag)}));

    EXPECT_TRUE(mountingManager.takeAccessibilityChanges().empty());
}

TEST(LinuxMountingManagerAccessibilityChangesTest, AnUpdateForATagTheSceneDoesNotHoldRecordsNothing) {
    LinuxMountingManager mountingManager;
    const ShadowView orphan = makeStyledView(kMountedTag, makeRect(0, 0, 10, 10),
                                             propsWithState(AccessibilityState::CheckedState::Checked));

    mountingManager.executeMount(kSurfaceTag,
                                 transactionOf({ShadowViewMutation::UpdateMutation(orphan, orphan, kSurfaceTag)}));

    EXPECT_TRUE(mountingManager.takeAccessibilityChanges().empty());
}

TEST(LinuxMountingManagerAccessibilityChangesTest, ARemountIsNotMistakenForAStateChange) {
    LinuxMountingManager mountingManager;
    mountWithAccessibilityProps(mountingManager, propsWithState(AccessibilityState::CheckedState::Unchecked));
    const ShadowView remounted =
        makeStyledView(kMountedTag, makeRect(0, 0, 10, 10), propsWithState(AccessibilityState::CheckedState::Checked));

    mountingManager.executeMount(
        kSurfaceTag,
        transactionOf({ShadowViewMutation::DeleteMutation(makeStyledView(
                           kMountedTag, makeRect(0, 0, 10, 10), propsWithState(AccessibilityState::CheckedState::Unchecked))),
                       ShadowViewMutation::CreateMutation(remounted),
                       ShadowViewMutation::InsertMutation(kSurfaceTag, remounted, 0)}));

    EXPECT_TRUE(mountingManager.takeAccessibilityChanges().empty());
}

// Mounts unchecked with `testID: "toggle"`, toggles to checked in its own commit, and hands back the checked
// `ShadowView` an owning test can later remove — the shared starting point for the testID-capture cases below.
ShadowView mountAndToggleCheckedWithTestId(LinuxMountingManager& mountingManager) {
    const std::shared_ptr<ViewProps> initialProps = propsWithState(AccessibilityState::CheckedState::Unchecked);

    initialProps->testId = "toggle";
    mountWithAccessibilityProps(mountingManager, initialProps);

    const std::shared_ptr<ViewProps> checkedProps = propsWithState(AccessibilityState::CheckedState::Checked);

    checkedProps->testId = "toggle";

    const ShadowView checked = makeStyledView(kMountedTag, makeRect(0, 0, 10, 10), checkedProps);

    mountingManager.executeMount(
        kSurfaceTag, transactionOf({ShadowViewMutation::UpdateMutation(
                        makeStyledView(kMountedTag, makeRect(0, 0, 10, 10), initialProps), checked, kSurfaceTag)}));

    return checked;
}

TEST(LinuxMountingManagerAccessibilityChangesTest, ACheckedStateChangeCapturesTheMutationsTestId) {
    LinuxMountingManager mountingManager;

    mountAndToggleCheckedWithTestId(mountingManager);

    const std::vector<AccessibilityChange> changes = mountingManager.takeAccessibilityChanges();

    ASSERT_EQ(changes.size(), 1U);
    EXPECT_EQ(changes[0].testId, "toggle");
}

TEST(LinuxMountingManagerAccessibilityChangesTest, AChangeRecordedBeforeARemovalStillReportsTheOriginalTestId) {
    LinuxMountingManager mountingManager;
    const ShadowView removed = mountAndToggleCheckedWithTestId(mountingManager);
    const std::vector<AccessibilityChange> changes = mountingManager.takeAccessibilityChanges();

    ASSERT_EQ(changes.size(), 1U);

    // The node is removed after the change was recorded — the scene no longer has a "toggle" tag to look up by
    // the time this change is described, but the change already captured its own testID at record time.
    mountingManager.executeMount(kSurfaceTag, transactionOf({ShadowViewMutation::RemoveMutation(kSurfaceTag, removed, 0),
                                                             ShadowViewMutation::DeleteMutation(removed)}));

    EXPECT_EQ(changes[0].testId, "toggle");
}

TEST(LinuxMountingManagerAccessibilityChangesTest, DrainingEmptiesTheQueue) {
    LinuxMountingManager mountingManager;

    mountAndToggleChecked(mountingManager);

    EXPECT_EQ(mountingManager.takeAccessibilityChanges().size(), 1U);
    EXPECT_TRUE(mountingManager.takeAccessibilityChanges().empty());
}

} // namespace
