#include "RetainedScene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <gtest/gtest.h>
#include <initializer_list>
#include <memory>
#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/components/scrollview/ScrollViewShadowNode.h>
#include <react/renderer/components/scrollview/ScrollViewState.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/components/view/ViewShadowNode.h>
#include <react/renderer/core/ConcreteState.h>
#include <react/renderer/core/LayoutMetrics.h>
#include <react/renderer/core/LayoutableShadowNode.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/core/ShadowNode.h>
#include <react/renderer/core/ShadowNodeFamily.h>
#include <react/renderer/element/Element.h>
#include <react/renderer/element/testUtils.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/graphics/Transform.h>
#include <react/renderer/mounting/ShadowView.h>
#include <utility>
#include <vector>
#include <yoga/enums/PositionType.h>

// Issue #235. `RetainedScene::findNodeAtPoint` reads the painted scene and
// `LayoutableShadowNode::findNodeAtPoint` reads the committed shadow tree — #97 splits them on purpose so an
// animation frame can move the picture without a commit — and the two are required to agree whenever nothing is
// animating (*Hit-testing under animation* in docs/cpp-toolchain.md). This file is that proof: every tree upstream's
// own `FindNodeAtPointTest.cpp` uses, built with its own `Element`/`testUtils.h` helpers, is mounted into a
// `RetainedScene` tag for tag and frame for frame, and both hit-testers are asked the same points. `pointerEvents`
// and `transform` trees extend the oracle past what upstream's ten cases cover, because #64 and #103 are the
// other two things the two algorithms have to agree about.

