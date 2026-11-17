#include "RetriedCommitRelayout.h"

#include <memory>
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

std::shared_ptr<facebook::react::ShadowNode> dirtyCopy(const facebook::react::ShadowNode& candidate,
                                                       const facebook::react::ShadowNode* committed) {
    auto children = candidate.getChildren();

    for (std::shared_ptr<const facebook::react::ShadowNode>& child : children) {
        const facebook::react::ShadowNode* committedChild = committedCounterpart(*child, committed);

        if (child.get() != committedChild &&
            dynamic_cast<const facebook::react::LayoutableShadowNode*>(child.get()) != nullptr) {
            child = dirtyCopy(*child, committedChild);
        }
    }

    std::shared_ptr<facebook::react::ShadowNode> copy = candidate.clone(
        {.children = std::make_shared<const std::vector<std::shared_ptr<const facebook::react::ShadowNode>>>(
             std::move(children))});

    static_cast<facebook::react::LayoutableShadowNode&>(*copy).dirtyLayout();

    return copy;
}

} // namespace

void RetriedCommitRelayout::commitHookWasRegistered(const facebook::react::UIManager& /*uiManager*/) noexcept {}

void RetriedCommitRelayout::commitHookWasUnregistered(const facebook::react::UIManager& /*uiManager*/) noexcept {}

facebook::react::RootShadowNode::Unshared RetriedCommitRelayout::shadowTreeWillCommit(
    const facebook::react::ShadowTree& shadowTree, const facebook::react::RootShadowNode::Shared& oldRootShadowNode,
    const facebook::react::RootShadowNode::Unshared& newRootShadowNode,
    const facebook::react::ShadowTreeCommitOptions& /*commitOptions*/) noexcept {
    if (std::exchange(treeInFlight, &shadowTree) != &shadowTree) {
        return newRootShadowNode;
    }

    return std::static_pointer_cast<facebook::react::RootShadowNode>(
        dirtyCopy(*newRootShadowNode, oldRootShadowNode.get()));
}

void RetriedCommitRelayout::shadowTreeDidCommit(
    const facebook::react::ShadowTree& /*shadowTree*/,
    const facebook::react::RootShadowNode::Shared& /*rootShadowNode*/,
    const std::vector<const facebook::react::LayoutableShadowNode*>& /*affectedLayoutableNodes*/) noexcept {
    treeInFlight = nullptr;
}

} // namespace react_native_linux
