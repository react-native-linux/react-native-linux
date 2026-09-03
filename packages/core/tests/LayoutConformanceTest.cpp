#include "ShadowTreeTestSupport.h"

#include <atomic>
#include <folly/dynamic.h>
#include <gtest/gtest.h>
#include <latch>
#include <map>
#include <memory>
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
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/mounting/MountingCoordinator.h>
#include <react/renderer/mounting/ShadowTree.h>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

using react_native_linux::PassThroughShadowTreeDelegate;

constexpr SurfaceId kSurfaceId = 1;
constexpr double kFrameTolerance = 0.001;
constexpr double kIndefinite = -1.0;

using ChildList = std::vector<std::shared_ptr<const ShadowNode>>;

struct NodeSpec final {
    Tag tag;
    std::string componentName = "View";
    folly::dynamic props = folly::dynamic::object();
    std::vector<NodeSpec> children = {};
};

folly::dynamic box(double width, double height) {
    folly::dynamic props = folly::dynamic::object();

    if (width != kIndefinite) {
        props["width"] = width;
    }
    if (height != kIndefinite) {
        props["height"] = height;
    }

    return props;
}

std::vector<NodeSpec> boxes(const std::vector<Tag>& tags, double width, double height) {
    std::vector<NodeSpec> items;

    for (const Tag tag : tags) {
        items.push_back(NodeSpec{.tag = tag, .props = box(width, height)});
    }

    return items;
}

NodeSpec rowParent(Tag tag, double width, std::vector<NodeSpec> children) {
    return NodeSpec{.tag = tag,
                    .props = folly::dynamic::object("width", width)("height", 100)("flexDirection", "row"),
                    .children = std::move(children)};
}

/**
 * React Native's default flex direction is `column`, not CSS's `row`, so every wrapping case states `row`
 * explicitly — the wrapping-contract questions are about horizontal line breaking.
 */
folly::dynamic wrappingRow(double width, folly::dynamic extra = folly::dynamic::object()) {
    folly::dynamic props = folly::dynamic::object("width", width)("flexDirection", "row")("flexWrap", "wrap");

    for (const auto& entry : extra.items()) {
        props[entry.first] = entry.second;
    }

    return props;
}

/**
 * The layout conformance suite: every case commits a real tree through the ShadowTree commit path — the same
 * Yoga layout the platform runs on the commit thread — and reads absolute frames back out of the committed
 * revision. Nothing here talks to Yoga directly, so a case fails exactly when our pipeline would lay the tree
 * out differently from the CSS rule the case states.
 */
class LayoutConformanceTest : public ::testing::Test {
protected:
    const std::map<Tag, Rect>& commitTree(LayoutConstraints constraints, std::vector<NodeSpec> children) {
        frames_.clear();
        nodes_.clear();
        scrollViewContentSizes_.clear();

        shadowTree_ = std::make_unique<ShadowTree>(kSurfaceId, constraints, LayoutContext{}, shadowTreeDelegate_,
                                                   *contextContainer_);

        const ShadowTreeCommitOptions commitOptions{.enableStateReconciliation = false, .mountSynchronously = true};

        shadowTree_->commit(
            [this, &children](const RootShadowNode& oldRootShadowNode) {
                return cloneRootWith(oldRootShadowNode, children);
            },
            commitOptions);

        committedRevision_ = shadowTree_->getCurrentRevision();

        collectFrames(committedRevision_.rootShadowNode, Point{});

        return frames_;
    }

    const ShadowNode& node(Tag tag) const {
        const auto entry = nodes_.find(tag);

        if (entry == nodes_.end()) {
            ADD_FAILURE() << "tag " << tag << " was not committed";
        }

        return *entry->second;
    }

    RootShadowNode::Unshared cloneRootWith(const RootShadowNode& oldRootShadowNode, std::vector<NodeSpec>& children) {
        return std::static_pointer_cast<RootShadowNode>(oldRootShadowNode.ShadowNode::clone(
            ShadowNodeFragment{.props = ShadowNodeFragment::propsPlaceholder(), .children = makeChildren(children)}));
    }

    static void expectFrameNear(const std::map<Tag, Rect>& frames, Tag tag, double x, double y, double width,
                                double height) {
        const auto entry = frames.find(tag);

        if (entry == frames.end()) {
            ADD_FAILURE() << "tag " << tag << " was not committed";
            return;
        }

        const Rect& frame = entry->second;

        EXPECT_NEAR(frame.origin.x, x, kFrameTolerance) << "tag " << tag << " origin.x";
        EXPECT_NEAR(frame.origin.y, y, kFrameTolerance) << "tag " << tag << " origin.y";
        EXPECT_NEAR(frame.size.width, width, kFrameTolerance) << "tag " << tag << " size.width";
        EXPECT_NEAR(frame.size.height, height, kFrameTolerance) << "tag " << tag << " size.height";
    }

    Size contentSize(Tag tag) const {
        const auto entry = scrollViewContentSizes_.find(tag);

        if (entry == scrollViewContentSizes_.end()) {
            ADD_FAILURE() << "tag " << tag << " is not a ScrollView";
            return Size{};
        }

        return entry->second;
    }

    const std::map<Tag, Rect>& commitWrappingRow(folly::dynamic containerProps, std::vector<NodeSpec> items,
                                                 Tag containerTag = 10) {
        return commitTree(
            LayoutConstraints{},
            {NodeSpec{.tag = containerTag, .props = std::move(containerProps), .children = std::move(items)}});
    }

    const std::map<Tag, Rect>& commitItemInRowContainer(const NodeSpec& item) {
        return commitTree(LayoutConstraints{}, {NodeSpec{.tag = 10,
                                                         .props = folly::dynamic::object("width", 300)("height", 300)(
                                                             "flexDirection", "row")("alignItems", "flex-start"),
                                                         .children = std::vector<NodeSpec>{item}}});
    }

