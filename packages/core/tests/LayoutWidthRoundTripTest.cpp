#include <cmath>
#include <gtest/gtest.h>
#include <yoga/Yoga.h>

namespace {

constexpr float kPortraitWidth = 400;
constexpr float kPortraitHeight = 800;
constexpr float kLandscapeWidth = 800;
constexpr float kLandscapeHeight = 400;
constexpr float kCellPadding = 16;
constexpr float kFixedSiblingHeight = 24;
constexpr float kTotalTextWidth = 1840;
constexpr float kLineHeight = 20;
constexpr float kFiveLines = 100;
constexpr float kThreeLines = 60;

/**
 * A paragraph that rewraps: the same total advance width broken into as many lines as the available width needs,
 * which is the only property of `TextLayoutManager::measure` this suite depends on. The measured text itself is
 * not the subject — the number of times Yoga asks, and what it does with the answer it kept, is.
 */
YGSize measureRewrappingText(YGNodeConstRef /*node*/, float width, YGMeasureMode widthMode, float /*height*/,
                             YGMeasureMode /*heightMode*/) {
    const float availableWidth =
        (widthMode == YGMeasureModeUndefined || std::isnan(width)) ? kTotalTextWidth : std::max(1.0F, width);
    const float lineCount = std::max(1.0F, std::ceil(kTotalTextWidth / availableWidth));

    return YGSize{.width = std::min(availableWidth, kTotalTextWidth), .height = lineCount * kLineHeight};
}

/**
 * The tree facebook/react-native#58294 and facebook/yoga#2019 report, built directly on Yoga because that is
 * where the reporter root-caused it and where the reproduction is exact: a scrolling container whose content is
 * sized at an undefined height, a padded cell, a `flex: 1` column, a fixed-height sibling, `wrapperCount` plain
 * views, and a `flex: 1` measured paragraph at the bottom. Every ingredient is load-bearing; `wrapperCount`
 * decides whether the defect appears, which is why it is the one parameter.
 */
class RotationHarness final {
public:
    explicit RotationHarness(int wrapperCount) {
        YGNodeStyleSetOverflow(scrollView_, YGOverflowScroll);
        YGNodeStyleSetFlexGrow(scrollView_, 1);
        YGNodeInsertChild(root_, scrollView_, 0);

        const YGNodeRef cell = YGNodeNew();
        YGNodeStyleSetPadding(cell, YGEdgeAll, kCellPadding);
        YGNodeInsertChild(scrollView_, cell, 0);

        const YGNodeRef flexColumn = YGNodeNew();
        YGNodeStyleSetFlex(flexColumn, 1);
        YGNodeInsertChild(cell, flexColumn, 0);

        const YGNodeRef fixedSibling = YGNodeNew();
        YGNodeStyleSetHeight(fixedSibling, kFixedSiblingHeight);
        YGNodeInsertChild(flexColumn, fixedSibling, 0);

        YGNodeRef textParent = flexColumn;

        for (int wrapper = 0; wrapper < wrapperCount; ++wrapper) {
            const YGNodeRef wrapperNode = YGNodeNew();
            YGNodeInsertChild(textParent, wrapperNode, wrapper == 0 ? 1 : 0);
            textParent = wrapperNode;
        }

        YGNodeStyleSetFlex(text_, 1);
        YGNodeSetMeasureFunc(text_, measureRewrappingText);
        YGNodeInsertChild(textParent, text_, 0);
    }

    RotationHarness(const RotationHarness&) = delete;
    RotationHarness(RotationHarness&&) = delete;
    RotationHarness& operator=(const RotationHarness&) = delete;
    RotationHarness& operator=(RotationHarness&&) = delete;

    ~RotationHarness() { YGNodeFreeRecursive(root_); }

    float layoutAt(float width, float height) const {
        YGNodeStyleSetWidth(root_, width);
        YGNodeStyleSetHeight(root_, height);
        YGNodeCalculateLayout(root_, YGUndefined, YGUndefined, YGDirectionLTR);

        return YGNodeLayoutGetHeight(text_);
    }

