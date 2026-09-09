#include "ShadowTreeTestSupport.h"

#include <cstdint>
#include <functional>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <vector>

#include <react/renderer/components/scrollview/ScrollViewState.h>
#include <react/renderer/core/ConcreteState.h>

// What a Wayland `xdg_toplevel.configure` does to the committed tree (#432). `FabricHost::setSurfaceSize` is one
// call to `SurfaceHandler::constraintLayout`, whose whole body is one `ShadowTree::commit` of
// `RootShadowNode::clone(propsParserContext, layoutConstraints, layoutContext)` — which is what `resizeTo` below
// is. These read the mounting transaction that commit produces rather than synthesising mutations, because the
// question the issue asks is what the differ emits, and only a real commit can answer it.
namespace {

using facebook::react::ConcreteState;
using facebook::react::ScrollViewShadowNode;
using facebook::react::ScrollViewState;
using facebook::react::ShadowViewMutation;

constexpr SurfaceId kSurfaceId = 1;
constexpr Tag kScrollViewTag = 2;
constexpr Tag kContentViewTag = 3;
constexpr Tag kFirstRowTag = 10;
constexpr int kRowCount = 4;
constexpr float kRowHeight = 100.0F;
constexpr float kInitialWidth = 400.0F;
constexpr float kResizedWidth = 800.0F;
constexpr float kSurfaceHeight = 600.0F;
constexpr float kScrolledDown = 250.0F;
constexpr size_t kNodeCount = static_cast<size_t>(kRowCount) + 3;
constexpr int32_t kOpaqueBlue = static_cast<int32_t>(0xFF3366CCU);

LayoutConstraints surfaceConstraints(float width) {
    const Size size{.width = width, .height = kSurfaceHeight};

    return LayoutConstraints{
        .minimumSize = size, .maximumSize = size, .layoutDirection = facebook::react::LayoutDirection::LeftToRight};
}

folly::dynamic paintedProps(folly::dynamic props) {
    props["backgroundColor"] = kOpaqueBlue;

    return props;
}

void collectFamilies(const ShadowNode& node, std::map<Tag, const ShadowNodeFamily*>& families) {
    families.emplace(node.getTag(), &node.getFamily());

    for (const std::shared_ptr<const ShadowNode>& child : node.getChildren()) {
        collectFamilies(*child, families);
    }
}

const ScrollViewShadowNode* findScrollView(const ShadowNode& node) {
    if (const auto* scrollView = dynamic_cast<const ScrollViewShadowNode*>(&node); scrollView != nullptr) {
        return scrollView;
    }

    for (const std::shared_ptr<const ShadowNode>& child : node.getChildren()) {
        if (const ScrollViewShadowNode* found = findScrollView(*child); found != nullptr) {
            return found;
        }
    }

    return nullptr;
}

class ResizeRelayoutTest : public ::testing::Test {
protected:
    void SetUp() override {
        uiManager_ = react_native_linux::makeTaskDroppingUIManager(contextContainer_);
        shadowTree_ = react_native_linux::addRegisteredShadowTree(*uiManager_, shadowTreeDelegate_, *contextContainer_,
                                                                  kSurfaceId);
        resizeTo(kInitialWidth);
        commitApplicationTree();
        drainTransaction();
    }

    void TearDown() override { react_native_linux::removeShadowTree(*uiManager_, kSurfaceId); }

    void resizeTo(float width) {
        const PropsParserContext propsParserContext{kSurfaceId, *contextContainer_};
        const LayoutConstraints layoutConstraints = surfaceConstraints(width);

        commit([&propsParserContext, &layoutConstraints](const RootShadowNode& oldRootShadowNode) {
            return oldRootShadowNode.clone(propsParserContext, layoutConstraints, LayoutContext{});
        });
    }