    /**
     * Re-commits the tree that is currently committed with one style property changed on the tagged node — the
     * way a React update reaches Yoga. The new props are cloned from the committed node's props with only the
     * delta applied, so this is the props re-read path and not a tree rebuild.
     */
    const std::map<Tag, Rect>& recommitWithStyle(Tag targetTag, folly::dynamic delta) {
        const auto target = nodes_.find(targetTag);

        if (target == nodes_.end()) {
            ADD_FAILURE() << "tag " << targetTag << " was not committed";
            return frames_;
        }

        const PropsParserContext parserContext{kSurfaceId, *contextContainer_};
        const facebook::react::Props::Shared updatedProps = viewDescriptor_.cloneProps(
            parserContext, target->second->getProps(), RawProps{folly::dynamic(std::move(delta))});

        const ShadowTreeCommitOptions commitOptions{.enableStateReconciliation = false, .mountSynchronously = true};

        shadowTree_->commit(
            [&](const RootShadowNode& oldRootShadowNode) {
                return std::static_pointer_cast<RootShadowNode>(oldRootShadowNode.ShadowNode::clone(ShadowNodeFragment{
                    .props = ShadowNodeFragment::propsPlaceholder(),
                    .children = withReplacedProps(oldRootShadowNode.getChildren(), targetTag, updatedProps)}));
            },
            commitOptions);

        committedRevision_ = shadowTree_->getCurrentRevision();

        frames_.clear();
        nodes_.clear();
        scrollViewContentSizes_.clear();
        collectFrames(committedRevision_.rootShadowNode, Point{});

        return frames_;
    }

protected:
    std::shared_ptr<const ChildList> withReplacedProps(const ChildList& children, Tag targetTag,
                                                       const facebook::react::Props::Shared& updatedProps) {
        auto replaced = std::make_shared<ChildList>();

        for (const std::shared_ptr<const ShadowNode>& child : children) {
            if (child->getTag() == targetTag) {
                replaced->push_back(child->clone(ShadowNodeFragment{.props = updatedProps}));
                continue;
            }

            if (child->getChildren().empty()) {
                replaced->push_back(child);
                continue;
            }

            replaced->push_back(child->clone(
                ShadowNodeFragment{.props = ShadowNodeFragment::propsPlaceholder(),
                                   .children = withReplacedProps(child->getChildren(), targetTag, updatedProps)}));
        }

        return replaced;
    }

    std::shared_ptr<const ChildList> makeChildren(const std::vector<NodeSpec>& specs) {
        ChildList children;

        for (const NodeSpec& spec : specs) {
            children.push_back(makeNode(spec));
        }

        return std::make_shared<const ChildList>(std::move(children));
    }

    std::shared_ptr<const ShadowNode> makeNode(const NodeSpec& spec) {
        const bool isScrollView = spec.componentName == "ScrollView";

        const ShadowNodeFamily::Shared family =
            (isScrollView
                 ? scrollViewDescriptor_.createFamily(
                       {.tag = spec.tag, .surfaceId = kSurfaceId, .instanceHandle = nullptr})
                 : viewDescriptor_.createFamily({.tag = spec.tag, .surfaceId = kSurfaceId, .instanceHandle = nullptr}));

        ShadowNodeFragment fragment{.props = cloneProps(spec), .children = makeChildren(spec.children)};

        if (isScrollView) {
            const ShadowNodeFragment scrollViewFragment{
                .props = fragment.props,
                .children = fragment.children,
                .state = scrollViewDescriptor_.createInitialState(fragment.props, family)};

            return scrollViewDescriptor_.createShadowNode(scrollViewFragment, family);
        }

        return viewDescriptor_.createShadowNode(fragment, family);
    }

    facebook::react::Props::Shared cloneProps(const NodeSpec& spec) {
        const PropsParserContext parserContext{kSurfaceId, *contextContainer_};
        RawProps rawProps{folly::dynamic(spec.props)};

        if (spec.componentName == "ScrollView") {
            return scrollViewDescriptor_.cloneProps(parserContext, ScrollViewShadowNode::defaultSharedProps(),
                                                    std::move(rawProps));
        }

        return viewDescriptor_.cloneProps(parserContext, ViewShadowNode::defaultSharedProps(), std::move(rawProps));
    }

    void collectFrames(const std::shared_ptr<const ShadowNode>& node, Point parentOrigin) {
        nodes_.emplace(node->getTag(), node);

        const auto* layoutable = dynamic_cast<const LayoutableShadowNode*>(node.get());

        if (layoutable == nullptr) {
            return;
        }

        const Rect frame = layoutable->getLayoutMetrics().frame;
        const Point absolute{parentOrigin.x + frame.origin.x, parentOrigin.y + frame.origin.y};

        frames_.emplace(node->getTag(), Rect{.origin = absolute, .size = frame.size});

        if (const auto* scrollView = dynamic_cast<const ScrollViewShadowNode*>(node.get())) {
            scrollViewContentSizes_.emplace(node->getTag(), scrollView->getStateData().getContentSize());
        }

        for (const std::shared_ptr<const ShadowNode>& child : node->getChildren()) {
            collectFrames(child, absolute);
        }
    }

    PassThroughShadowTreeDelegate shadowTreeDelegate_;
    std::shared_ptr<const ContextContainer> contextContainer_{std::make_shared<ContextContainer>()};
    ViewComponentDescriptor viewDescriptor_{ComponentDescriptorParameters{
        .eventDispatcher = EventDispatcher::Shared{}, .contextContainer = contextContainer_, .flavor = nullptr}};
    facebook::react::ScrollViewComponentDescriptor scrollViewDescriptor_{ComponentDescriptorParameters{
        .eventDispatcher = EventDispatcher::Shared{}, .contextContainer = contextContainer_, .flavor = nullptr}};
    std::unique_ptr<ShadowTree> shadowTree_;
    facebook::react::ShadowTreeRevision committedRevision_;
    std::map<Tag, std::shared_ptr<const ShadowNode>> nodes_;
    std::map<Tag, Rect> frames_;
    std::map<Tag, Size> scrollViewContentSizes_;
};