namespace {

using facebook::react::ConcreteState;
using facebook::react::Element;
using facebook::react::EmptyLayoutMetrics;
using facebook::react::LayoutableShadowNode;
using facebook::react::Point;
using facebook::react::PointerEventsMode;
using facebook::react::Rect;
using facebook::react::ScrollViewProps;
using facebook::react::ScrollViewShadowNode;
using facebook::react::ScrollViewState;
using facebook::react::ShadowNode;
using facebook::react::ShadowNodeFamily;
using facebook::react::ShadowView;
using facebook::react::simpleComponentBuilder;
using facebook::react::Tag;
using facebook::react::Transform;
using facebook::react::ViewProps;
using facebook::react::ViewShadowNode;
using react_native_linux::RetainedScene;

Rect rect(float x, float y, float width, float height) {
    return Rect{.origin = {.x = x, .y = y}, .size = {.width = width, .height = height}};
}

/** One node at `frame`, built the way every upstream `FindNodeAtPointTest.cpp` case builds one: an `Element` whose
 * `finalize` writes `frame` straight into `LayoutMetrics`, bypassing Yoga the same way upstream's own fixture
 * does. Shared here because the same six lines once per node, ten-plus times over, is a jscpd clone at threshold
 * zero. */
template <typename ShadowNodeT> Element<ShadowNodeT> nodeAt(Tag tag, Rect frame) {
    return Element<ShadowNodeT>().tag(tag).finalize([frame](ShadowNodeT& shadowNode) {
        auto layoutMetrics = EmptyLayoutMetrics;
        layoutMetrics.frame = frame;
        shadowNode.setLayoutMetrics(layoutMetrics);
    });
}

template <typename PropsT> std::shared_ptr<PropsT> propsWithTransform(Transform transform) {
    const std::shared_ptr<PropsT> sharedProps = std::make_shared<PropsT>();

    sharedProps->transform = std::move(transform);

    return sharedProps;
}

std::shared_ptr<ViewProps> viewPropsWithPointerEvents(PointerEventsMode pointerEvents) {
    const std::shared_ptr<ViewProps> sharedProps = std::make_shared<ViewProps>();

    sharedProps->pointerEvents = pointerEvents;

    return sharedProps;
}

/** `zIndex` plus `position: absolute`, which is what upstream's `overlappingViewsWithZIndex` and the two
 * `pointerEvents` cases beside it give a sibling to paint on top of siblings declared after it. */
std::shared_ptr<ViewProps> viewPropsStackedAboveItsSiblings() {
    const std::shared_ptr<ViewProps> sharedProps = std::make_shared<ViewProps>();

    sharedProps->zIndex = 1;
    sharedProps->yogaStyle.setPositionType(facebook::yoga::PositionType::Absolute);

    return sharedProps;
}

/**
 * The `ShadowView` `RetainedScene::createNode`/`insertChild` want, read straight off the committed `ShadowNode`:
 * the same frame `LayoutableShadowNode::findNodeAtPoint` reads, and the same `Props` object, so `pointerEvents` and
 * `transform` cannot drift between the two trees this file compares. A `ScrollView`'s `contentOffset` is copied out
 * of its state the same way `readScrollContent` reads it back.
 */
ShadowView shadowViewFor(const std::shared_ptr<const ShadowNode>& node) {
    ShadowView shadowView;
    const auto* layoutable = dynamic_cast<const LayoutableShadowNode*>(node.get());

    shadowView.tag = node->getTag();
    shadowView.componentName = "View";
    shadowView.layoutMetrics = layoutable->getLayoutMetrics();
    shadowView.props = node->getProps();

    if (const auto* scrollViewNode = dynamic_cast<const ScrollViewShadowNode*>(node.get())) {
        shadowView.state = std::make_shared<const ConcreteState<ScrollViewState>>(
            std::make_shared<const ScrollViewState>(scrollViewNode->getStateData()), ShadowNodeFamily::Weak{});
    }

    return shadowView;
}

/**
 * Mounts an upstream-built `ShadowNode` tree into `scene`, tag for tag, frame for frame. Siblings are
 * stable-sorted by `getOrderIndex()` first — the order `ConcreteViewShadowNode`'s constructor already derived from
 * `zIndex` — because that is the order Fabric's differ would have handed `RetainedScene` as mount/paint order, and
 * this tree never goes through a real diff to get one.
 */
void mountIntoScene(RetainedScene& scene, const std::shared_ptr<const ShadowNode>& node, Tag parentTag, int index) {
    const ShadowView shadowView = shadowViewFor(node);

    scene.createNode(shadowView);

    if (parentTag != 0) {
        scene.insertChild(parentTag, shadowView, index);
    }

    auto children = node->getChildren();

    std::ranges::stable_sort(
        children, [](const auto& lhs, const auto& rhs) { return lhs->getOrderIndex() < rhs->getOrderIndex(); });

    int childIndex = 0;

    for (const auto& child : children) {
        mountIntoScene(scene, child, node->getTag(), childIndex++);
    }
}

Tag upstreamHitTag(const std::shared_ptr<const ShadowNode>& root, Point point) {
    const std::shared_ptr<const ShadowNode> hit = LayoutableShadowNode::findNodeAtPoint(root, point);

    return hit != nullptr ? hit->getTag() : Tag{0};
}

Tag sceneHitTag(RetainedScene& scene, Tag rootTag, Point point) { return scene.findNodeAtPoint(rootTag, point).tag; }

/**
 * Mounts `root` once and asserts the two hit-testers name the same tag at every point in `points`. `root` itself
 * is the query root on both sides — `RetainedScene::findNodeAtPoint` takes any tag as its root and seeds a fresh
 * `ScenePaintState` there, so calling it with `root`'s own tag is exactly the query
 * `LayoutableShadowNode::findNodeAtPoint(root, point)` asks, with no synthetic surface wrapping either side.
 */
void expectHitTestersAgree(const std::shared_ptr<const ShadowNode>& root, std::initializer_list<Point> points) {
    RetainedScene scene;

    mountIntoScene(scene, root, 0, 0);

    for (const Point point : points) {
        EXPECT_EQ(sceneHitTag(scene, root->getTag(), point), upstreamHitTag(root, point))
            << "at (" << point.x << ", " << point.y << ")";
    }
}

/**
 * The grid-of-points form of `expectHitTestersAgree`, for the trees where a handful of literal points is not
 * enough to trust the whole surface agrees.
 *
 * Samples are taken at pixel centres — `x + 0.5`, `y + 0.5` — the same convention `--hit-paint-golden` uses (*The
 * hit-versus-paint agreement proof* in docs/cpp-toolchain.md), and for the same reason here: `roundedBoxContainsPoint`
 * is deliberately half-open on the right and the bottom edge of a box (see its own comment in RetainedScene.cpp,
 * issue #35), while upstream's `Rect::containsPoint` closes both. A grid that lands exactly on an integer box edge
 * would fail on that documented, intentional difference rather than on anything this file is trying to prove.
 */
void expectHitTestersAgreeOverGrid(const std::shared_ptr<const ShadowNode>& root, Rect bounds, float step) {
    RetainedScene scene;

    mountIntoScene(scene, root, 0, 0);

    const int rowCount = static_cast<int>(bounds.size.height / step) + 1;
    const int columnCount = static_cast<int>(bounds.size.width / step) + 1;

    for (int row = 0; row <= rowCount; ++row) {
        const float y = bounds.origin.y + 0.5F + (static_cast<float>(row) * step);

        for (int column = 0; column <= columnCount; ++column) {
            const float x = bounds.origin.x + 0.5F + (static_cast<float>(column) * step);
            const Point point{.x = x, .y = y};

            EXPECT_EQ(sceneHitTag(scene, root->getTag(), point), upstreamHitTag(root, point))
                << "at (" << x << ", " << y << ")";
        }
    }
}

// Upstream's own FindNodeAtPointTest.cpp, ported verbatim

// The nested pair `WithoutTransform` and `ViewIsTranslated` both hang under their (differently transformed) root:
// a 100x100 child at (100, 100) holding a 10x10 grandchild at (10, 10) within it.
Element<ViewShadowNode> childWithNestedGrandchild() {
    return nodeAt<ViewShadowNode>(2, rect(100, 100, 100, 100))
        .children({nodeAt<ViewShadowNode>(3, rect(10, 10, 10, 10))});
}

TEST(HitTestDifferentialTest, WithoutTransform) {
    auto builder = simpleComponentBuilder();
    auto element = nodeAt<ViewShadowNode>(1, rect(0, 0, 1000, 1000)).children({childWithNestedGrandchild()});

    const std::shared_ptr<const ShadowNode> root = builder.build(element);

    expectHitTestersAgree(root, {Point{.x = 115, .y = 115}, Point{.x = 105, .y = 105}, Point{.x = 900, .y = 900},
                                 Point{.x = 1001, .y = 1001}});
    expectHitTestersAgreeOverGrid(root, rect(0, 0, 1000, 1000), 47);
}

TEST(HitTestDifferentialTest, ViewIsTranslated) {
    auto builder = simpleComponentBuilder();
    auto element = nodeAt<ScrollViewShadowNode>(1, rect(0, 0, 1000, 1000))
                       .stateData([](ScrollViewState& data) { data.contentOffset = {.x = 100, .y = 100}; })
                       .children({childWithNestedGrandchild()});

    const std::shared_ptr<const ShadowNode> root = builder.build(element);

    expectHitTestersAgree(root, {Point{.x = 15, .y = 15}, Point{.x = 5, .y = 5}});
}

TEST(HitTestDifferentialTest, ViewIsScaled) {
    auto builder = simpleComponentBuilder();
    auto element = nodeAt<ViewShadowNode>(1, rect(0, 0, 1000, 1000))
                       .children({nodeAt<ViewShadowNode>(2, rect(100, 100, 100, 100))
                                      .children({nodeAt<ViewShadowNode>(3, rect(10, 10, 10, 10)).props([] {
                                          return propsWithTransform<ViewProps>(Transform::Scale(0.5, 0.5, 0));
                                      })})});

    const std::shared_ptr<const ShadowNode> root = builder.build(element);

    expectHitTestersAgree(root, {Point{.x = 119, .y = 119}});
}

TEST(HitTestDifferentialTest, OverlappingViews) {
    auto builder = simpleComponentBuilder();
    auto element = nodeAt<ViewShadowNode>(1, rect(0, 0, 100, 100))
                       .children({nodeAt<ViewShadowNode>(2, rect(25, 25, 50, 50)),
                                  nodeAt<ViewShadowNode>(3, rect(50, 50, 50, 50))});

    const std::shared_ptr<const ShadowNode> root = builder.build(element);

    expectHitTestersAgreeOverGrid(root, rect(0, 0, 100, 100), 5);
}

// `OverlappingViewsWithZIndex` and the two `BoxNone`/`None` pointer-events cases below it all hang the same
// stacked pair off their (differently pointer-gated) root: a 50x50 child at (25, 25) stacked above its sibling by
// `zIndex`, and a plain 50x50 sibling at (50, 50).
Element<ViewShadowNode> rootWithStackedChildAndSibling() {
    return nodeAt<ViewShadowNode>(1, rect(0, 0, 100, 100))
        .children(
            {nodeAt<ViewShadowNode>(2, rect(25, 25, 50, 50)).props([] { return viewPropsStackedAboveItsSiblings(); }),
             nodeAt<ViewShadowNode>(3, rect(50, 50, 50, 50))});
}

TEST(HitTestDifferentialTest, OverlappingViewsWithZIndex) {
    auto builder = simpleComponentBuilder();
    auto element = rootWithStackedChildAndSibling();

    const std::shared_ptr<const ShadowNode> root = builder.build(element);

    expectHitTestersAgreeOverGrid(root, rect(0, 0, 100, 100), 5);
}

TEST(HitTestDifferentialTest, OverlappingViewsWithParentPointerEventsBoxOnly) {
    auto builder = simpleComponentBuilder();
    auto element = nodeAt<ViewShadowNode>(1, rect(0, 0, 100, 100))
                       .props([] { return viewPropsWithPointerEvents(PointerEventsMode::BoxOnly); })
                       .children({nodeAt<ViewShadowNode>(2, rect(50, 50, 50, 50)),
                                  nodeAt<ViewShadowNode>(3, rect(50, 50, 50, 50))});

    const std::shared_ptr<const ShadowNode> root = builder.build(element);

    expectHitTestersAgree(root, {Point{.x = 60, .y = 60}});
}

TEST(HitTestDifferentialTest, OverlappingViewsWithParentPointerEventsBoxNone) {
    auto builder = simpleComponentBuilder();
    auto element =
        rootWithStackedChildAndSibling().props([] { return viewPropsWithPointerEvents(PointerEventsMode::BoxNone); });

    const std::shared_ptr<const ShadowNode> root = builder.build(element);

    expectHitTestersAgree(root, {Point{.x = 50, .y = 50}});
}

TEST(HitTestDifferentialTest, OverlappingViewsWithParentPointerEventsNone) {
    auto builder = simpleComponentBuilder();
    auto element =
        rootWithStackedChildAndSibling().props([] { return viewPropsWithPointerEvents(PointerEventsMode::None); });

    const std::shared_ptr<const ShadowNode> root = builder.build(element);

    expectHitTestersAgree(root, {Point{.x = 50, .y = 50}});
}

TEST(HitTestDifferentialTest, InvertedList) {
    auto builder = simpleComponentBuilder();
    auto element = nodeAt<ScrollViewShadowNode>(1, rect(0, 0, 100, 200))
                       .props([] { return propsWithTransform<ScrollViewProps>(Transform::VerticalInversion()); })
                       .children({nodeAt<ViewShadowNode>(2, rect(0, 0, 100, 100)),
                                  nodeAt<ViewShadowNode>(3, rect(0, 100, 100, 100))});

    const std::shared_ptr<const ShadowNode> root = builder.build(element);

    expectHitTestersAgree(root, {Point{.x = 10, .y = 10}, Point{.x = 10, .y = 105}});
}

// Upstream's `considersOverflowAreaOfTheParent`, ported unmodified. Upstream needs `layoutMetrics.overflowInset`
// as an escape hatch because its recursion is gated on the point landing inside the *current* node's own frame —
// without it, a zero-height parent would hide every child laid out past its own bottom edge. `SceneNode` has no
// `overflowInset` field and needs none: `RetainedScene::hitTestNode` recurses into every child unconditionally and
// lets each descendant's own `coversPrimitive` decide, so a parent with no area of its own never gates anything.
// Both algorithms reach the grandchild here; they just get there by different rules, which is exactly the case
// this file exists to catch if it ever stopped being true.
TEST(HitTestDifferentialTest, ConsidersOverflowAreaOfTheParent) {
    auto builder = simpleComponentBuilder();
    auto element =
        nodeAt<ViewShadowNode>(1, rect(0, 0, 100, 100))
            .children({Element<ViewShadowNode>()
                           .tag(2)
                           .finalize([](ViewShadowNode& shadowNode) {
                               auto layoutMetrics = EmptyLayoutMetrics;
                               layoutMetrics.frame.size = {.width = 100, .height = 0};
                               layoutMetrics.overflowInset = {.left = 0, .top = 0, .right = 0, .bottom = -100};
                               shadowNode.setLayoutMetrics(layoutMetrics);
                           })
                           .children({nodeAt<ViewShadowNode>(3, rect(0, 0, 100, 100))})});

    const std::shared_ptr<const ShadowNode> root = builder.build(element);

    expectHitTestersAgree(root, {Point{.x = 1, .y = 99}});
}

// pointerEvents: all four values, crossed two levels deep (#64)

// The exact table `AnimatedHitTestTest.cpp`'s `PointerEventsComposeAcrossTwoLevelsAsTheTableSays` pins for
// `RetainedScene` alone, reused here as a differential: `kNoHit` there is `kSurfaceTag`, the wrapping surface root
// that test's fixture mounts under the box. This file mounts the box itself as the query root on both sides, so a
// point neither level accepts is a miss on both sides — tag zero — rather than a fallback to a synthetic ancestor
// neither `LayoutableShadowNode::findNodeAtPoint` nor this file's tree has.
struct PointerEventsCase {
    PointerEventsMode parent;
    PointerEventsMode child;
    Tag expectedInsideChild;
    Tag expectedInsideParentOnly;
};

/**
 * `ComponentBuilder` owns the `ComponentDescriptorRegistry` the returned tree's `ShadowNodeFamily`s hold a
 * reference into, so a helper that builds a tree and hands back only the `ShadowNode` leaves that reference
 * dangling the moment the helper returns — the builder has to outlive every use of the tree, exactly like the
 * fixture-level `builder` each `TEST` body above keeps in scope. This pairs the two so a caller cannot have one
 * without the other.
 */
struct PointerEventsTree {
    facebook::react::ComponentBuilder builder;
    std::shared_ptr<const ShadowNode> root;
};

PointerEventsTree nestedPointerEventsTree(PointerEventsMode parent, PointerEventsMode child) {
    facebook::react::ComponentBuilder builder = simpleComponentBuilder();
    auto element = nodeAt<ViewShadowNode>(2, rect(100, 80, 200, 120))
                       .props([parent] { return viewPropsWithPointerEvents(parent); })
                       .children({nodeAt<ViewShadowNode>(3, rect(150, 20, 200, 80)).props([child] {
                           return viewPropsWithPointerEvents(child);
                       })});
    std::shared_ptr<const ShadowNode> root = builder.build(element);

    return PointerEventsTree{.builder = std::move(builder), .root = std::move(root)};
}

TEST(HitTestDifferentialTest, PointerEventsComposeAcrossTwoLevelsAsTheTableSays) {
    constexpr PointerEventsMode kAuto = PointerEventsMode::Auto;
    constexpr PointerEventsMode kNone = PointerEventsMode::None;
    constexpr PointerEventsMode kBoxNone = PointerEventsMode::BoxNone;
    constexpr PointerEventsMode kBoxOnly = PointerEventsMode::BoxOnly;
    constexpr Tag kNoHit = 0;
    constexpr Tag kBoxTag = 2;
    constexpr Tag kInnerTag = 3;
    const std::array<PointerEventsCase, 16> table{{
        {.parent = kAuto, .child = kAuto, .expectedInsideChild = kInnerTag, .expectedInsideParentOnly = kBoxTag},
        {.parent = kAuto, .child = kNone, .expectedInsideChild = kBoxTag, .expectedInsideParentOnly = kBoxTag},
        {.parent = kAuto, .child = kBoxNone, .expectedInsideChild = kBoxTag, .expectedInsideParentOnly = kBoxTag},
        {.parent = kAuto, .child = kBoxOnly, .expectedInsideChild = kInnerTag, .expectedInsideParentOnly = kBoxTag},
        {.parent = kNone, .child = kAuto, .expectedInsideChild = kNoHit, .expectedInsideParentOnly = kNoHit},
        {.parent = kNone, .child = kNone, .expectedInsideChild = kNoHit, .expectedInsideParentOnly = kNoHit},
        {.parent = kNone, .child = kBoxNone, .expectedInsideChild = kNoHit, .expectedInsideParentOnly = kNoHit},
        {.parent = kNone, .child = kBoxOnly, .expectedInsideChild = kNoHit, .expectedInsideParentOnly = kNoHit},
        {.parent = kBoxNone, .child = kAuto, .expectedInsideChild = kInnerTag, .expectedInsideParentOnly = kNoHit},
        {.parent = kBoxNone, .child = kNone, .expectedInsideChild = kNoHit, .expectedInsideParentOnly = kNoHit},
        {.parent = kBoxNone, .child = kBoxNone, .expectedInsideChild = kNoHit, .expectedInsideParentOnly = kNoHit},
        {.parent = kBoxNone, .child = kBoxOnly, .expectedInsideChild = kInnerTag, .expectedInsideParentOnly = kNoHit},
        {.parent = kBoxOnly, .child = kAuto, .expectedInsideChild = kBoxTag, .expectedInsideParentOnly = kBoxTag},
        {.parent = kBoxOnly, .child = kNone, .expectedInsideChild = kBoxTag, .expectedInsideParentOnly = kBoxTag},
        {.parent = kBoxOnly, .child = kBoxNone, .expectedInsideChild = kBoxTag, .expectedInsideParentOnly = kBoxTag},
        {.parent = kBoxOnly, .child = kBoxOnly, .expectedInsideChild = kBoxTag, .expectedInsideParentOnly = kBoxTag},
    }};

    // Inside the child's own absolute frame, (250, 100)-(450, 180), and inside the parent's absolute frame,
    // (100, 80)-(300, 200), but outside the child's.
    constexpr Point kInsideChild{.x = 260, .y = 140};
    constexpr Point kInsideParentOnly{.x = 120, .y = 190};

    for (const PointerEventsCase& entry : table) {
        const PointerEventsTree tree = nestedPointerEventsTree(entry.parent, entry.child);
        const std::shared_ptr<const ShadowNode>& root = tree.root;
        RetainedScene scene;

        mountIntoScene(scene, root, 0, 0);

        const int parentValue = static_cast<int>(entry.parent);
        const int childValue = static_cast<int>(entry.child);

        EXPECT_EQ(upstreamHitTag(root, kInsideChild), entry.expectedInsideChild)
            << "upstream, inside the child, parent " << parentValue << " child " << childValue;
        EXPECT_EQ(sceneHitTag(scene, root->getTag(), kInsideChild), entry.expectedInsideChild)
            << "scene, inside the child, parent " << parentValue << " child " << childValue;
        EXPECT_EQ(upstreamHitTag(root, kInsideParentOnly), entry.expectedInsideParentOnly)
            << "upstream, inside the parent only, parent " << parentValue << " child " << childValue;
        EXPECT_EQ(sceneHitTag(scene, root->getTag(), kInsideParentOnly), entry.expectedInsideParentOnly)
            << "scene, inside the parent only, parent " << parentValue << " child " << childValue;
    }
}

// transform: translate, scale, rotate (#103)

TEST(HitTestDifferentialTest, ATranslatedChildIsFoundWhereItIsPaintedAndNotWhereItWasLaidOut) {
    auto builder = simpleComponentBuilder();
    auto element = nodeAt<ViewShadowNode>(1, rect(0, 0, 300, 300))
                       .children({nodeAt<ViewShadowNode>(2, rect(50, 50, 60, 60)).props([] {
                           return propsWithTransform<ViewProps>(Transform::Translate(100, 0, 0));
                       })});

    const std::shared_ptr<const ShadowNode> root = builder.build(element);

    // Laid out at (50, 50)-(110, 110) and painted at (150, 50)-(210, 110): the laid-out position finds only the
    // root, and the painted one finds the child, on both hit-testers.
    expectHitTestersAgreeOverGrid(root, rect(0, 0, 300, 300), 20);
}

TEST(HitTestDifferentialTest, AScaledUpChildIsFoundPastItsOwnUnscaledFrame) {
    auto builder = simpleComponentBuilder();
    auto element = nodeAt<ViewShadowNode>(1, rect(0, 0, 300, 300))
                       .children({nodeAt<ViewShadowNode>(2, rect(120, 120, 60, 60)).props([] {
                           return propsWithTransform<ViewProps>(Transform::Scale(2, 2, 1));
                       })});

    const std::shared_ptr<const ShadowNode> root = builder.build(element);

    // Scale keeps the shape axis-aligned, so upstream's forward-mapped bounding box and the scene's inverse-mapped
    // frame describe the same rectangle: (90, 90)-(210, 210). A point in that grown area but outside the original
    // (120, 120)-(180, 180) frame is the case worth pinning — both sides have to have grown the hit region, not
    // just the picture.
    expectHitTestersAgreeOverGrid(root, rect(0, 0, 300, 300), 20);
}

// #99's inverse-mapped hit test and upstream's forward-mapped bounding box agree on every axis-aligned transform —
// translate and scale both keep a rectangle a rectangle — and *The algorithm* in docs/cpp-toolchain.md names the
// one case they cannot: a rotation's bounding box is bigger than the shape it bounds, so a point inside the box
// but outside the rotated rectangle is a hit upstream and a miss here, on purpose, because it is a miss in the
// picture the scene painted.
TEST(HitTestDifferentialTest, ARotatedChildAgreesInsideItsOwnShapeAndDisagreesInTheCornersOfItsBoundingBoxByDesign) {
    auto builder = simpleComponentBuilder();
    auto element = nodeAt<ViewShadowNode>(1, rect(0, 0, 300, 300))
                       .children({nodeAt<ViewShadowNode>(2, rect(120, 120, 60, 60)).props([] {
                           return propsWithTransform<ViewProps>(Transform::RotateZ(static_cast<float>(M_PI / 4.0)));
                       })});

    const std::shared_ptr<const ShadowNode> root = builder.build(element);

    // The rotated child is centred on (150, 150). A 60x60 square rotated 45 degrees is a diamond of "radius"
    // 30 * sqrt(2) ~= 42.43 along the axes from that centre, inscribed in an axis-aligned bounding box of the
    // same half-width. (150, 150) and (160, 160) are inside the diamond; (250, 250) is outside the bounding box
    // entirely. All three agree on both hit-testers.
    expectHitTestersAgree(root, {Point{.x = 150, .y = 150}, Point{.x = 160, .y = 160}, Point{.x = 250, .y = 250}});

    // (190, 190) is 40 points from the centre on both axes: inside the 42.43-radius bounding box upstream's
    // `transformedFrame` tests against, but outside the 42.43-radius diamond the picture actually painted
    // (|40| + |40| = 80 > 42.43). This is the documented divergence, not a bug: upstream finds the rotated child
    // and the scene finds the surface it is rotated over, and both answers are the ones each side promises.
    constexpr Point kBoundingBoxCornerOutsideTheRotatedShape{.x = 190, .y = 190};
    RetainedScene scene;

    mountIntoScene(scene, root, 0, 0);

    EXPECT_EQ(upstreamHitTag(root, kBoundingBoxCornerOutsideTheRotatedShape), Tag{2});
    EXPECT_EQ(sceneHitTag(scene, root->getTag(), kBoundingBoxCornerOutsideTheRotatedShape), Tag{1});
}

} // namespace
