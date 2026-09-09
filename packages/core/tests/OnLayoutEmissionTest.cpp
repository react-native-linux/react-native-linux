#include "RecordingEventDispatcher.h"
#include "ShadowTreeTestSupport.h"

#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <string>
#include <vector>

// When a resize is allowed to wake JavaScript up, and at which output scale two frames count as the same one
// (#435). The defect the issue imports from upstream — `onLayout` firing per configure with values that did not
// change — does not reproduce here, and this file is the measurement that says so rather than a fix for it.
//
// What a configure actually does, measured at the two layers it passes through:
//
//  1. `RootShadowNode::clone(parserContext, layoutConstraints, layoutContext)` dirties the root, so the layout
//     pass visits every node under it and marks *all* of them affected — including nodes whose metrics come out
//     byte-identical. `YogaLayoutableShadowNode::layout` does this deliberately (its comment names D22999891),
//     so `ShadowTree::emitLayoutEvents` is handed the whole visited set on every configure.
//  2. `BaseViewEventEmitter::onLayout` is the gate. It holds the last `frame` it dispatched to JavaScript and
//     returns without dispatching when the new one equals it, and collapses a second dispatch while one is in
//     flight. So the redundant set from step 1 costs no JavaScript wake-up.
//
// The equality in step 2 is exact `Rect` comparison, and that is already "equal at the scale we are presenting
// at": Yoga rounds every laid-out frame onto the pixel grid of `LayoutContext::pointScaleFactor`, so two frames
// that are equal at that scale are equal in raw floats and the reverse. `AHalfUnitOfWidth...` below is that
// statement as a measurement — the same pair of surface widths is one frame at scale 1.0 and two at scale 2.0.
// An `RCTSafeAreaViewComponentView`-style `1.0 / scale + 0.01` epsilon on top of this would be *finer* than the
// grid the values already sit on, so it could only emit more events, never fewer.
//
// The scale these cases pass in `LayoutContext` is not the scale a running window uses: `FabricHost` commits
// `LayoutContext{}`, so the shipped pixel grid is the logical one at 1.0 regardless of the Wayland output scale.
// That is a coarser grid than the physical one, which can merge two frames that differ by less than a logical
// pixel but can never split one — it cannot produce a redundant event. Where the output scale comes from is
// #113; this file pins the grid behaviour so that plumbing it in later has to keep it.
//
// The event-count case is bounded by what the unit tier can see: with no `jsi::Runtime`, the payload closure
// that clears `isDispatching` never runs, so the emitter reports at most one in-flight `topLayout` per node. It
// is asserted anyway because it is the upper bound the issue asks for, and `AnAddedNode...` proves the count is
// live rather than frozen.
namespace {

constexpr SurfaceId kSurfaceId = 1;
constexpr Tag kFixedTag = 10;
constexpr Tag kFlexibleTag = 11;
constexpr Tag kAddedTag = 12;
constexpr float kSurfaceHeight = 600.0F;
constexpr float kInitialWidth = 400.0F;
constexpr float kResizedWidth = 800.0F;
constexpr float kFixedWidth = 100.0F;
constexpr float kFixedHeight = 50.0F;
constexpr int kConfigureCount = 5;
constexpr int kSettleCommitBound = 2;
constexpr int32_t kOpaqueBlue = static_cast<int32_t>(0xFF3366CCU);

folly::dynamic reportingViewProps(folly::dynamic props) {
    props["backgroundColor"] = kOpaqueBlue;
    props["onLayout"] = true;

    return props;
}

class OnLayoutEmissionTest : public ::testing::Test {
protected:
    void SetUp() override {
        uiManager_ = react_native_linux::makeTaskDroppingUIManager(contextContainer_);
        shadowTree_ = react_native_linux::addRegisteredShadowTree(*uiManager_, shadowTreeDelegate_, *contextContainer_,
                                                                  kSurfaceId);
        configure(kInitialWidth, 1.0F);
        commitChildren(makeReportingChildren());
        shadowTree_->getMountingCoordinator()->pullTransaction();
    }

    void TearDown() override { react_native_linux::removeShadowTree(*uiManager_, kSurfaceId); }

    /**
     * One `xdg_toplevel.configure`, as `FabricHost::setSurfaceSize` performs it: the root cloned with new
     * constraints and the layout context the surface is presenting at, committed once.
     */
    void configure(float width, float pointScaleFactor) {
        const PropsParserContext parserContext{kSurfaceId, *contextContainer_};

        shadowTree_->commit(
            [this, &parserContext, width, pointScaleFactor](const RootShadowNode& oldRootShadowNode) {
                return rootClonedFor(oldRootShadowNode, parserContext, width, pointScaleFactor);
            },
            {});
    }