#pragma mark - flexWrap line breaking

TEST_F(LayoutConformanceTest, FlexWrapWrapBreaksLinesWhenItemsExceedTheMainAxis) {
    const std::map<Tag, Rect>& frames = commitWrappingRow(wrappingRow(100), boxes({11, 12, 13}, 45, 40), 10);

    expectFrameNear(frames, 10, 0, 0, 100, 80);
    expectFrameNear(frames, 11, 0, 0, 45, 40);
    expectFrameNear(frames, 12, 45, 0, 45, 40);
    expectFrameNear(frames, 13, 0, 40, 45, 40);
}

TEST_F(LayoutConformanceTest, FlexWrapWrapReverseStacksTheFirstLineAtTheCrossEnd) {
    std::vector<NodeSpec> items = boxes({11, 12, 13}, 45, 40);

    const std::map<Tag, Rect>& frames = commitTree(
        LayoutConstraints{}, {NodeSpec{.tag = 10,
                                       .props = wrappingRow(100, folly::dynamic::object("flexWrap", "wrap-reverse")),
                                       .children = std::move(items)}});

    expectFrameNear(frames, 10, 0, 0, 100, 80);
    expectFrameNear(frames, 11, 0, 40, 45, 40);
    expectFrameNear(frames, 12, 45, 40, 45, 40);
    expectFrameNear(frames, 13, 0, 0, 45, 40);
}

TEST_F(LayoutConformanceTest, CoreIssue48527AlignItemsFlexEndDoesNotChangeWhereLinesBreak) {
    std::vector<NodeSpec> items = {NodeSpec{.tag = 11, .props = box(45, 40)}, NodeSpec{.tag = 12, .props = box(45, 20)},
                                   NodeSpec{.tag = 13, .props = box(45, 40)}};

    const std::map<Tag, Rect>& frames = commitTree(
        LayoutConstraints{}, {NodeSpec{.tag = 10,
                                       .props = wrappingRow(100, folly::dynamic::object("alignItems", "flex-end")),
                                       .children = std::move(items)}});

    expectFrameNear(frames, 10, 0, 0, 100, 80);
    expectFrameNear(frames, 11, 0, 0, 45, 40);
    expectFrameNear(frames, 12, 45, 20, 45, 20);
    expectFrameNear(frames, 13, 0, 40, 45, 40);
}

TEST_F(LayoutConformanceTest, CoreIssue49984AlignContentStretchDistributesLeftoverCrossSpaceAcrossLines) {
    folly::dynamic containerProps = wrappingRow(100, folly::dynamic::object("height", 120)("alignContent", "stretch"));

    const std::map<Tag, Rect>& frames = commitWrappingRow(
        containerProps,
        {NodeSpec{.tag = 11, .props = box(45, kIndefinite)}, NodeSpec{.tag = 12, .props = box(45, kIndefinite)},
         NodeSpec{.tag = 13, .props = box(45, kIndefinite)}, NodeSpec{.tag = 14, .props = box(45, kIndefinite)}},
        10);

    expectFrameNear(frames, 10, 0, 0, 100, 120);
    expectFrameNear(frames, 11, 0, 0, 45, 60);
    expectFrameNear(frames, 12, 45, 0, 45, 60);
    expectFrameNear(frames, 13, 0, 60, 45, 60);
    expectFrameNear(frames, 14, 45, 60, 45, 60);
}

TEST_F(LayoutConformanceTest, CoreIssue35351GapFlexWrapAndAlignContentSpaceBetweenSizeTheContainer) {
    folly::dynamic containerProps =
        wrappingRow(100, folly::dynamic::object("height", 100)("gap", 10)("alignContent", "space-between"));

    const std::map<Tag, Rect>& frames = commitWrappingRow(containerProps, boxes({11, 12, 13, 14}, 45, 20), 10);

    expectFrameNear(frames, 10, 0, 0, 100, 100);
    expectFrameNear(frames, 11, 0, 0, 45, 20);
    expectFrameNear(frames, 12, 55, 0, 45, 20);
    expectFrameNear(frames, 13, 0, 80, 45, 20);
    expectFrameNear(frames, 14, 55, 80, 45, 20);
}

TEST_F(LayoutConformanceTest, CoreIssue35351GapParticipatesInAvailableSpaceOnBothAxes) {
    std::vector<NodeSpec> items = boxes({11, 12, 13}, 30, 20);

    const std::map<Tag, Rect>& frames =
        commitTree(LayoutConstraints{}, {NodeSpec{.tag = 10,
                                                  .props = wrappingRow(100, folly::dynamic::object("gap", 10)),
                                                  .children = std::move(items)}});

    expectFrameNear(frames, 10, 0, 0, 100, 50);
    expectFrameNear(frames, 11, 0, 0, 30, 20);
    expectFrameNear(frames, 12, 40, 0, 30, 20);
    expectFrameNear(frames, 13, 0, 30, 30, 20);
}

