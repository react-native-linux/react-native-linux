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
}

TEST(ScrollIndicatorSceneTest, AScrollViewThatTakesNoPointerEventsLetsTheTrackPassThrough) {
    const auto props = std::make_shared<facebook::react::ScrollViewProps>();

    props->pointerEvents = facebook::react::PointerEventsMode::BoxNone;

    EXPECT_EQ(sceneWithScrollView(props).findNodeAtPoint(kSurfaceTag, Point{.x = 195, .y = 90}).tag, kRowTag);
}

} // namespace
