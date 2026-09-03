#include "LinuxMountingManager.h"

#include <gtest/gtest.h>

#include <react/featureflags/ReactNativeFeatureFlags.h>
#include <react/featureflags/ReactNativeFeatureFlagsDefaults.h>
#include <react/renderer/components/root/RootShadowNode.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/components/view/ViewShadowNode.h>
#include <react/renderer/core/ComponentDescriptor.h>
#include <react/renderer/core/EventDispatcher.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/renderer/core/Props.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/core/ShadowNode.h>
#include <react/renderer/core/ShadowNodeFamily.h>
#include <react/renderer/core/ShadowNodeFragment.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/Size.h>
#include <react/renderer/mounting/MountingCoordinator.h>
#include <react/renderer/mounting/MountingTransaction.h>
#include <react/renderer/mounting/ShadowTree.h>
#include <react/renderer/mounting/ShadowTreeDelegate.h>
#include <react/renderer/mounting/ShadowViewMutation.h>
#include <react/utils/ContextContainer.h>

#include <algorithm>
#include <atomic>
#include <latch>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using facebook::react::ComponentDescriptorParameters;
using facebook::react::ContextContainer;
using facebook::react::EventDispatcher;
using facebook::react::LayoutConstraints;
using facebook::react::LayoutContext;
using facebook::react::MountingCoordinator;
using facebook::react::MountingTransaction;
using facebook::react::ReactNativeFeatureFlags;
using facebook::react::ReactNativeFeatureFlagsDefaults;
using facebook::react::RootShadowNode;
using facebook::react::ShadowNode;
using facebook::react::ShadowNodeFamily;
using facebook::react::ShadowNodeFragment;
using facebook::react::ShadowTree;
using facebook::react::ShadowTreeDelegate;
using facebook::react::ShadowViewMutation;
using facebook::react::Size;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::ViewComponentDescriptor;
using react_native_linux::LinuxMountingManager;

using ChildList = std::vector<std::shared_ptr<const ShadowNode>>;

constexpr SurfaceId kSurfaceId = 1;
constexpr Tag kRenderedTag = 100;
constexpr Tag kEffectTag = 101;
constexpr Tag kConcurrentBaseTag = 200;
constexpr int kConcurrentCommitCount = 64;
constexpr int kAttemptsBeforeLocking = 3;

/**
 * The `ShadowTreeDelegate` upstream's own mounting tests use: it accepts every proposed tree and lets the
 * `MountingCoordinator` be the only observer of a commit, which is what makes `pullTransaction` the thing under
 * test rather than a mount callback.
 */
class PassThroughShadowTreeDelegate final : public ShadowTreeDelegate {
public:
    RootShadowNode::Unshared shadowTreeWillCommit(const ShadowTree& /*shadowTree*/,
                                                  const RootShadowNode::Shared& /*oldRootShadowNode*/,
                                                  const RootShadowNode::Unshared& newRootShadowNode,
                                                  const facebook::react::ShadowTreeCommitOptions& /*commitOptions*/)
        const override {
        return newRootShadowNode;
    }

    void shadowTreeDidFinishTransaction(std::shared_ptr<const MountingCoordinator> /*mountingCoordinator*/,
                                        bool /*mountSynchronously*/) const override {}

    void shadowTreeDidFinishReactCommit(const ShadowTree& /*shadowTree*/) const override {}

    void shadowTreeDidPromoteReactRevision(const ShadowTree& /*shadowTree*/) const override {}
};

/**
 * `preventShadowTreeCommitExhaustion` is what turns `ShadowTree::commit`'s unbounded retry loop — the one guarded
 * only by `react_native_assert(attempts < 1024)` — into a bounded one that reports its outcome. It is off by
 * default upstream, so the test that proves termination has to turn it on.
 */
class ExhaustionPreventingFeatureFlags final : public ReactNativeFeatureFlagsDefaults {
public:
    bool preventShadowTreeCommitExhaustion() override {
        return true;
    }
};

/**
 * `<View>` props carrying an opaque background colour.
 *
 * The colour is not decoration: `ViewShadowNode::initialize` reads it through `isColorMeaningful` to decide
 * `ShadowNodeTraits::Trait::FormsView`, and a view built from `defaultSharedProps` forms none. The differ
 * flattens such a node away — `sliceChildShadowNodeViewPairs` is called with `allowFlattened = false` — so a tree
 * made of default-prop views produces a transaction with no mutations at all, and every assertion about what was
 * mounted reads as an empty scene. See *Fabric bootstrap* in docs/cpp-toolchain.md, which records the same
 * flattening for `packages/core/test-bundles/fabric-view.js`.
 */
facebook::react::Props::Shared paintedViewProps() {
    const std::shared_ptr<facebook::react::ViewProps> viewProps = std::make_shared<facebook::react::ViewProps>();

    viewProps->backgroundColor = facebook::react::colorFromRGBA(51, 102, 204, 255);

    return viewProps;
}