TEST_F(LayoutConformanceTest, CoreIssue36024GapSizingInsideAScrollViewMatchesTheContainerOutsideOne) {
    std::vector<NodeSpec> items = boxes({21, 22, 23}, 30, 20);

    NodeSpec wrappingContainer{
        .tag = 11, .props = wrappingRow(100, folly::dynamic::object("gap", 10)), .children = std::move(items)};

    const std::map<Tag, Rect>& frames =
        commitTree(LayoutConstraints{}, {NodeSpec{.tag = 10,
                                                  .componentName = "ScrollView",
                                                  .props = folly::dynamic::object("width", 100)("height", 60),
                                                  .children = {std::move(wrappingContainer)}}});

    expectFrameNear(frames, 11, 0, 0, 100, 50);
    expectFrameNear(frames, 21, 0, 0, 30, 20);
    expectFrameNear(frames, 22, 40, 0, 30, 20);
    expectFrameNear(frames, 23, 0, 30, 30, 20);

    const Size scrollViewContentSize = contentSize(10);

    EXPECT_NEAR(scrollViewContentSize.width, 100, kFrameTolerance);
    EXPECT_NEAR(scrollViewContentSize.height, 50, kFrameTolerance);
}

TEST_F(LayoutConformanceTest, ColumnGapAndRowGapResolveIndependently) {
    folly::dynamic containerProps =
        wrappingRow(100, folly::dynamic::object("height", 100)("columnGap", 10)("rowGap", 5));

    const std::map<Tag, Rect>& frames = commitWrappingRow(containerProps, boxes({11, 12, 13, 14}, 40, 30), 10);

    expectFrameNear(frames, 11, 0, 0, 40, 30);
    expectFrameNear(frames, 12, 50, 0, 40, 30);
    expectFrameNear(frames, 13, 0, 35, 40, 30);
    expectFrameNear(frames, 14, 50, 35, 40, 30);
}

TEST_F(LayoutConformanceTest, PercentageGapsResolveAgainstTheContainerDimensionTheyAreDeclaredOn) {
    const std::map<Tag, Rect>& frames =
        commitWrappingRow(wrappingRow(200, folly::dynamic::object("height", 200)("columnGap", "20%")("rowGap", "25%")),
                          boxes({11, 12, 13}, 60, 20));

    expectFrameNear(frames, 10, 0, 0, 200, 200);
    expectFrameNear(frames, 11, 0, 0, 60, 20);
    expectFrameNear(frames, 12, 100, 0, 60, 20);
    expectFrameNear(frames, 13, 0, 70, 60, 20);
}

/**
 * Yoga deviates from CSS here: CSS Box Alignment resolves percentage gaps against the content box dimension and
 * treats them as zero when that dimension is indefinite, while Yoga resolves them against the resulting content
 * height — 25% of the two-line 40-point container — and then lets the shifted line overflow it. Pinned as
 * observed; see *Layout* in docs/cpp-toolchain.md.
 */
TEST_F(LayoutConformanceTest, PercentageRowGapWithIndefiniteHeightResolvesAgainstTheContentHeightLikeYogaNotCss) {
    const std::map<Tag, Rect>& frames =
        commitWrappingRow(wrappingRow(140, folly::dynamic::object("rowGap", "25%")), boxes({11, 12, 13}, 60, 20));

    expectFrameNear(frames, 10, 0, 0, 140, 40);
    expectFrameNear(frames, 11, 0, 0, 60, 20);
    expectFrameNear(frames, 12, 60, 0, 60, 20);
    expectFrameNear(frames, 13, 0, 30, 60, 20);
}

#pragma mark - aspectRatio constraint ordering (#117)

constexpr double kNoConstraint = -1.0;

folly::dynamic constrainedBox(double width, double height, folly::dynamic extra) {
    const folly::dynamic dimensions = box(width, height);
    folly::dynamic props = folly::dynamic::object();

    for (const auto& entry : dimensions.items()) {
        props[entry.first] = entry.second;
    }
    for (const auto& entry : extra.items()) {
        props[entry.first] = entry.second;
    }

    return props;
}

struct AspectRatioCase {
    const char* name;
    double width;
    double height;
    double minWidth;
    double maxWidth;
    double minHeight;
    double maxHeight;
    double expectedWidth;
    double expectedHeight;
};

/**
 * The constraint table of #57304, pinned as Yoga resolves it: the width anchors, the height is width / ratio even
 * when the height was itself definite, min/max on the anchoring axis clamp first and re-derive the other axis
 * through the ratio, and min/max on the derived axis clamp without propagating back. Contradictory constraints on
 * the derived axis resolve to the min. Contradictory constraints on the anchoring axis keep the min but leave the
 * height derived from the max-clamped width, so the ratio does not hold between the final axes — that deviation
 * from CSS is documented in docs/cpp-toolchain.md. Every case is a single item at the origin of a 300x300 row
 * container aligned to flex-start, so the derived axis is the only degree of freedom.
 */