    void markTextDirty() const { YGNodeMarkDirty(text_); }

private:
    YGNodeRef root_ = YGNodeNew();
    YGNodeRef scrollView_ = YGNodeNew();
    YGNodeRef text_ = YGNodeNew();
};

/**
 * The defect, pinned at the value it currently produces rather than the value it should.
 *
 * `layout.computedFlexBasis` is a cross-layout cache that Yoga never validates against the layout that filled it.
 * `computeFlexBasisForChild` writes a resolved basis only when none is stored yet — ReactCommon/yoga/yoga/
 * algorithm/CalculateLayout.cpp, the `useResolvedFlexBasis` branch:
 *
 *     if (child->getLayout().computedFlexBasis.isUndefined() ||
 *         (child->getConfig()->isExperimentalFeatureEnabled(ExperimentalFeature::WebFlexBasis) &&
 *          child->getLayout().computedFlexBasisGeneration != generationCount)) {
 *
 * so the basis measured under the landscape constraint survives into the second portrait layout. The only place
 * that clears it is `Node::markDirtyAndPropagate`, which walks up to the owners and never down, and a resize
 * touches the root alone. `computedFlexBasisGeneration` exists for exactly this comparison but is consulted only
 * under `WebFlexBasis`.
 *
 * This lives in `third_party/react-native`, which is re-cloned from a pinned tag and cannot carry a patch, and it
 * is not reachable from `packages/core` without dirtying every measured node on every resize frame. So the
 * contract is pinned here instead: the day the pin moves to a Yoga that fixes facebook/yoga#2019, this test goes
 * red, and the expectation below becomes `kFiveLines` — which is what the two tests after it already assert for
 * every shape that is not the reported one.
 *
 * facebook/react-native#58294, facebook/yoga#2019, react-native-linux#433.
 */
TEST(LayoutWidthRoundTripTest, TwoWrappersKeepTheLandscapeTextHeightOnTheWayBackToPortrait) {
    const RotationHarness harness{2};

    EXPECT_FLOAT_EQ(harness.layoutAt(kPortraitWidth, kPortraitHeight), kFiveLines);
    EXPECT_FLOAT_EQ(harness.layoutAt(kLandscapeWidth, kLandscapeHeight), kThreeLines);
    EXPECT_FLOAT_EQ(harness.layoutAt(kPortraitWidth, kPortraitHeight), kThreeLines);
}

/**
 * The ingredient boundary the upstream report establishes by removing one wrapper at a time: with a single plain
 * view between the `flex: 1` column and the paragraph, that view's own measurement cache still hits and the
 * paragraph is stretched to a correct height, so the stale basis is masked. This is what makes the case above a
 * defect in a specific shape rather than a broken harness.
 */
TEST(LayoutWidthRoundTripTest, OneWrapperRoundTripsBackToThePortraitTextHeight) {
    const RotationHarness harness{1};

    EXPECT_FLOAT_EQ(harness.layoutAt(kPortraitWidth, kPortraitHeight), kFiveLines);
    EXPECT_FLOAT_EQ(harness.layoutAt(kLandscapeWidth, kLandscapeHeight), kThreeLines);
    EXPECT_FLOAT_EQ(harness.layoutAt(kPortraitWidth, kPortraitHeight), kFiveLines);
}

/**
 * The remedy the upstream report measured as working on both the pinned Yoga and Yoga main: dirtying the
 * paragraph clears its `computedFlexBasis` through `Node::markDirtyAndPropagate` before the layout that would
 * have consumed it. It is asserted here because it is the shape any fix in our own code would have to take, and a
 * fix that stops working is worth knowing about before it is written.
 */
TEST(LayoutWidthRoundTripTest, DirtyingTheTextBeforeTheReturnLayoutRestoresThePortraitHeight) {
    const RotationHarness harness{2};

    EXPECT_FLOAT_EQ(harness.layoutAt(kPortraitWidth, kPortraitHeight), kFiveLines);
    EXPECT_FLOAT_EQ(harness.layoutAt(kLandscapeWidth, kLandscapeHeight), kThreeLines);

    harness.markTextDirty();

    EXPECT_FLOAT_EQ(harness.layoutAt(kPortraitWidth, kPortraitHeight), kFiveLines);
}

} // namespace
