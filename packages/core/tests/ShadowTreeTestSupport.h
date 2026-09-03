#pragma once

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

namespace react_native_linux {

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
