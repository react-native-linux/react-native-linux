#include "ScrollIndicator.h"

#include "SceneTestSupport.h"

#include <gtest/gtest.h>
#include <optional>

#include <react/renderer/components/scrollview/ScrollViewProps.h>

namespace {

using react_native_linux::horizontalScrollIndicator;
using react_native_linux::ScrollIndicatorGeometry;
using react_native_linux::verticalScrollIndicator;

TEST(ScrollIndicatorTest, ContentThatFitsHasNoIndicator) {
    EXPECT_FALSE(verticalScrollIndicator(Size{.width = 200, .height = 100}, Size{.width = 200, .height = 100}, 0));
    EXPECT_FALSE(horizontalScrollIndicator(Size{.width = 200, .height = 100}, Size{.width = 150, .height = 900}, 0));
}

TEST(ScrollIndicatorTest, TheThumbIsTheViewportsShareOfTheContentAtTheOffsetsShareOfTheRange) {
    const Size viewport{.width = 200, .height = 100};
    const Size content{.width = 200, .height = 400};
    const ScrollIndicatorGeometry top = verticalScrollIndicator(viewport, content, 0).value();

    EXPECT_EQ(top.track, makeRect(192, 2, 6, 96));
    EXPECT_EQ(top.thumb, makeRect(192, 2, 6, 24));
    EXPECT_EQ(verticalScrollIndicator(viewport, content, 150).value().thumb, makeRect(192, 38, 6, 24));
    EXPECT_EQ(verticalScrollIndicator(viewport, content, 300).value().thumb, makeRect(192, 74, 6, 24));
}

TEST(ScrollIndicatorTest, AnOverscrollClampsTheThumbToTheTrackEnds) {
    const Size viewport{.width = 200, .height = 100};
    const Size content{.width = 200, .height = 400};

    EXPECT_EQ(verticalScrollIndicator(viewport, content, -40).value().thumb.origin.y, 2);
    EXPECT_EQ(verticalScrollIndicator(viewport, content, 500).value().thumb.origin.y, 74);
}

TEST(ScrollIndicatorTest, AVeryLongDocumentKeepsAThumbLongEnoughToSeeButNeverLongerThanTheTrack) {
    EXPECT_EQ(verticalScrollIndicator(Size{.width = 200, .height = 100}, Size{.width = 200, .height = 10000}, 0)
                  .value()
                  .thumb.size.height,
              24);
    EXPECT_EQ(verticalScrollIndicator(Size{.width = 200, .height = 20}, Size{.width = 200, .height = 40}, 0)
                  .value()
                  .thumb.size.height,
              16);
    EXPECT_FALSE(verticalScrollIndicator(Size{.width = 200, .height = 3}, Size{.width = 200, .height = 40}, 0));
}

TEST(ScrollIndicatorTest, TheHorizontalIndicatorRunsAlongTheBottomEdge) {
    const ScrollIndicatorGeometry geometry =
        horizontalScrollIndicator(Size{.width = 100, .height = 50}, Size{.width = 400, .height = 50}, 0).value();

    EXPECT_EQ(geometry.track, makeRect(2, 42, 96, 6));
    EXPECT_EQ(geometry.thumb, makeRect(2, 42, 24, 6));
}

TEST(ScrollIndicatorTest, TheTrackMustFitAcrossTheViewport) {
    EXPECT_FALSE(verticalScrollIndicator(Size{.width = 7, .height = 100}, Size{.width = 7, .height = 400}, 0));
    EXPECT_FALSE(horizontalScrollIndicator(Size{.width = 100, .height = 7}, Size{.width = 400, .height = 7}, 0));
    EXPECT_TRUE(verticalScrollIndicator(Size{.width = 8, .height = 100}, Size{.width = 8, .height = 400}, 0));
    EXPECT_TRUE(horizontalScrollIndicator(Size{.width = 100, .height = 8}, Size{.width = 400, .height = 8}, 0));
}

constexpr Tag kScrollTag = 2;
constexpr Tag kRowTag = 3;

/** A 200x100 scroll view over 400 points of content, with one full-width row under where its indicator sits. */
RetainedScene sceneWithScrollView(const std::shared_ptr<facebook::react::ScrollViewProps>& props) {
    RetainedScene scene;
    ShadowView scrollView = makeScrollView(kScrollTag, makeRect(0, 0, 200, 100), Point{}, makeRect(0, 0, 200, 400));

    if (props != nullptr) {
        scrollView.props = props;
    }

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, scrollView);
    addChild(scene, kScrollTag, makePaintedView(kRowTag, makeRect(0, 0, 200, 400), red()));

    return scene;
}

