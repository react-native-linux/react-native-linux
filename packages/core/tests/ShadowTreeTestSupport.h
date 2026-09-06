#pragma once

#include <algorithm>
#include <filesystem>
#include <map>
#include <react/featureflags/ReactNativeFeatureFlags.h>
#include <react/featureflags/ReactNativeFeatureFlagsDefaults.h>
#include <react/renderer/components/root/RootShadowNode.h>
#include <react/renderer/components/scrollview/ScrollViewComponentDescriptor.h>
#include <react/renderer/components/scrollview/ScrollViewShadowNode.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>
#include <react/renderer/components/view/ViewShadowNode.h>
#include <react/renderer/core/ComponentDescriptor.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/renderer/core/LayoutMetrics.h>
#include <react/renderer/core/LayoutableShadowNode.h>
#include <react/renderer/core/Props.h>
#include <react/renderer/core/PropsParserContext.h>
#include <react/renderer/core/RawProps.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/core/ShadowNode.h>
#include <react/renderer/core/ShadowNodeFamily.h>
#include <react/renderer/core/ShadowNodeFragment.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/mounting/MountingCoordinator.h>
#include <react/renderer/mounting/ShadowTree.h>
#include <react/renderer/mounting/ShadowTreeDelegate.h>
#include <react/renderer/uimanager/UIManager.h>
#include <string>
#include <vector>

// The using-declarations both commit-driven test files share. They live here rather than in each file because a
// second copy of the list is a jscpd clone at threshold 0. The header is included by tests only, so the
// declarations leak nowhere else.
using facebook::react::ComponentDescriptorParameters;
using facebook::react::ContextContainer;
using facebook::react::EventDispatcher;
using facebook::react::LayoutableShadowNode;
using facebook::react::LayoutConstraints;
using facebook::react::LayoutContext;
using facebook::react::MountingCoordinator;
using facebook::react::MountingTransaction;
using facebook::react::Point;
using facebook::react::PropsParserContext;
using facebook::react::RawProps;
using facebook::react::ReactNativeFeatureFlags;
using facebook::react::ReactNativeFeatureFlagsDefaults;
using facebook::react::Rect;
using facebook::react::RootShadowNode;
using facebook::react::ScrollViewShadowNode;
using facebook::react::ShadowNode;
using facebook::react::ShadowNodeFamily;
using facebook::react::ShadowNodeFragment;
using facebook::react::ShadowTree;
using facebook::react::ShadowTreeCommitOptions;
using facebook::react::ShadowTreeDelegate;
using facebook::react::ShadowViewMutation;
using facebook::react::Size;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::ViewComponentDescriptor;
using facebook::react::ViewShadowNode;

// The two descriptors every commit-driven fixture builds with the same empty dispatcher. A helper rather than a
// member initializer in each fixture because the initializer is a jscpd clone at threshold 0.
inline facebook::react::ViewComponentDescriptor
makeViewComponentDescriptor(const std::shared_ptr<const facebook::react::ContextContainer>& contextContainer) {
    return facebook::react::ViewComponentDescriptor{
        facebook::react::ComponentDescriptorParameters{.eventDispatcher = facebook::react::EventDispatcher::Shared{},
                                                       .contextContainer = contextContainer,
                                                       .flavor = nullptr}};
}

namespace react_native_linux {

/**
 * The sorted file list a vendored upstream tests directory holds, which both drift-oracle tests (#132's animated
 * list and #211's Fabric module lists) assert against. A shared helper because the same function twice is a
 * jscpd clone at threshold 0.
 */
inline std::vector<std::string> sortedUpstreamFileNames(const std::filesystem::path& directory) {
    std::vector<std::string> fileNames;

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            fileNames.push_back(entry.path().filename().string());
        }
    }

    std::sort(fileNames.begin(), fileNames.end());

    return fileNames;
}

/**
 * A UIManager whose runtime executor drops its tasks: the commit-driven fixtures never dispatch through it, and
 * the empty lambda keeps the UIManager's constructor requirement satisfied without a runtime scheduler.
 */
inline std::shared_ptr<facebook::react::UIManager>
makeTaskDroppingUIManager(const std::shared_ptr<const facebook::react::ContextContainer>& contextContainer) {
    return std::make_shared<facebook::react::UIManager>([](std::function<void(facebook::jsi::Runtime&)>&&) {},
                                                        contextContainer);
}

/**
 * A registered, delegate-wired `ShadowTree` for commit-driven fixtures: constructed, added to the `uiManager`'s
 * registry, and handed back as a raw pointer the fixture commits through. The registry owns the tree;
 * `removeShadowTree` is the fixture's teardown half, because the registry asserts when it dies non-empty.
 */
inline facebook::react::ShadowTree* addRegisteredShadowTree(facebook::react::UIManager& uiManager,
                                                            facebook::react::ShadowTreeDelegate& delegate,
                                                            const facebook::react::ContextContainer& contextContainer,
                                                            SurfaceId surfaceId) {
    auto shadowTree = std::make_unique<facebook::react::ShadowTree>(
        surfaceId, facebook::react::LayoutConstraints{}, facebook::react::LayoutContext{}, delegate, contextContainer);

    facebook::react::ShadowTree* rawPointer = shadowTree.get();

    uiManager.getShadowTreeRegistry().add(std::move(shadowTree));

    return rawPointer;
}

