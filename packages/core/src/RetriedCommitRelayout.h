#pragma once

#include <react/renderer/uimanager/UIManagerCommitHook.h>

namespace react_native_linux {

/**
 * Lays a retried commit out as its first attempt found it, so the views it mounts still get their first `onLayout`.
 *
 * `ShadowTree::tryCommit` lays the candidate tree out before it compares revisions, and that layout mutates the
 * candidate's uncommitted nodes in place, spending Yoga's `hasNewLayout` flag on every node it visits. When another
 * thread commits in between (the frame thread's resize, layout-direction or state-update commits against a JavaScript
 * `completeRoot`), the compare fails and the transaction runs again over the same uncommitted nodes. Yoga finds them
 * already laid out, skips them, and `LayoutEventEmitter` never hears of them, so their `onLayout` is lost for good.
 *
 * A retry is a second `shadowTreeWillCommit` for one tree on one thread with no `shadowTreeDidCommit` between them.
 * On a retry this marks every node of the candidate that the committed tree does not share dirty, which is how the
 * first attempt found them; nodes the committed tree shares were never mutated, because Yoga clones a node it does
 * not own before laying it out. A commit that a later hook cancels looks like a retry to the next commit on that
 * thread, which then only relays what that commit changed.
 *
 * Threading contract: both callbacks run on the committing thread, and the only state is that thread's own tree in
 * flight, so one instance serves every thread that commits.
 */
class RetriedCommitRelayout final : public facebook::react::UIManagerCommitHook {
public:
    using facebook::react::UIManagerCommitHook::shadowTreeWillCommit;

    void commitHookWasRegistered(const facebook::react::UIManager& uiManager) noexcept override;
    void commitHookWasUnregistered(const facebook::react::UIManager& uiManager) noexcept override;

    facebook::react::RootShadowNode::Unshared
    shadowTreeWillCommit(const facebook::react::ShadowTree& shadowTree,
                         const facebook::react::RootShadowNode::Shared& oldRootShadowNode,
                         const facebook::react::RootShadowNode::Unshared& newRootShadowNode,
                         const facebook::react::ShadowTreeCommitOptions& commitOptions) noexcept override;

    void shadowTreeDidCommit(
        const facebook::react::ShadowTree& shadowTree, const facebook::react::RootShadowNode::Shared& rootShadowNode,
        const std::vector<const facebook::react::LayoutableShadowNode*>& affectedLayoutableNodes) noexcept override;
};

} // namespace react_native_linux