TEST_F(LayoutConformanceTest, CoreIssue57304AspectRatioClampsAndReDerivesThroughTheRatio) {
    const double ratio = 2.0;
    const std::vector<AspectRatioCase> cases = {
        // A definite width derives the height; only the height constraints can bind.
        {.name = "widthAndRatio",
         .width = 100,
         .height = kNoConstraint,
         .minWidth = kNoConstraint,
         .maxWidth = kNoConstraint,
         .minHeight = kNoConstraint,
         .maxHeight = kNoConstraint,
         .expectedWidth = 100,
         .expectedHeight = 50},
        {.name = "widthAndRatioMaxHeight",
         .width = 100,
         .height = kNoConstraint,
         .minWidth = kNoConstraint,
         .maxWidth = kNoConstraint,
         .minHeight = kNoConstraint,
         .maxHeight = 30,
         .expectedWidth = 100,
         .expectedHeight = 30},
        {.name = "widthAndRatioMinHeight",
         .width = 100,
         .height = kNoConstraint,
         .minWidth = kNoConstraint,
         .maxWidth = kNoConstraint,
         .minHeight = 80,
         .maxHeight = kNoConstraint,
         .expectedWidth = 100,
         .expectedHeight = 80},
        {.name = "widthAndRatioUnbindingMaxWidth",
         .width = 100,
         .height = kNoConstraint,
         .minWidth = kNoConstraint,
         .maxWidth = 200,
         .minHeight = kNoConstraint,
         .maxHeight = kNoConstraint,
         .expectedWidth = 100,
         .expectedHeight = 50},
        {.name = "widthAndRatioUnbindingMinWidth",
         .width = 100,
         .height = kNoConstraint,
         .minWidth = 50,
         .maxWidth = kNoConstraint,
         .minHeight = kNoConstraint,
         .maxHeight = kNoConstraint,
         .expectedWidth = 100,
         .expectedHeight = 50},
        {.name = "widthAndRatioContradictoryHeightConstraintsResolveToTheMin",
         .width = 100,
         .height = kNoConstraint,
         .minWidth = kNoConstraint,
         .maxWidth = kNoConstraint,
         .minHeight = 200,
         .maxHeight = 30,
         .expectedWidth = 100,
         .expectedHeight = 200},
        // A definite height derives the width; only the width constraints can bind.
        {.name = "heightAndRatio",
         .width = kNoConstraint,
         .height = 60,
         .minWidth = kNoConstraint,
         .maxWidth = kNoConstraint,
         .minHeight = kNoConstraint,
         .maxHeight = kNoConstraint,
         .expectedWidth = 120,
         .expectedHeight = 60},
        {.name = "heightAndRatioMaxWidthReDerivesTheHeight",
         .width = kNoConstraint,
         .height = 60,
         .minWidth = kNoConstraint,
         .maxWidth = 100,
         .minHeight = kNoConstraint,
         .maxHeight = kNoConstraint,
         .expectedWidth = 100,
         .expectedHeight = 50},
        {.name = "heightAndRatioMinWidthReDerivesTheHeight",
         .width = kNoConstraint,
         .height = 60,
         .minWidth = 150,
         .maxWidth = kNoConstraint,
         .minHeight = kNoConstraint,
         .maxHeight = kNoConstraint,
         .expectedWidth = 150,
         .expectedHeight = 75},
        {.name = "heightAndRatioContradictoryWidthConstraintsKeepTheMinButTheHeightFollowsTheMax",
         .width = kNoConstraint,
         .height = 60,
         .minWidth = 150,
         .maxWidth = 100,
         .minHeight = kNoConstraint,
         .maxHeight = kNoConstraint,
         .expectedWidth = 150,
         .expectedHeight = 50},
        // Without a definite axis the ratio has nothing to derive from.
        {.name = "ratioAloneSizesToZero",
         .width = kNoConstraint,
         .height = kNoConstraint,
         .minWidth = kNoConstraint,
         .maxWidth = kNoConstraint,
         .minHeight = kNoConstraint,
         .maxHeight = kNoConstraint,
         .expectedWidth = 0,
         .expectedHeight = 0},
        {.name = "ratioAloneWithMaxWidthStaysZero",
         .width = kNoConstraint,
         .height = kNoConstraint,
         .minWidth = kNoConstraint,
         .maxWidth = 80,
         .minHeight = kNoConstraint,
         .maxHeight = kNoConstraint,
         .expectedWidth = 0,
         .expectedHeight = 0},
        // Two definite axes win over the ratio; min/max still clamp the definite values.
        {.name = "definiteAxesReAnchorOnTheWidth",
         .width = 100,
         .height = 40,
         .minWidth = kNoConstraint,
         .maxWidth = kNoConstraint,
         .minHeight = kNoConstraint,
         .maxHeight = kNoConstraint,
         .expectedWidth = 100,
         .expectedHeight = 50},
        {.name = "definiteAxesStillClampHeight",
         .width = 100,
         .height = 40,
         .minWidth = kNoConstraint,
         .maxWidth = kNoConstraint,
         .minHeight = kNoConstraint,
         .maxHeight = 30,
         .expectedWidth = 100,
         .expectedHeight = 30},
        {.name = "definiteAxesClampWidthAndReDeriveTheHeight",
         .width = 100,
         .height = 40,
         .minWidth = kNoConstraint,
         .maxWidth = 60,
         .minHeight = kNoConstraint,
         .maxHeight = kNoConstraint,
         .expectedWidth = 60,
         .expectedHeight = 30},
    };

    for (const AspectRatioCase& layoutCase : cases) {
        folly::dynamic extra = folly::dynamic::object("aspectRatio", ratio);

        if (layoutCase.minWidth != kNoConstraint) {
            extra["minWidth"] = layoutCase.minWidth;
        }
        if (layoutCase.maxWidth != kNoConstraint) {
            extra["maxWidth"] = layoutCase.maxWidth;
        }
        if (layoutCase.minHeight != kNoConstraint) {
            extra["minHeight"] = layoutCase.minHeight;
        }
        if (layoutCase.maxHeight != kNoConstraint) {
            extra["maxHeight"] = layoutCase.maxHeight;
        }

        const std::map<Tag, Rect>& frames = commitItemInRowContainer(
            NodeSpec{.tag = 11, .props = constrainedBox(layoutCase.width, layoutCase.height, extra)});

        SCOPED_TRACE(layoutCase.name);

        expectFrameNear(frames, 11, 0, 0, layoutCase.expectedWidth, layoutCase.expectedHeight);
    }
}

/**
 * The #35858 boundary: a ratio that is not a positive finite number must default to auto at the parse boundary
 * instead of reaching the layout engine. Zero and infinity are normalized by Yoga itself; NaN is Yoga's undefined;
 * negative values fall out of the derivation and are bounded to zero.
 */