    RootShadowNode::Unshared rootClonedFor(const RootShadowNode& oldRootShadowNode,
                                           const PropsParserContext& parserContext, float width,
                                           float pointScaleFactor) {
        return oldRootShadowNode.clone(
            parserContext, react_native_linux::exactSurfaceConstraints(Size{.width = width, .height = kSurfaceHeight}),
            LayoutContext{.pointScaleFactor = pointScaleFactor});
    }

    /**
     * The set `ShadowTree::commit` would hand `emitLayoutEvents` for this configure, laid out on a candidate root
     * exactly as commit does it, without committing the result.
     */
    std::vector<Tag> nodesMarkedAffectedBy(float width, float pointScaleFactor) {
        const PropsParserContext parserContext{kSurfaceId, *contextContainer_};
        const RootShadowNode::Unshared candidateRoot =
            rootClonedFor(*shadowTree_->getCurrentRevision().rootShadowNode, parserContext, width, pointScaleFactor);
        std::vector<const LayoutableShadowNode*> affectedNodes;

        candidateRoot->layoutIfNeeded(&affectedNodes);

        std::vector<Tag> tags;

        for (const LayoutableShadowNode* affectedNode : affectedNodes) {
            tags.push_back(dynamic_cast<const ShadowNode&>(*affectedNode).getTag());
        }

        return tags;
    }

    std::vector<std::shared_ptr<const ShadowNode>> makeReportingChildren() {
        return {makeReportingView(kFixedTag, folly::dynamic::object("width", kFixedWidth)("height", kFixedHeight)),
                makeReportingView(kFlexibleTag, folly::dynamic::object("width", "50%")("height", kFixedHeight))};
    }

    std::shared_ptr<const ShadowNode> makeReportingView(Tag tag, folly::dynamic props) {
        return react_native_linux::makeConfiguredShadowNode(
            viewDescriptor_, tag, kSurfaceId, contextContainer_, reportingViewProps(std::move(props)),
            std::make_shared<const std::vector<std::shared_ptr<const ShadowNode>>>());
    }

    void commitChildren(std::vector<std::shared_ptr<const ShadowNode>> children) {
        const auto childList =
            std::make_shared<const std::vector<std::shared_ptr<const ShadowNode>>>(std::move(children));

        shadowTree_->commit(
            [&childList](const RootShadowNode& oldRootShadowNode) {
                return react_native_linux::cloneRootWithChildren(oldRootShadowNode, childList);
            },
            {});
    }

    /**
     * A layout handler writing back what it was told: the flexible node re-committed with its own reported width
     * as an explicit style width, on the same family, the way a `setState` from `onLayout` would arrive.
     */
    void writeBackReportedWidth() {
        const PropsParserContext parserContext{kSurfaceId, *contextContainer_};
        const std::shared_ptr<const ShadowNode> flexibleNode = childWithTag(kFlexibleTag);
        folly::dynamic writtenBack = folly::dynamic::object("width", frames().at(kFlexibleTag).size.width);
        facebook::react::RawProps rawProps{std::move(writtenBack)};
        const std::shared_ptr<const ShadowNode> rewritten = flexibleNode->clone(ShadowNodeFragment{
            .props = viewDescriptor_.cloneProps(parserContext, flexibleNode->getProps(), std::move(rawProps))});

        commitChildren({childWithTag(kFixedTag), rewritten});
    }

    std::shared_ptr<const ShadowNode> childWithTag(Tag tag) {
        for (const std::shared_ptr<const ShadowNode>& child :
             shadowTree_->getCurrentRevision().rootShadowNode->getChildren()) {
            if (child->getTag() == tag) {
                return child;
            }
        }

        return nullptr;
    }

    std::map<Tag, Rect> frames() {
        std::map<Tag, Rect> collectedFrames;

        react_native_linux::collectAbsoluteFrames(shadowTree_->getCurrentRevision().rootShadowNode,
                                                  Point{.x = 0, .y = 0}, collectedFrames,
                                                  [](const std::shared_ptr<const ShadowNode>&) {});

        return collectedFrames;
    }

    size_t layoutEventCount() const {
        size_t count = 0;

        for (const std::string& eventType : *recordedEventTypes_) {
            if (eventType == "topLayout") {
                count++;
            }
        }

        return count;
    }

