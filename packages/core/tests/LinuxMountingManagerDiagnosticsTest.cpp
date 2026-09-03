#include "LinuxMountingManager.h"
#include "SceneTestSupport.h"

#include <gtest/gtest.h>

#include <folly/dynamic.h>

#include <string>
#include <utility>
#include <vector>

// Issue #125: a mounting instruction that names a tag the scene does not hold used to be a silent skip, and
// `dispatchCommand` used to be empty. Both are contracts now — see *Commit termination and mounting atomicity* in
// docs/cpp-toolchain.md — and this file is what holds them. The fixture builders and the shared `using`
// declarations come from `SceneTestSupport.h`.

namespace {

using react_native_linux::MountDiagnostics;
using react_native_linux::SceneCommand;

constexpr Tag kMountedTag = 2;
constexpr Tag kUnknownTag = 404;
constexpr Tag kOtherUnknownTag = 505;
constexpr MountingTransaction::Number kMountTransaction = 7;
constexpr MountingTransaction::Number kLaterTransaction = 8;

// `SceneTestSupport.h`'s `transactionOf` numbers every transaction 1; these tests assert on the number the
// diagnostics record, so they need to choose it.
MountingTransaction numberedTransactionOf(MountingTransaction::Number number, ShadowViewMutationList&& mutations) {
    return MountingTransaction{kSurfaceTag, number, std::move(mutations),
                               facebook::react::TransactionTelemetry{}};
}

// One painted child mounted under the surface root, which is what every command case and the well-formed
// mutation case start from.
ShadowView mountChild(LinuxMountingManager& mountingManager) {
    const ShadowView child = makePaintedView(kMountedTag, makeRect(0, 0, 10, 10), blue());
    ShadowViewMutationList mutations;

    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});
    mutations.push_back(ShadowViewMutation::CreateMutation(child));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceTag, child, 0));
    mountingManager.executeMount(kSurfaceTag, numberedTransactionOf(kMountTransaction, std::move(mutations)));

    return child;
}

// The offending mutation the multi-mutation cases lead with: a delete naming a tag the scene never mounted.
ShadowViewMutation deleteOfUnknownTag(Tag tag) {
    return ShadowViewMutation::DeleteMutation(makePaintedView(tag, makeRect(0, 0, 10, 10), blue()));
}

// One mutation against a surface that holds nothing but its root, which is the whole arrange of every
// missing-tag case.
MountDiagnostics diagnosticsAfter(ShadowViewMutation mutation) {
    LinuxMountingManager mountingManager;
    ShadowViewMutationList mutations;

    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});
    mutations.push_back(std::move(mutation));
    mountingManager.executeMount(kSurfaceTag, numberedTransactionOf(kLaterTransaction, std::move(mutations)));

    return mountingManager.mountDiagnostics();
}

void appendFrame(std::vector<std::string>& sequence, LinuxMountingManager& mountingManager) {
    const SceneFrame frame = mountingManager.takeFrame();

    for (const ScenePrimitive& primitive : frame.scene) {
        sequence.push_back("mounted " + std::to_string(static_cast<int>(primitive.frame.size.width)) + "x" +
                           std::to_string(static_cast<int>(primitive.frame.size.height)));
    }
}

void appendCommands(std::vector<std::string>& sequence, LinuxMountingManager& mountingManager) {
    for (const SceneCommand& command : mountingManager.takeCommands()) {
        sequence.push_back("command " + command.name + " #" + std::to_string(command.tag));
    }
}

TEST(LinuxMountingManagerDiagnosticsTest, AFreshManagerHasNothingToReport) {
    const LinuxMountingManager mountingManager;
    const MountDiagnostics diagnostics = mountingManager.mountDiagnostics();

    EXPECT_EQ(diagnostics.unknownTagOperations, 0U);
    EXPECT_EQ(diagnostics.firstUnknownOperation, "");
    EXPECT_EQ(diagnostics.firstUnknownTag, 0);
    EXPECT_EQ(diagnostics.firstUnknownTransactionNumber, 0);
}