    /**
     * The list every case resizes: a ScrollView filling the surface over a content view over `kRowCount` rows,
     * every one of them sized as a percentage of its parent so that the width of the surface is the only thing
     * their laid-out frames depend on. The rows are painted because `ViewShadowNode` forms no view without a
     * meaningful background colour and the differ flattens such a node away, which would leave a resize with
     * nothing to emit and every assertion here reading as an empty transaction.
     */
    void commitApplicationTree() {
        const auto rows = std::make_shared<const std::vector<std::shared_ptr<const ShadowNode>>>(makeRows());
        const std::shared_ptr<const ShadowNode> contentView = react_native_linux::makeConfiguredShadowNode(
            viewDescriptor_, kContentViewTag, kSurfaceId, contextContainer_,
            paintedProps(folly::dynamic::object("width", "100%")), rows);
        const auto contentChildren = std::make_shared<const std::vector<std::shared_ptr<const ShadowNode>>>(
            std::vector<std::shared_ptr<const ShadowNode>>{contentView});
        const std::shared_ptr<const ShadowNode> scrollView = react_native_linux::makeConfiguredShadowNode(
            scrollViewDescriptor_, kScrollViewTag, kSurfaceId, contextContainer_,
            folly::dynamic::object("width", "100%")("height", "100%"), contentChildren);
        const auto surfaceChildren = std::make_shared<const std::vector<std::shared_ptr<const ShadowNode>>>(
            std::vector<std::shared_ptr<const ShadowNode>>{scrollViewScrolledTo(*scrollView, kScrolledDown)});

        commit([&surfaceChildren](const RootShadowNode& oldRootShadowNode) {
            return react_native_linux::cloneRootWithChildren(oldRootShadowNode, surfaceChildren);
        });
    }

    std::vector<std::shared_ptr<const ShadowNode>> makeRows() {
        std::vector<std::shared_ptr<const ShadowNode>> rows;

        for (int position = 0; position < kRowCount; position++) {
            rows.push_back(react_native_linux::makeConfiguredShadowNode(
                viewDescriptor_, kFirstRowTag + position, kSurfaceId, contextContainer_,
                paintedProps(folly::dynamic::object("width", "100%")("height", kRowHeight)),
                std::make_shared<const std::vector<std::shared_ptr<const ShadowNode>>>()));
        }

        return rows;
    }

    std::shared_ptr<const ShadowNode> scrollViewScrolledTo(const ShadowNode& scrollView, float offsetY) {
        const auto stateData =
            std::make_shared<const ScrollViewState>(ScrollViewState{Point{.x = 0, .y = offsetY}, Rect{}, 0});

        return scrollView.clone(
            ShadowNodeFragment{.props = ShadowNodeFragment::propsPlaceholder(),
                               .children = ShadowNodeFragment::childrenPlaceholder(),
                               .state = scrollViewDescriptor_.createState(scrollView.getFamily(), stateData)});
    }

    void commit(const std::function<RootShadowNode::Unshared(const RootShadowNode&)>& transaction) {
        shadowTree_->commit(transaction, {});
    }

    RootShadowNode::Shared currentRoot() { return shadowTree_->getCurrentRevision().rootShadowNode; }

    std::vector<ShadowViewMutation> drainTransaction() {
        std::optional<MountingTransaction> transaction = shadowTree_->getMountingCoordinator()->pullTransaction();

        if (!transaction.has_value()) {
            return {};
        }

        return transaction->getMutations();
    }

    std::map<Tag, Rect> currentFrames() {
        std::map<Tag, Rect> frames;

        react_native_linux::collectAbsoluteFrames(currentRoot(), Point{.x = 0, .y = 0}, frames,
                                                  [](const std::shared_ptr<const ShadowNode>&) {});

        return frames;
    }