    std::shared_ptr<std::vector<std::string>> recordedEventTypes_{std::make_shared<std::vector<std::string>>()};
    std::shared_ptr<const ContextContainer> contextContainer_{std::make_shared<ContextContainer>()};
    std::shared_ptr<const EventDispatcher> eventDispatcher_{
        react_native_linux::makeRecordingEventDispatcher(recordedEventTypes_)};
    std::shared_ptr<facebook::react::UIManager> uiManager_;
    ViewComponentDescriptor viewDescriptor_{ComponentDescriptorParameters{
        .eventDispatcher = eventDispatcher_, .contextContainer = contextContainer_, .flavor = nullptr}};
    react_native_linux::PassThroughShadowTreeDelegate shadowTreeDelegate_;
    ShadowTree* shadowTree_{nullptr};
};

TEST_F(OnLayoutEmissionTest, AConfigureOfTheSameSizeStillMarksEveryNodeAffectedAndChangesNoFrame) {
    const std::map<Tag, Rect> before = frames();

    EXPECT_EQ(nodesMarkedAffectedBy(kInitialWidth, 1.0F), (std::vector<Tag>{kFixedTag, kFlexibleTag}));

    configure(kInitialWidth, 1.0F);

    EXPECT_EQ(frames(), before);
}

TEST_F(OnLayoutEmissionTest, AResizeLeavesAFixedSizedNodesFrameUntouchedWhileTheFlexibleOneFollows) {
    const std::map<Tag, Rect> before = frames();

    configure(kResizedWidth, 1.0F);

    const std::map<Tag, Rect> after = frames();

    EXPECT_EQ(after.at(kFixedTag), before.at(kFixedTag));
    EXPECT_EQ(before.at(kFlexibleTag).size.width, kInitialWidth / 2);
    EXPECT_EQ(after.at(kFlexibleTag).size.width, kResizedWidth / 2);
}

TEST_F(OnLayoutEmissionTest, EveryFrameLandsOnThePixelGridOfTheOutputScaleItWasLaidOutAt) {
    for (float pointScaleFactor : {1.0F, 1.25F, 1.5F, 2.0F}) {
        configure(kInitialWidth + 1, pointScaleFactor);

        const float laidOutWidth = frames().at(kFlexibleTag).size.width;
        const float physicalPixels = laidOutWidth * pointScaleFactor;

        EXPECT_FLOAT_EQ(physicalPixels, std::round(physicalPixels)) << "scale " << pointScaleFactor;
    }
}

/**
 * The scale-dependent half of the question, as a measurement rather than an epsilon: half a logical unit of
 * surface width is below the pixel grid at scale 1.0, so the two configures produce one frame and one event's
 * worth of change, and it is above the grid at scale 2.0, so the same two produce two.
 */
TEST_F(OnLayoutEmissionTest, AHalfUnitOfWidthIsOneFrameAtScaleOneAndTwoFramesAtScaleTwo) {
    configure(kInitialWidth + 1, 1.0F);
    const Rect firstAtScaleOne = frames().at(kFlexibleTag);
    configure(kInitialWidth + 2, 1.0F);
    const Rect secondAtScaleOne = frames().at(kFlexibleTag);

    configure(kInitialWidth + 1, 2.0F);
    const Rect firstAtScaleTwo = frames().at(kFlexibleTag);
    configure(kInitialWidth + 2, 2.0F);
    const Rect secondAtScaleTwo = frames().at(kFlexibleTag);

    EXPECT_EQ(firstAtScaleOne, secondAtScaleOne);
    EXPECT_NE(firstAtScaleTwo, secondAtScaleTwo);
}

TEST_F(OnLayoutEmissionTest, ConfiguresOfTheSameSizeProduceOneLayoutEventPerReportingNode) {
    for (int configureIndex = 0; configureIndex < kConfigureCount; configureIndex++) {
        configure(kInitialWidth, 1.0F);
    }

    EXPECT_EQ(layoutEventCount(), 2U);
}

TEST_F(OnLayoutEmissionTest, AnAddedReportingNodeIsTheOnlyThingThatAddsAnEvent) {
    for (int configureIndex = 0; configureIndex < kConfigureCount; configureIndex++) {
        configure(kResizedWidth + static_cast<float>(configureIndex), 1.0F);
    }

    EXPECT_EQ(layoutEventCount(), 2U);

    commitChildren(
        {childWithTag(kFixedTag), childWithTag(kFlexibleTag),
         makeReportingView(kAddedTag, folly::dynamic::object("width", kFixedWidth)("height", kFixedHeight))});

    EXPECT_EQ(layoutEventCount(), 3U);
}

/**
 * The feedback fixture: a handler that writes its reported width back as its own style width settles, and the
 * bound is asserted rather than assumed. Fractional scale is the case that could fail to settle — a value read
 * off the pixel grid and written back has to land on the same grid point again — so it is the scale used.
 */
TEST_F(OnLayoutEmissionTest, ALayoutHandlerWritingBackItsReportedWidthSettlesWithinTheCommitBound) {
    configure(kInitialWidth + 1, 1.25F);

    std::map<Tag, Rect> before = frames();
    int commitsUntilSettled = 0;

    for (int commitIndex = 0; commitIndex < kSettleCommitBound; commitIndex++) {
        writeBackReportedWidth();
        commitsUntilSettled++;

        if (frames() == before) {
            break;
        }

        before = frames();
    }

    EXPECT_EQ(commitsUntilSettled, 1);
    EXPECT_EQ(frames(), before);
}

} // namespace