inline void removeShadowTree(facebook::react::UIManager& uiManager, SurfaceId surfaceId) {
    uiManager.getShadowTreeRegistry().remove(surfaceId);
}

/**
 * One configured shadow node from a concrete component descriptor: family, parsed props, optional initial state.
 * The three commit-driven fixtures build their nodes through this because the same body three times is a jscpd
 * clone at threshold 0.
 */
template <typename ComponentDescriptorT>
std::shared_ptr<const facebook::react::ShadowNode> makeConfiguredShadowNode(
    ComponentDescriptorT& descriptor, Tag tag, SurfaceId surfaceId,
    const std::shared_ptr<const facebook::react::ContextContainer>& contextContainer, folly::dynamic props,
    const std::shared_ptr<const std::vector<std::shared_ptr<const facebook::react::ShadowNode>>>& children) {
    using ConcreteShadowNodeT = typename ComponentDescriptorT::ConcreteShadowNode;
    const facebook::react::ShadowNodeFamily::Shared family =
        descriptor.createFamily({.tag = tag, .surfaceId = surfaceId, .instanceHandle = nullptr});

    const facebook::react::PropsParserContext parserContext{surfaceId, *contextContainer};
    facebook::react::RawProps rawProps{folly::dynamic(props)};

    const facebook::react::Props::Shared nodeProps =
        descriptor.cloneProps(parserContext, ConcreteShadowNodeT::defaultSharedProps(), std::move(rawProps));

    const facebook::react::ShadowNodeFragment fragment{
        .props = nodeProps, .children = children, .state = descriptor.createInitialState(nodeProps, family)};

    return descriptor.createShadowNode(fragment, family);
}

/**
 * The root clone every commit-driven fixture hands `ShadowTree::commit` as its mutation: the root's own props are
 * untouched and only the child list changes. A helper rather than a lambda body repeated per fixture because the
 * same clone is a jscpd clone at threshold 0.
 */
inline facebook::react::RootShadowNode::Unshared cloneRootWithChildren(
    const facebook::react::RootShadowNode& oldRootShadowNode,
    std::shared_ptr<const std::vector<std::shared_ptr<const facebook::react::ShadowNode>>> children) {
    return std::static_pointer_cast<facebook::react::RootShadowNode>(oldRootShadowNode.ShadowNode::clone(
        ShadowNodeFragment{.props = ShadowNodeFragment::propsPlaceholder(), .children = std::move(children)}));
}

/**
 * The absolute-frame tree walk every commit-driven layout fixture replays: each node's own `LayoutableShadowNode`
 * frame folded into the running parent origin, with `perNode` for whatever else a fixture wants to read off the
 * node it is currently at (a `ScrollView`'s content size, a tag-to-node index) — called once per visited node,
 * before the frame of that node is computed. A helper rather than a second copy because the walk itself is a
 * jscpd clone at threshold 0.
 */
template <typename PerNode>
void collectAbsoluteFrames(const std::shared_ptr<const facebook::react::ShadowNode>& node, Point parentOrigin,
                          std::map<Tag, Rect>& frames, const PerNode& perNode) {
    perNode(node);

    const auto* layoutable = dynamic_cast<const LayoutableShadowNode*>(node.get());

    if (layoutable == nullptr) {
        return;
    }

    const Rect frame = layoutable->getLayoutMetrics().frame;
    const Point absolute{parentOrigin.x + frame.origin.x, parentOrigin.y + frame.origin.y};

    frames.emplace(node->getTag(), Rect{.origin = absolute, .size = frame.size});

    for (const std::shared_ptr<const facebook::react::ShadowNode>& child : node->getChildren()) {
        collectAbsoluteFrames(child, absolute, frames, perNode);
    }
}

/**
 * The `ShadowTreeDelegate` upstream's own mounting tests use: it accepts every proposed tree and lets the
 * `MountingCoordinator` be the only observer of a commit, which is what makes `pullTransaction` the thing under
 * test rather than a mount callback.
 */
class PassThroughShadowTreeDelegate final : public facebook::react::ShadowTreeDelegate {
public:
    facebook::react::RootShadowNode::Unshared
    shadowTreeWillCommit(const facebook::react::ShadowTree& /*shadowTree*/,
                         const facebook::react::RootShadowNode::Shared& /*oldRootShadowNode*/,
                         const facebook::react::RootShadowNode::Unshared& newRootShadowNode,
                         const facebook::react::ShadowTreeCommitOptions& /*commitOptions*/) const override {
        return newRootShadowNode;
    }

    void
    shadowTreeDidFinishTransaction(std::shared_ptr<const facebook::react::MountingCoordinator> /*mountingCoordinator*/,
                                   bool /*mountSynchronously*/) const override {}

    void shadowTreeDidFinishReactCommit(const facebook::react::ShadowTree& /*shadowTree*/) const override {}

    void shadowTreeDidPromoteReactRevision(const facebook::react::ShadowTree& /*shadowTree*/) const override {}
};

} // namespace react_native_linux