    std::shared_ptr<const ContextContainer> contextContainer_{std::make_shared<ContextContainer>()};
    std::shared_ptr<facebook::react::UIManager> uiManager_;
    ViewComponentDescriptor viewDescriptor_{makeViewComponentDescriptor(contextContainer_)};
    facebook::react::ScrollViewComponentDescriptor scrollViewDescriptor_{ComponentDescriptorParameters{
        .eventDispatcher = EventDispatcher::Shared{}, .contextContainer = contextContainer_, .flavor = nullptr}};
    react_native_linux::PassThroughShadowTreeDelegate shadowTreeDelegate_;
    ShadowTree* shadowTree_{nullptr};
};

TEST_F(ResizeRelayoutTest, ResizeEmitsOnlyUpdateMutations) {
    const std::map<Tag, Rect> before = currentFrames();

    resizeTo(kResizedWidth);

    const std::vector<ShadowViewMutation> mutations = drainTransaction();

    ASSERT_FALSE(mutations.empty());

    for (const ShadowViewMutation& mutation : mutations) {
        EXPECT_EQ(mutation.type, ShadowViewMutation::Update)
            << "mutation type " << static_cast<int>(mutation.type) << " on tags " << mutation.oldChildShadowView.tag
            << "/" << mutation.newChildShadowView.tag;
    }

    const std::map<Tag, Rect> after = currentFrames();

    EXPECT_EQ(before.at(kFirstRowTag).size.width, kInitialWidth);
    EXPECT_EQ(after.at(kFirstRowTag).size.width, kResizedWidth);
}

TEST_F(ResizeRelayoutTest, ResizePreservesShadowNodeFamilyIdentity) {
    std::map<Tag, const ShadowNodeFamily*> before;
    std::map<Tag, const ShadowNodeFamily*> after;

    collectFamilies(*currentRoot(), before);
    resizeTo(kResizedWidth);
    drainTransaction();
    collectFamilies(*currentRoot(), after);

    EXPECT_EQ(before.size(), kNodeCount);
    EXPECT_EQ(before, after);
}

TEST_F(ResizeRelayoutTest, ResizePreservesScrollContentOffset) {
    ASSERT_NE(findScrollView(*currentRoot()), nullptr);
    EXPECT_EQ(findScrollView(*currentRoot())->getStateData().contentOffset.y, kScrolledDown);

    resizeTo(kResizedWidth);
    drainTransaction();

    const ScrollViewShadowNode* scrollView = findScrollView(*currentRoot());

    ASSERT_NE(scrollView, nullptr);
    EXPECT_EQ(scrollView->getStateData().contentOffset.y, kScrolledDown);
    EXPECT_EQ(scrollView->getStateData().getContentSize().width, kResizedWidth);
}

/**
 * The #47153 invariant: the metrics an offset would be resolved against are the ones the resize's own layout
 * produced, in the transaction that carries the new viewport. A ScrollView whose frame has already grown while its
 * content bounding rect still describes the old width is the stale-metrics window that issue is about, so the two
 * are asserted on the same `ShadowView`.
 */
TEST_F(ResizeRelayoutTest, ResizeTransactionCarriesTheContentMetricsItsOwnLayoutProduced) {
    resizeTo(kResizedWidth);

    const std::vector<ShadowViewMutation> mutations = drainTransaction();
    int scrollViewMutations = 0;

    for (const ShadowViewMutation& mutation : mutations) {
        if (mutation.newChildShadowView.tag != kScrollViewTag) {
            continue;
        }

        scrollViewMutations++;

        const auto* state =
            dynamic_cast<const ConcreteState<ScrollViewState>*>(mutation.newChildShadowView.state.get());

        ASSERT_NE(state, nullptr);
        EXPECT_EQ(mutation.newChildShadowView.layoutMetrics.frame.size.width, kResizedWidth);
        EXPECT_EQ(state->getData().getContentSize().width, kResizedWidth);
        EXPECT_EQ(state->getData().contentOffset.y, kScrolledDown);
    }

    EXPECT_EQ(scrollViewMutations, 1);
}

} // namespace