TEST(LinuxMountingManagerDiagnosticsTest, AWellFormedTransactionReportsNothing) {
    LinuxMountingManager mountingManager;
    const ShadowView child = mountChild(mountingManager);
    ShadowViewMutationList mutations;

    mutations.push_back(ShadowViewMutation::UpdateMutation(
        child, makePaintedView(kMountedTag, makeRect(0, 0, 20, 20), blue()), kSurfaceTag));
    mutations.push_back(ShadowViewMutation::RemoveMutation(kSurfaceTag, child, 0));
    mutations.push_back(ShadowViewMutation::DeleteMutation(child));
    mountingManager.executeMount(kSurfaceTag, numberedTransactionOf(kLaterTransaction, std::move(mutations)));

    EXPECT_EQ(mountingManager.mountDiagnostics().unknownTagOperations, 0U);
    EXPECT_TRUE(mountingManager.snapshotScene().empty());
}

TEST(LinuxMountingManagerDiagnosticsTest, AnUpdateForAnUnknownTagIsCountedWithItsTagAndTransaction) {
    const ShadowView orphan = makePaintedView(kUnknownTag, makeRect(0, 0, 10, 10), blue());
    const MountDiagnostics diagnostics =
        diagnosticsAfter(ShadowViewMutation::UpdateMutation(orphan, orphan, kSurfaceTag));

    EXPECT_EQ(diagnostics.unknownTagOperations, 1U);
    EXPECT_EQ(diagnostics.firstUnknownOperation, "Update");
    EXPECT_EQ(diagnostics.firstUnknownTag, kUnknownTag);
    EXPECT_EQ(diagnostics.firstUnknownTransactionNumber, kLaterTransaction);
}

TEST(LinuxMountingManagerDiagnosticsTest, ARemoveForAnUnknownTagIsCountedWithItsTagAndTransaction) {
    const ShadowView orphan = makePaintedView(kUnknownTag, makeRect(0, 0, 10, 10), blue());
    const MountDiagnostics diagnostics = diagnosticsAfter(ShadowViewMutation::RemoveMutation(kSurfaceTag, orphan, 0));

    EXPECT_EQ(diagnostics.unknownTagOperations, 1U);
    EXPECT_EQ(diagnostics.firstUnknownOperation, "Remove");
    EXPECT_EQ(diagnostics.firstUnknownTag, kUnknownTag);
    EXPECT_EQ(diagnostics.firstUnknownTransactionNumber, kLaterTransaction);
}

TEST(LinuxMountingManagerDiagnosticsTest, ADeleteForAnUnknownTagIsCountedWithItsTagAndTransaction) {
    const MountDiagnostics diagnostics = diagnosticsAfter(deleteOfUnknownTag(kUnknownTag));

    EXPECT_EQ(diagnostics.unknownTagOperations, 1U);
    EXPECT_EQ(diagnostics.firstUnknownOperation, "Delete");
    EXPECT_EQ(diagnostics.firstUnknownTag, kUnknownTag);
    EXPECT_EQ(diagnostics.firstUnknownTransactionNumber, kLaterTransaction);
}

TEST(LinuxMountingManagerDiagnosticsTest, EveryUnknownTagIsCountedAndOnlyTheFirstIsKept) {
    LinuxMountingManager mountingManager;
    ShadowViewMutationList mutations;

    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});
    mutations.push_back(deleteOfUnknownTag(kUnknownTag));
    mutations.push_back(deleteOfUnknownTag(kOtherUnknownTag));
    mountingManager.executeMount(kSurfaceTag, numberedTransactionOf(kLaterTransaction, std::move(mutations)));

    const MountDiagnostics diagnostics = mountingManager.mountDiagnostics();

    EXPECT_EQ(diagnostics.unknownTagOperations, 2U);
    EXPECT_EQ(diagnostics.firstUnknownTag, kUnknownTag);
}