TEST_F(LayoutConformanceTest, CoreIssue35858DegenerateAspectRatioValuesDefaultToAuto) {
    const std::vector<std::pair<const char*, folly::dynamic>> degenerateRatios = {
        {"zero", 0.0},
        {"negative", -2.0},
        {"notANumber", std::nan("")},
        {"infinity", std::numeric_limits<double>::infinity()},
    };

    for (const auto& [name, ratio] : degenerateRatios) {
        const std::map<Tag, Rect>& frames = commitItemInRowContainer(NodeSpec{
            .tag = 11, .props = constrainedBox(100, kNoConstraint, folly::dynamic::object("aspectRatio", ratio))});

        SCOPED_TRACE(name);

        expectFrameNear(frames, 11, 0, 0, 100, 0);
    }
}

#pragma mark - out-of-flow boxes against a padded parent (#116)

struct AbsolutePlacementCase {
    const char* name;
    folly::dynamic parentProps;
    folly::dynamic childProps;
    double expectedX;
    double expectedY;
    double expectedWidth = 40;
    double expectedHeight = 20;
};

folly::dynamic absoluteChild(folly::dynamic extra) {
    folly::dynamic props = folly::dynamic::object("position", "absolute")("width", 40)("height", 20);

    for (const auto& entry : extra.items()) {
        props[entry.first] = entry.second;
    }

    return props;
}

folly::dynamic paddedParent(folly::dynamic extra) {
    folly::dynamic props = folly::dynamic::object("width", 200)("height", 100);

    for (const auto& entry : extra.items()) {
        props[entry.first] = entry.second;
    }

    return props;
}

/**
 * The containing-block table of #46392 and #43206: where an absolutely positioned child sits and which box its
 * percentage sizes and insets resolve against. CSS names the padding box of the parent as the containing block;
 * the commit thread (Yoga, like every RN platform) resolves the percentage sizes and insets against the
 * content box and offsets insets by the border width alone, so padding moves neither a zero inset nor a
 * percentage — the one systematic deviation from the CSS rule this table pins. Half-point results land on the
 * pixel grid through Fabric's rounding, so the cases use percentages that resolve exactly.
 */
TEST_F(LayoutConformanceTest, CoreIssue46392AbsoluteChildPercentageResolutionAgainstAPaddedParent) {
    const std::vector<AbsolutePlacementCase> cases = {
        {.name = "noInsetsSitsAtTheOrigin",
         .parentProps = folly::dynamic::object(),
         .childProps = folly::dynamic::object("left", 0)("top", 0),
         .expectedX = 0,
         .expectedY = 0},
        {.name = "coreIssue46392PercentSizeResolvesAgainstTheContentBoxLikeYogaNotThePaddingBoxLikeCss",
         .parentProps = folly::dynamic::object("padding", 10)("borderWidth", 5),
         .childProps = folly::dynamic::object("left", 0)("top", 0)("width", "50%")("height", "50%"),
         .expectedX = 5,
         .expectedY = 5,
         .expectedWidth = 85,
         .expectedHeight = 35},
        {.name = "percentInsetsOffsetByTheBorderAndResolveAgainstTheContentBox",
         .parentProps = folly::dynamic::object("padding", 10)("borderWidth", 5),
         .childProps = folly::dynamic::object("left", "20%")("top", "10%"),
         .expectedX = 5 + 0.20 * 170,
         .expectedY = 5 + 0.10 * 70},
        {.name = "paddingAloneShiftsNothingForALeftZeroChild",
         .parentProps = folly::dynamic::object("padding", 10),
         .childProps = folly::dynamic::object("left", 0)("top", 0),
         .expectedX = 0,
         .expectedY = 0},
        {.name = "borderShiftsALeftZeroChildToThePaddingBoxOrigin",
         .parentProps = folly::dynamic::object("borderWidth", 5),
         .childProps = folly::dynamic::object("left", 0)("top", 0),
         .expectedX = 5,
         .expectedY = 5},
        {.name = "marginOnTheParentShiftsTheWholeContainingBlock",
         .parentProps = folly::dynamic::object("margin", 15)("borderWidth", 5),
         .childProps = folly::dynamic::object("left", 0)("top", 0),
         .expectedX = 20,
         .expectedY = 20},
    };

    for (const AbsolutePlacementCase& layoutCase : cases) {
        const std::map<Tag, Rect>& frames =
            commitTree(LayoutConstraints{}, {NodeSpec{.tag = 10,
                                                      .props = paddedParent(layoutCase.parentProps),
                                                      .children = std::vector<NodeSpec>{NodeSpec{
                                                          .tag = 11, .props = absoluteChild(layoutCase.childProps)}}}});

        SCOPED_TRACE(layoutCase.name);

        expectFrameNear(frames, 11, layoutCase.expectedX, layoutCase.expectedY, layoutCase.expectedWidth,
                        layoutCase.expectedHeight);
    }
}

TEST_F(LayoutConformanceTest, CoreIssue43206PercentInsetsAfterAddingPaddingResolveAgainstTheContentBox) {
    const std::map<Tag, Rect>& frames = commitTree(
        LayoutConstraints{},
        {NodeSpec{.tag = 10,
                  .props = paddedParent(folly::dynamic::object("padding", 20)("paddingTop", 30)),
                  .children = std::vector<NodeSpec>{NodeSpec{
                      .tag = 11, .props = absoluteChild(folly::dynamic::object("left", "10%")("top", "20%"))}}}});

    expectFrameNear(frames, 11, 0.10 * 160, 0.20 * 50, 40, 20);
}

TEST_F(LayoutConformanceTest, CoreIssue34542AbsoluteChildOutsideTheParentBoundsKeepsItsFullFrame) {
    const std::map<Tag, Rect>& frames = commitTree(
        LayoutConstraints{},
        {NodeSpec{.tag = 10,
                  .props = paddedParent(folly::dynamic::object()),
                  .children = std::vector<NodeSpec>{NodeSpec{.tag = 11,
                                                             .props = absoluteChild(folly::dynamic::object("left", 150)(
                                                                 "top", 60)("width", 100)("height", 80))}}}});

    expectFrameNear(frames, 11, 150, 60, 100, 80);
}

