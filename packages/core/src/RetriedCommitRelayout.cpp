#include "RetriedCommitRelayout.h"

#include <utility>

#include <react/renderer/core/LayoutableShadowNode.h>
#include <react/renderer/core/ShadowNode.h>

namespace react_native_linux {

namespace {

thread_local const facebook::react::ShadowTree* treeInFlight = nullptr;

const facebook::react::ShadowNode* committedCounterpart(const facebook::react::ShadowNode& candidate,
                                                        const facebook::react::ShadowNode* committedParent) {
    if (committedParent == nullptr) {
        return nullptr;
    }

    for (const std::shared_ptr<const facebook::react::ShadowNode>& committedChild : committedParent->getChildren()) {
        if (facebook::react::ShadowNode::sameFamily(*committedChild, candidate)) {
            return committedChild.get();
        }
    }

    return nullptr;
}

void dirtyUncommittedLayout(facebook::react::LayoutableShadowNode& candidate,
                            const facebook::react::ShadowNode* committed) {
    if (&candidate == committed) {
        return;
    }

    candidate.dirtyLayout();

    for (facebook::react::LayoutableShadowNode* child : candidate.getLayoutableChildNodes()) {
        dirtyUncommittedLayout(*child, committedCounterpart(*child, committed));
    }
}

} // namespace

void RetriedCommitRelayout::commitHookWasRegistered(const facebook::react::UIManager& /*uiManager*/) noexcept {}

void RetriedCommitRelayout::commitHookWasUnregistered(const facebook::react::UIManager& /*uiManager*/) noexcept {}

facebook::react::RootShadowNode::Unshared RetriedCommitRelayout::shadowTreeWillCommit(
    const facebook::react::ShadowTree& shadowTree, const facebook::react::RootShadowNode::Shared& oldRootShadowNode,
    const facebook::react::RootShadowNode::Unshared& newRootShadowNode,
    const facebook::react::ShadowTreeCommitOptions& /*commitOptions*/) noexcept {
    if (std::exchange(treeInFlight, &shadowTree) == &shadowTree) {
        dirtyUncommittedLayout(*newRootShadowNode, oldRootShadowNode.get());
    }

    return newRootShadowNode;
}

void RetriedCommitRelayout::shadowTreeDidCommit(
    const facebook::react::ShadowTree& /*shadowTree*/,
    const facebook::react::RootShadowNode::Shared& /*rootShadowNode*/,
    const std::vector<const facebook::react::LayoutableShadowNode*>& /*affectedLayoutableNodes*/) noexcept {
    treeInFlight = nullptr;
}

} // namespace react_native_linux
