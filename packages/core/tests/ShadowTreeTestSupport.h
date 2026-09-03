#pragma once

#include <react/renderer/components/root/RootShadowNode.h>
#include <react/renderer/mounting/MountingCoordinator.h>
#include <react/renderer/mounting/ShadowTreeDelegate.h>

namespace react_native_linux {

/**
 * The `ShadowTreeDelegate` upstream's own mounting tests use: it accepts every proposed tree and lets the
 * `MountingCoordinator` be the only observer of a commit, which is what makes `pullTransaction` the thing under
 * test rather than a mount callback.
 */
class PassThroughShadowTreeDelegate final : public facebook::react::ShadowTreeDelegate {
public:
    facebook::react::RootShadowNode::Unshared shadowTreeWillCommit(
        const facebook::react::ShadowTree& /*shadowTree*/,
        const facebook::react::RootShadowNode::Shared& /*oldRootShadowNode*/,
        const facebook::react::RootShadowNode::Unshared& newRootShadowNode,
        const facebook::react::ShadowTreeCommitOptions& /*commitOptions*/) const override {
        return newRootShadowNode;
    }

    void shadowTreeDidFinishTransaction(
        std::shared_ptr<const facebook::react::MountingCoordinator> /*mountingCoordinator*/,
        bool /*mountSynchronously*/) const override {}

    void shadowTreeDidFinishReactCommit(const facebook::react::ShadowTree& /*shadowTree*/) const override {}

    void shadowTreeDidPromoteReactRevision(const facebook::react::ShadowTree& /*shadowTree*/) const override {}
};

} // namespace react_native_linux