/**
 * The re-read table of #47979: every style property that reaches Yoga must change the layout when it changes. Each
 * row commits one base tree, then re-commits the same tree with a single property mutated on either the parent or
 * the child — the clone-with-new-props path React's updates take, not a tree rebuild — and asserts the metric the
 * mutated property is supposed to move. A property that stops reaching Yoga fails its row instead of silently
 * laying out the stale style, which is the failure mode core#47979 reports for flexBasis.
 */
TEST_F(LayoutConformanceTest, CoreIssue47979EveryStylePropertyThatReachesYogaIsReReadWhenItChanges) {
    struct StyleUpdateCase {
        const char* name;
        Tag targetTag;
        folly::dynamic delta;
        Tag assertedTag;
        double expectedX;
        double expectedY;
        double expectedWidth;
        double expectedHeight;
    };

    const std::vector<StyleUpdateCase> cases = {
        {.name = "childWidth",
         .targetTag = 11,
         .delta = folly::dynamic::object("width", 80),
         .assertedTag = 11,
         .expectedX = 0,
         .expectedY = 0,
         .expectedWidth = 80,
         .expectedHeight = 20},
        {.name = "childHeight",
         .targetTag = 11,
         .delta = folly::dynamic::object("height", 60),
         .assertedTag = 11,
         .expectedX = 0,
         .expectedY = 0,
         .expectedWidth = 40,
         .expectedHeight = 60},
        {.name = "childRelativeLeft",
         .targetTag = 11,
         .delta = folly::dynamic::object("left", 25),
         .assertedTag = 11,
         .expectedX = 25,
         .expectedY = 0,
         .expectedWidth = 40,
         .expectedHeight = 20},
        {.name = "childMarginTop",
         .targetTag = 11,
         .delta = folly::dynamic::object("marginTop", 10),
         .assertedTag = 11,
         .expectedX = 0,
         .expectedY = 10,
         .expectedWidth = 40,
         .expectedHeight = 20},
        {.name = "childAspectRatio",
         .targetTag = 11,
         .delta = folly::dynamic::object("aspectRatio", 0.5),
         .assertedTag = 11,
         .expectedX = 0,
         .expectedY = 0,
         .expectedWidth = 40,
         .expectedHeight = 80},
        {.name = "childMinWidth",
         .targetTag = 11,
         .delta = folly::dynamic::object("minWidth", 90),
         .assertedTag = 11,
         .expectedX = 0,
         .expectedY = 0,
         .expectedWidth = 90,
         .expectedHeight = 20},
        {.name = "parentPaddingLeft",
         .targetTag = 10,
         .delta = folly::dynamic::object("paddingLeft", 20),
         .assertedTag = 11,
         .expectedX = 20,
         .expectedY = 0,
         .expectedWidth = 40,
         .expectedHeight = 20},
        {.name = "parentBorderWidth",
         .targetTag = 10,
         .delta = folly::dynamic::object("borderWidth", 8),
         .assertedTag = 11,
         .expectedX = 8,
         .expectedY = 8,
         .expectedWidth = 40,
         .expectedHeight = 20},
        {.name = "parentGap",
         .targetTag = 10,
         .delta = folly::dynamic::object("gap", 12),
         .assertedTag = 12,
         .expectedX = 52,
         .expectedY = 0,
         .expectedWidth = 30,
         .expectedHeight = 20},
        {.name = "parentFlexDirection",
         .targetTag = 10,
         .delta = folly::dynamic::object("flexDirection", "column"),
         .assertedTag = 12,
         .expectedX = 0,
         .expectedY = 20,
         .expectedWidth = 30,
         .expectedHeight = 20},
    };

    for (const StyleUpdateCase& layoutCase : cases) {
        commitTree(LayoutConstraints{},
                   {NodeSpec{.tag = 10,
                             .props = paddedParent(folly::dynamic::object("flexDirection", "row")),
                             .children = std::vector<NodeSpec>{NodeSpec{.tag = 11, .props = box(40, 20)},
                                                               NodeSpec{.tag = 12, .props = box(30, 20)}}}});

        const std::map<Tag, Rect>& frames = recommitWithStyle(layoutCase.targetTag, layoutCase.delta);

        SCOPED_TRACE(layoutCase.name);

        expectFrameNear(frames, layoutCase.assertedTag, layoutCase.expectedX, layoutCase.expectedY,
                        layoutCase.expectedWidth, layoutCase.expectedHeight);
    }
}

#pragma mark - the commit contract under adversarial tree mutation (#120)

/**
 * The conditional-rendering cycle of core#52349: children added, removed, replaced and reordered between commits
 * must keep the layout of the surviving children correct every round, with upstream's assertions live (the test
 * preset is a Debug build). A structural change here is the same input to Yoga the differ produces when React
 * commits a new tree.
 */
TEST_F(LayoutConformanceTest, CoreIssue52349ConditionalRenderingCyclesKeepTheLayoutCorrectEveryRound) {
    const std::vector<NodeSpec> baseChildren = {NodeSpec{.tag = 11, .props = box(40, 20)},
                                                NodeSpec{.tag = 12, .props = box(30, 20)}};

    const std::vector<std::vector<NodeSpec>> rounds = {
        baseChildren,
        {NodeSpec{.tag = 12, .props = box(30, 20)}},
        {NodeSpec{.tag = 12, .props = box(30, 20)}, NodeSpec{.tag = 13, .props = box(50, 20)}},
        {NodeSpec{.tag = 13, .props = box(50, 20)}},
        {NodeSpec{.tag = 11, .props = box(40, 20)}, NodeSpec{.tag = 13, .props = box(50, 20)}},
        {},
        baseChildren,
    };

    for (const std::vector<NodeSpec>& children : rounds) {
        const std::map<Tag, Rect>& frames = commitTree(LayoutConstraints{}, {rowParent(10, 200, children)});

        double expectedX = 0;

        for (const NodeSpec& child : children) {
            const Rect& frame = frames.at(child.tag);

            EXPECT_NEAR(frame.origin.x, expectedX, kFrameTolerance) << "tag " << child.tag << " origin.x";
            EXPECT_NEAR(frame.size.width, child.props["width"].asDouble(), kFrameTolerance)
                << "tag " << child.tag << " size.width";

            expectedX += frame.size.width;
        }
    }
}

