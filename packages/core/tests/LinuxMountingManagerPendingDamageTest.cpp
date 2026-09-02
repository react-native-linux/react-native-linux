#include "LinuxMountingManager.h"

#include <gtest/gtest.h>

#include <react/renderer/mounting/MountingTransaction.h>

namespace {

using facebook::react::MountingTransaction;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using react_native_linux::LinuxMountingManager;
using react_native_linux::SceneEditorState;

constexpr facebook::react::SurfaceId kSurfaceId = 1;
constexpr facebook::react::Tag kTag = 2;

MountingTransaction transactionOf(ShadowViewMutation::List&& mutations) {
    return MountingTransaction{kSurfaceId, 1, std::move(mutations), facebook::react::TransactionTelemetry{}};
}

ShadowView taggedView() {
    ShadowView shadowView;

    shadowView.tag = kTag;

    return shadowView;
}

TEST(LinuxMountingManagerPendingDamageTest, StartsWithNoPendingDamage) {
    LinuxMountingManager mountingManager;

    EXPECT_FALSE(mountingManager.hasPendingDamage());
}

TEST(LinuxMountingManagerPendingDamageTest, AnEmptyTransactionLeavesItUnset) {
    LinuxMountingManager mountingManager;

    mountingManager.executeMount(kSurfaceId, transactionOf({}));

    EXPECT_FALSE(mountingManager.hasPendingDamage());
}

TEST(LinuxMountingManagerPendingDamageTest, ANonEmptyTransactionSetsItUntilTheFrameIsTaken) {
    LinuxMountingManager mountingManager;
    ShadowViewMutation::List mutations;

    mutations.push_back(ShadowViewMutation::CreateMutation(taggedView()));
    mountingManager.executeMount(kSurfaceId, transactionOf(std::move(mutations)));

    EXPECT_TRUE(mountingManager.hasPendingDamage());

    mountingManager.takeFrame();

    EXPECT_FALSE(mountingManager.hasPendingDamage());
}

TEST(LinuxMountingManagerPendingDamageTest, AnImageDecodeSetsIt) {
    LinuxMountingManager mountingManager;

    mountingManager.damageImageSource("https://example.test/image.png");

    EXPECT_TRUE(mountingManager.hasPendingDamage());
}

TEST(LinuxMountingManagerPendingDamageTest, AFocusChangeSetsIt) {
    LinuxMountingManager mountingManager;

    mountingManager.setFocus(kTag, true);

    EXPECT_TRUE(mountingManager.hasPendingDamage());
}

TEST(LinuxMountingManagerPendingDamageTest, AnEditorStatePublishSetsIt) {
    LinuxMountingManager mountingManager;

    mountingManager.setEditorState(kTag, SceneEditorState{});

    EXPECT_TRUE(mountingManager.hasPendingDamage());
}

} // namespace