/**
 * Builds `<View>` shadow nodes without the upstream element-tree test helpers, which would drag
 * `react/renderer/element` and the modal component descriptor into this binary for two calls.
 */
class ViewFactory final {
public:
    std::shared_ptr<const ShadowNode> makeView(Tag tag) const {
        const ShadowNodeFamily::Shared family =
            descriptor_.createFamily({.tag = tag, .surfaceId = kSurfaceId, .instanceHandle = nullptr});

        return descriptor_.createShadowNode(ShadowNodeFragment{.props = paintedProps_}, family);
    }

private:
    std::shared_ptr<const ContextContainer> contextContainer_{std::make_shared<ContextContainer>()};
    ViewComponentDescriptor descriptor_{ComponentDescriptorParameters{.eventDispatcher = EventDispatcher::Shared{},
                                                                      .contextContainer = contextContainer_,
                                                                      .flavor = nullptr}};
    facebook::react::Props::Shared paintedProps_{paintedViewProps()};
};

RootShadowNode::Unshared cloneRootWithChild(const RootShadowNode& oldRootShadowNode,
                                            std::shared_ptr<const ShadowNode> child) {
    const std::shared_ptr<const ChildList> children = std::make_shared<const ChildList>(ChildList{std::move(child)});

    return std::static_pointer_cast<RootShadowNode>(oldRootShadowNode.ShadowNode::clone(
        ShadowNodeFragment{.props = ShadowNodeFragment::propsPlaceholder(), .children = children}));
}

ShadowTree::CommitOptions commitOptions() {
    return ShadowTree::CommitOptions{.enableStateReconciliation = false, .mountSynchronously = true};
}

/**
 * A commit transaction that lands another commit before it returns, which is the shape of a state update made
 * from a layout effect: by the time the outer commit tries to publish, the revision it read is no longer current
 * and `tryCommit` has to report `Failed`.
 */
facebook::react::ShadowTreeCommitTransaction transactionThatMovesTheBaseTree(const ShadowTree& shadowTree,
                                                                            const ViewFactory& viewFactory,
                                                                            int& transactionCalls) {
    return [&shadowTree, &viewFactory, &transactionCalls](const RootShadowNode& oldRootShadowNode) {
        transactionCalls++;
        shadowTree.commit(
            [&viewFactory](const RootShadowNode& innerOldRootShadowNode) {
                return cloneRootWithChild(innerOldRootShadowNode, viewFactory.makeView(kEffectTag));
            },
            commitOptions());

        return cloneRootWithChild(oldRootShadowNode, viewFactory.makeView(kRenderedTag));
    };
}

class ShadowTreeCommitTest : public ::testing::Test {
protected:
    PassThroughShadowTreeDelegate shadowTreeDelegate;
    ContextContainer contextContainer;
    ViewFactory viewFactory;
    ShadowTree shadowTree{kSurfaceId, LayoutConstraints{}, LayoutContext{}, shadowTreeDelegate, contextContainer};
};

class ShadowTreeCommitExhaustionTest : public ShadowTreeCommitTest {
protected:
    void SetUp() override {
        ReactNativeFeatureFlags::dangerouslyReset();
        ReactNativeFeatureFlags::override(std::make_unique<ExhaustionPreventingFeatureFlags>());
    }

    void TearDown() override {
        ReactNativeFeatureFlags::dangerouslyReset();
    }
};

TEST_F(ShadowTreeCommitTest, ATryCommitWhoseTransactionMovesTheBaseTreeReportsFailedAfterOneAttempt) {
    int transactionCalls = 0;

    const ShadowTree::CommitStatus status =
        shadowTree.tryCommit(transactionThatMovesTheBaseTree(shadowTree, viewFactory, transactionCalls),
                             commitOptions());

    EXPECT_EQ(status, ShadowTree::CommitStatus::Failed);
    EXPECT_EQ(transactionCalls, 1);
}

TEST_F(ShadowTreeCommitTest, ACommitWhoseTransactionIsCancelledReportsCancelledWithoutRetrying) {
    int transactionCalls = 0;

    const ShadowTree::CommitStatus status = shadowTree.commit(
        [&](const RootShadowNode& /*oldRootShadowNode*/) {
            transactionCalls++;

            return RootShadowNode::Unshared{};
        },
        commitOptions());

    EXPECT_EQ(status, ShadowTree::CommitStatus::Cancelled);
    EXPECT_EQ(transactionCalls, 1);
}

TEST_F(ShadowTreeCommitTest, AReEntrantStateUpdateConvergesAndReportsSucceededWithinABoundedNumberOfAttempts) {
    int transactionCalls = 0;

    const ShadowTree::CommitStatus status = shadowTree.commit(
        [&](const RootShadowNode& oldRootShadowNode) {
            transactionCalls++;

            if (transactionCalls == 1) {
                shadowTree.commit(
                    [&](const RootShadowNode& innerOldRootShadowNode) {
                        return cloneRootWithChild(innerOldRootShadowNode, viewFactory.makeView(kEffectTag));
                    },
                    commitOptions());
            }

            return cloneRootWithChild(oldRootShadowNode, viewFactory.makeView(kRenderedTag));
        },
        commitOptions());

    EXPECT_EQ(status, ShadowTree::CommitStatus::Succeeded);
    EXPECT_EQ(transactionCalls, 2);
}