/**
 * Moving content between parents is supported through a fresh family: the item that lived under parent A is
 * removed there and a same-spec item (new family) is added under parent B, which lands at B's position. What the
 * mounting layer must never do is move one family across parents: ShadowNodeFamily::setParent asserts
 * first-parent-wins (ShadowNodeFamily.cpp:37), which is the exact mechanism of the still-open core#52349
 * assertion, so that shape is a documented landmine rather than a supported input.
 */
TEST_F(LayoutConformanceTest, CoreIssue52349ContentMovesBetweenParentsThroughAFreshFamily) {
    const std::map<Tag, Rect>& frames =
        commitTree(LayoutConstraints{},
                   {rowParent(10, 300,
                              {NodeSpec{.tag = 20,
                                        .props = box(100, 100),
                                        .children = std::vector<NodeSpec>{NodeSpec{.tag = 21, .props = box(40, 20)}}},
                               NodeSpec{.tag = 22, .props = box(100, 100), .children = {}}})});

    expectFrameNear(frames, 21, 0, 0, 40, 20);

    const std::map<Tag, Rect>& movedFrames = commitTree(
        LayoutConstraints{},
        {rowParent(10, 300,
                   {NodeSpec{.tag = 20, .props = box(100, 100), .children = {}},
                    NodeSpec{.tag = 22,
                             .props = box(100, 100),
                             .children = std::vector<NodeSpec>{NodeSpec{.tag = 23, .props = box(40, 20)}}}})});

    expectFrameNear(movedFrames, 23, 100, 0, 40, 20);
}

/**
 * The determinism half of the measure-callback contract, pinned one level up: committing the identical tree
 * repeatedly into the same ShadowTree must produce identical frames on the cold commit and on every warm one, so
 * nothing behind the layout (a cache, a pending state update) can change an answer between rounds.
 */
TEST_F(LayoutConformanceTest, RepeatedIdenticalCommitsProduceIdenticalFramesColdAndWarm) {
    for (int round = 0; round < 3; round++) {
        const std::map<Tag, Rect>& frames = commitTree(
            LayoutConstraints{},
            {rowParent(10, 200,
                       {NodeSpec{.tag = 11, .props = box(40, 20)}, NodeSpec{.tag = 12, .props = box(30, 20)}})});

        expectFrameNear(frames, 11, 0, 0, 40, 20);
        expectFrameNear(frames, 12, 40, 0, 30, 20);
    }
}

/**
 * The concurrent half: two threads commit competing trees into one ShadowTree, which is this platform's default
 * shape. After both finish, the surviving revision must be exactly one of the competing trees — a child of
 * 100x50 alone, or two children of 40x20 stacked — never a mixture of both. Run under TSan as well as plain
 * CTest; the commits are the only thing racing, and every read happens after the join.
 */
TEST_F(LayoutConformanceTest, ConcurrentCompetingCommitsLandExactlyOneTree) {
    constexpr int kRoundsPerThread = 32;

    shadowTree_ = std::make_unique<ShadowTree>(kSurfaceId, LayoutConstraints{}, LayoutContext{}, shadowTreeDelegate_,
                                               *contextContainer_);

    const ShadowTreeCommitOptions commitOptions{.enableStateReconciliation = false, .mountSynchronously = true};

    const auto commitShape = [this, &commitOptions](bool singleChild) {
        std::vector<NodeSpec> children = singleChild ? std::vector<NodeSpec>{NodeSpec{.tag = 11, .props = box(100, 50)}}
                                                     : std::vector<NodeSpec>{NodeSpec{.tag = 12, .props = box(40, 20)},
                                                                             NodeSpec{.tag = 13, .props = box(40, 20)}};

        shadowTree_->commit(
            [&](const RootShadowNode& oldRootShadowNode) { return cloneRootWith(oldRootShadowNode, children); },
            commitOptions);
    };

    std::latch startLatch{2};

    std::thread singleChildThread([&] {
        startLatch.arrive_and_wait();

        for (int round = 0; round < kRoundsPerThread; round++) {
            commitShape(true);
        }
    });

    std::thread twoChildrenThread([&] {
        startLatch.arrive_and_wait();

        for (int round = 0; round < kRoundsPerThread; round++) {
            commitShape(false);
        }
    });

    singleChildThread.join();
    twoChildrenThread.join();

    committedRevision_ = shadowTree_->getCurrentRevision();

    frames_.clear();
    nodes_.clear();
    scrollViewContentSizes_.clear();
    collectFrames(committedRevision_.rootShadowNode, Point{});

    const bool isSingleChildShape = frames_.count(11) == 1 && frames_.count(12) == 0;
    const bool isTwoChildrenShape = frames_.count(11) == 0 && frames_.count(12) == 1;

    ASSERT_TRUE(isSingleChildShape || isTwoChildrenShape)
        << "the surviving revision is a mixture of the two competing trees";

    if (isSingleChildShape) {
        expectFrameNear(frames_, 11, 0, 0, 100, 50);
    } else {
        expectFrameNear(frames_, 12, 0, 0, 40, 20);
        expectFrameNear(frames_, 13, 0, 20, 40, 20);
    }
}

} // namespace