TEST(ScrollIndicatorSceneTest, TheIndicatorIsPaintedAboveTheContentAsARoundedBar) {
    const SceneSnapshot snapshot = sceneWithScrollView(nullptr).snapshot();

    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_EQ(snapshot[0].tag, kRowTag);
    EXPECT_EQ(snapshot[1].tag, kScrollTag);
    EXPECT_EQ(snapshot[1].frame, makeRect(192, 2, 6, 24));
    EXPECT_FLOAT_EQ(snapshot[1].borderRadii.topLeft.horizontal, 3);
}

TEST(ScrollIndicatorSceneTest, TheIndicatorClosesTheScrollViewsOpacityLayerAfterTheContent) {
    const auto props = std::make_shared<facebook::react::ScrollViewProps>();

    props->opacity = 0.5F;

    const SceneSnapshot snapshot = sceneWithScrollView(props).snapshot();

    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_EQ(snapshot[0].opensLayers, std::vector<float>{0.5F});
    EXPECT_EQ(snapshot[0].closesLayers, 0U);
    EXPECT_EQ(snapshot[1].backgroundColorArgb, 0x99A0A6B0U);
    EXPECT_EQ(snapshot[1].closesLayers, 1U);
}

TEST(ScrollIndicatorSceneTest, TheIndicatorInheritsTheScrollViewsRoundedClip) {
    const auto props = std::make_shared<facebook::react::ScrollViewProps>();

    props->borderRadii.all = facebook::react::ValueUnit{40.0F, facebook::react::UnitType::Point};

    const RetainedScene scene = sceneWithScrollView(props);
    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 2U);
    ASSERT_EQ(snapshot[1].clips.size(), 1U);
    ASSERT_EQ(snapshot[0].clips.size(), 1U);
    EXPECT_EQ(snapshot[1].clips[0].frame, snapshot[0].clips[0].frame);
    EXPECT_EQ(snapshot[1].clips[0].borderRadii, snapshot[0].clips[0].borderRadii);
    EXPECT_FLOAT_EQ(snapshot[1].clips[0].borderRadii.topRight.horizontal, 40);
    EXPECT_NE(scene.findNodeAtPoint(kSurfaceTag, Point{.x = 195, .y = 5}).tag, kScrollTag);
}

TEST(ScrollIndicatorSceneTest, AMovingOverlayDoesNotDisplaceContentOrStandInForItsOwner) {
    SceneSnapshot before = sceneWithScrollView(nullptr).snapshot();
    SceneSnapshot after = before;

    ASSERT_EQ(after.size(), 2U);
    EXPECT_TRUE(after[1].isScrollIndicator);
    after[1].frame.origin.y += 10;
    EXPECT_FALSE(findDisplacedPrimitive(before, after).has_value());

    after[0].frame.origin.y += 20;
    const std::optional<ScenePrimitiveDisplacement> displaced = findDisplacedPrimitive(before, after);

    ASSERT_TRUE(displaced.has_value());
    EXPECT_EQ(displaced->tag, kRowTag);

    before = {ScenePrimitive{.tag = kScrollTag, .frame = makeRect(0, 0, 200, 100)}};
    after = {after[1]};
    const std::optional<ScenePrimitiveDisplacement> missing = findDisplacedPrimitive(before, after);

    ASSERT_TRUE(missing.has_value());
    EXPECT_TRUE(missing->isMissing);
}

TEST(ScrollIndicatorSceneTest, ShowsVerticalScrollIndicatorFalseHidesIt) {
    const auto props = std::make_shared<facebook::react::ScrollViewProps>();

    props->showsVerticalScrollIndicator = false;
    props->showsHorizontalScrollIndicator = false;

    EXPECT_EQ(sceneWithScrollView(props).snapshot().size(), 1U);
}

TEST(ScrollIndicatorSceneTest, APressOnTheIndicatorTrackReachesTheScrollViewAndNeverTheRowBeneath) {
    const RetainedScene scene = sceneWithScrollView(nullptr);

    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, Point{.x = 195, .y = 90}).tag, kScrollTag);
    EXPECT_EQ(scene.findNodeAtPoint(kSurfaceTag, Point{.x = 100, .y = 50}).tag, kRowTag);
    EXPECT_NE(scene.findNodeAtPoint(kSurfaceTag, Point{.x = 195, .y = 105}).tag, kScrollTag);
}

TEST(ScrollIndicatorSceneTest, AScrollViewThatTakesNoPointerEventsLetsTheTrackPassThrough) {
    const auto props = std::make_shared<facebook::react::ScrollViewProps>();

    props->pointerEvents = facebook::react::PointerEventsMode::BoxNone;

    EXPECT_EQ(sceneWithScrollView(props).findNodeAtPoint(kSurfaceTag, Point{.x = 195, .y = 90}).tag, kRowTag);
}

} // namespace