TEST_F(ShadowTreeCommitExhaustionTest, ACommitThatAlwaysRacesTheBaseTreeReportsFailedAfterABoundedNumberOfAttempts) {
    int transactionCalls = 0;

    const ShadowTree::CommitStatus status =
        shadowTree.commit(transactionThatMovesTheBaseTree(shadowTree, viewFactory, transactionCalls),
                          commitOptions());

    EXPECT_EQ(status, ShadowTree::CommitStatus::Failed);
    EXPECT_EQ(transactionCalls, kAttemptsBeforeLocking + 1);
}

TEST_F(ShadowTreeCommitTest, TwoCommitsBeforeOnePullYieldOneTransactionDescribingOnlyTheFinalTree) {
    const std::shared_ptr<const MountingCoordinator> mountingCoordinator = shadowTree.getMountingCoordinator();

    shadowTree.commit(
        [&](const RootShadowNode& oldRootShadowNode) {
            return cloneRootWithChild(oldRootShadowNode, viewFactory.makeView(kRenderedTag));
        },
        commitOptions());
    shadowTree.commit(
        [&](const RootShadowNode& oldRootShadowNode) {
            return cloneRootWithChild(oldRootShadowNode, viewFactory.makeView(kEffectTag));
        },
        commitOptions());

    std::optional<MountingTransaction> transaction = mountingCoordinator->pullTransaction();

    ASSERT_TRUE(transaction.has_value());

    std::vector<Tag> createdTags;

    for (const ShadowViewMutation& mutation : transaction->getMutations()) {
        EXPECT_NE(mutation.newChildShadowView.tag, kRenderedTag);
        EXPECT_NE(mutation.oldChildShadowView.tag, kRenderedTag);

        if (mutation.type == ShadowViewMutation::Create) {
            createdTags.push_back(mutation.newChildShadowView.tag);
        }
    }

    EXPECT_EQ(createdTags, std::vector<Tag>{kEffectTag});

    LinuxMountingManager mountingManager;

    mountingManager.startSurface(kSurfaceId, Size{.width = 800, .height = 600});
    mountingManager.executeMount(kSurfaceId, std::move(transaction.value()));

    const std::string dump = mountingManager.dumpScene();

    EXPECT_EQ(mountingManager.mountDiagnostics().unknownTagOperations, 0U);
    EXPECT_NE(dump.find("View #" + std::to_string(kEffectTag)), std::string::npos);
    EXPECT_EQ(dump.find("#" + std::to_string(kRenderedTag)), std::string::npos);
    EXPECT_FALSE(mountingCoordinator->pullTransaction().has_value());
}

// The concurrency proof for #125: commits run on one thread while the frame consumer pulls on another, which is
// this platform's default rather than an edge case. Run it under ThreadSanitizer as well as plain CTest.
TEST_F(ShadowTreeCommitTest, ConcurrentPullTransactionIsSerializedAgainstCommitsOnAnotherThread) {
    const std::shared_ptr<const MountingCoordinator> mountingCoordinator = shadowTree.getMountingCoordinator();
    LinuxMountingManager mountingManager;
    std::latch startLatch{2};
    std::atomic<int> committedCount{0};

    mountingManager.startSurface(kSurfaceId, Size{.width = 800, .height = 600});

    std::thread committingThread([&] {
        startLatch.arrive_and_wait();

        for (int index = 0; index < kConcurrentCommitCount; index++) {
            shadowTree.commit(
                [&](const RootShadowNode& oldRootShadowNode) {
                    return cloneRootWithChild(oldRootShadowNode, viewFactory.makeView(kConcurrentBaseTag + index));
                },
                commitOptions());
            committedCount.fetch_add(1);
        }
    });

    startLatch.arrive_and_wait();

    while (committedCount.load() < kConcurrentCommitCount) {
        std::optional<MountingTransaction> transaction = mountingCoordinator->pullTransaction();

        if (transaction.has_value()) {
            mountingManager.executeMount(kSurfaceId, std::move(transaction.value()));
        }
    }

    committingThread.join();

    std::optional<MountingTransaction> lastTransaction = mountingCoordinator->pullTransaction();

    if (lastTransaction.has_value()) {
        mountingManager.executeMount(kSurfaceId, std::move(lastTransaction.value()));
    }

    const std::string dump = mountingManager.dumpScene();

    EXPECT_EQ(mountingManager.mountDiagnostics().unknownTagOperations, 0U);
    EXPECT_NE(dump.find("View #" + std::to_string(kConcurrentBaseTag + kConcurrentCommitCount - 1)),
              std::string::npos);
    EXPECT_EQ(std::count(dump.begin(), dump.end(), '\n'), 2);
    EXPECT_FALSE(mountingCoordinator->pullTransaction().has_value());
}

} // namespace