TEST(LinuxMountingManagerDiagnosticsTest, AnUnknownTagNeverStopsTheRestOfTheTransaction) {
    LinuxMountingManager mountingManager;
    const ShadowView child = makePaintedView(kMountedTag, makeRect(24, 24, 120, 80), blue());
    ShadowViewMutationList mutations;

    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});
    mutations.push_back(deleteOfUnknownTag(kUnknownTag));
    mutations.push_back(ShadowViewMutation::CreateMutation(child));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceTag, child, 0));
    mountingManager.executeMount(kSurfaceTag, numberedTransactionOf(kLaterTransaction, std::move(mutations)));

    EXPECT_EQ(mountingManager.mountDiagnostics().unknownTagOperations, 1U);
    EXPECT_EQ(mountingManager.snapshotScene().size(), 1U);
}

TEST(LinuxMountingManagerCommandTest, ACommandForAKnownTagIsQueuedWithItsArgumentsAndReportsNothing) {
    LinuxMountingManager mountingManager;

    mountingManager.dispatchCommand(mountChild(mountingManager), "scrollToEnd", folly::dynamic::array(true));

    const std::vector<SceneCommand> queued = mountingManager.takeCommands();

    ASSERT_EQ(queued.size(), 1U);
    EXPECT_EQ(queued[0].tag, kMountedTag);
    EXPECT_EQ(queued[0].name, "scrollToEnd");
    ASSERT_EQ(queued[0].args.size(), 1U);
    EXPECT_TRUE(queued[0].args.at(0).asBool());
    EXPECT_EQ(mountingManager.mountDiagnostics().unknownTagOperations, 0U);
}

TEST(LinuxMountingManagerCommandTest, TakingCommandsEmptiesTheQueueAndPreservesArrivalOrder) {
    LinuxMountingManager mountingManager;
    const ShadowView child = mountChild(mountingManager);

    mountingManager.dispatchCommand(child, "focus", folly::dynamic::array());
    mountingManager.dispatchCommand(child, "blur", folly::dynamic::array());

    const std::vector<SceneCommand> commands = mountingManager.takeCommands();

    ASSERT_EQ(commands.size(), 2U);
    EXPECT_EQ(commands[0].name, "focus");
    EXPECT_EQ(commands[1].name, "blur");
    EXPECT_TRUE(mountingManager.takeCommands().empty());
}

TEST(LinuxMountingManagerCommandTest, ACommandForAnUnknownTagIsReportedAndStillDelivered) {
    LinuxMountingManager mountingManager;

    mountChild(mountingManager);
    mountingManager.dispatchCommand(makePaintedView(kUnknownTag, makeRect(0, 0, 1, 1), blue()), "focus",
                                    folly::dynamic::array());

    const MountDiagnostics diagnostics = mountingManager.mountDiagnostics();

    EXPECT_EQ(diagnostics.unknownTagOperations, 1U);
    EXPECT_EQ(diagnostics.firstUnknownOperation, "Command");
    EXPECT_EQ(diagnostics.firstUnknownTag, kUnknownTag);
    EXPECT_EQ(diagnostics.firstUnknownTransactionNumber, kMountTransaction);
    EXPECT_EQ(mountingManager.takeCommands().size(), 1U);
}

TEST(LinuxMountingManagerCommandTest, ACommandIsObservedAfterTheTransactionBeforeItAndBeforeTheNextOne) {
    LinuxMountingManager mountingManager;
    const ShadowView child = mountChild(mountingManager);
    std::vector<std::string> sequence;

    appendFrame(sequence, mountingManager);

    mountingManager.dispatchCommand(child, "scrollToEnd", folly::dynamic::array());

    appendCommands(sequence, mountingManager);

    ShadowViewMutationList update;

    update.push_back(ShadowViewMutation::UpdateMutation(
        child, makePaintedView(kMountedTag, makeRect(0, 0, 20, 20), blue()), kSurfaceTag));
    mountingManager.executeMount(kSurfaceTag, numberedTransactionOf(kLaterTransaction, std::move(update)));

    appendFrame(sequence, mountingManager);

    EXPECT_EQ(sequence, (std::vector<std::string>{"mounted 10x10", "command scrollToEnd #2", "mounted 20x20"}));
}

} // namespace
