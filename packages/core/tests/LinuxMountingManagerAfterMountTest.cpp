#include "LinuxMountingManager.h"

#include <cstddef>
#include <gtest/gtest.h>
#include <vector>

#include <react/renderer/mounting/MountingTransaction.h>

namespace {

using facebook::react::MountingTransaction;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::SurfaceId;
using react_native_linux::LinuxMountingManager;

constexpr SurfaceId kSurfaceId = 11;
constexpr facebook::react::Tag kTag = 12;

MountingTransaction transactionOf(ShadowViewMutation::List&& mutations) {
    return MountingTransaction{kSurfaceId, 1, std::move(mutations), facebook::react::TransactionTelemetry{}};
}

/**
 * The after-mount callback is what `FabricHost` points at `Scheduler::reportMount` (#210): it hears the surface the
 * transaction mounted, once the transaction is in the scene and the scene lock is released. The callback reads the
 * scene through the same lock, so a callback run under it would deadlock this case rather than fail it.
 */
TEST(LinuxMountingManagerAfterMountTest, TheCallbackHearsTheSurfaceOnceItsTransactionIsInTheUnlockedScene) {
    LinuxMountingManager mountingManager;
    std::vector<SurfaceId> reportedSurfaces;
    std::size_t nodesSeenByTheCallback = 0;

    mountingManager.setAfterMountCallback([&](SurfaceId surfaceId) {
        reportedSurfaces.push_back(surfaceId);
        nodesSeenByTheCallback = mountingManager.visualTreeNodes().size();
    });

    ShadowView shadowView;
    ShadowViewMutation::List mutations;

    shadowView.tag = kTag;
    mutations.push_back(ShadowViewMutation::CreateMutation(shadowView));
    mountingManager.executeMount(kSurfaceId, transactionOf(std::move(mutations)));

    EXPECT_EQ(reportedSurfaces, std::vector<SurfaceId>{kSurfaceId});
    EXPECT_EQ(nodesSeenByTheCallback, 1U);
}

} // namespace
