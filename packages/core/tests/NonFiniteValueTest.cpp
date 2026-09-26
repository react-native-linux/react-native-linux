#include "LinuxMountingManager.h"
#include "RetainedScene.h"
#include "SceneTestSupport.h"

#include <array>
#include <cmath>
#include <folly/dynamic.h>
#include <gtest/gtest.h>
#include <limits>
#include <string>
#include <utility>
#include <vector>

// Issue #73: a `NaN` or an infinity that reaches the scene is refused at the boundary it arrives on, rather than
// serialized onward. The failure this prevents is not a crash, which is why it survived so long: a non-finite
// number fails every `<` and `<=` comparison downstream, so `hasArea` decides the node damages nothing, the
// painter still emits it as an unspecified `SkRect`, and `roundedBoxContainsPoint` answers *true* for every point
// on the surface. The picture loses the node and the node eats every press —
// [core#57780](https://github.com/facebook/react-native/issues/57780) and
// [rnw#8318](https://github.com/microsoft/react-native-windows/issues/8318).
//
// The three boundaries are here together because they are one rule: the mounting transaction (`writeNode`), the
// synchronous animated-prop path (`applyAnimatedProps`) and the `dispatchCommand` payload (`scrollTo`).

namespace yoga = facebook::yoga;

namespace {

using react_native_linux::MountDiagnostics;
using react_native_linux::RejectedNonFiniteProp;
using react_native_linux::SceneHit;

constexpr Tag kNodeTag = 2;
constexpr Tag kChildTag = 3;
constexpr float kNotANumber = std::numeric_limits<float>::quiet_NaN();
constexpr float kInfinity = std::numeric_limits<float>::infinity();

// The three shapes the acceptance criteria name, in one list so each case below runs against all of them rather
// than picking the one that happens to work.
constexpr std::array<float, 3> kNonFiniteValues{kNotANumber, kInfinity, -kInfinity};

SceneSnapshot snapshotOfChild(ShadowView child) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, std::move(child));

    return scene.snapshot();
}

std::shared_ptr<ViewProps> paintedProps() { return propsWithBackground(blue()); }

} // namespace

// A frame Yoga laid out as non-finite keeps nothing of itself: it is the value the whole issue is named for, and
// the empty frame it becomes is what makes "paints nothing" true rather than "paints something unspecified".
TEST(NonFiniteMountBoundary, ANonFiniteFrameBecomesTheEmptyFrame) {
    for (const float value : kNonFiniteValues) {
        const SceneSnapshot snapshot =
            snapshotOfChild(makePaintedView(kNodeTag, makeRect(10, value, 200, 100), blue()));

        ASSERT_EQ(snapshot.size(), 1U);
        EXPECT_EQ(snapshot[0].frame, makeRect(0, 0, 0, 0));
    }
}

TEST(NonFiniteMountBoundary, ANonFiniteFrameSizeBecomesTheEmptyFrame) {
    for (const float value : kNonFiniteValues) {
        const SceneSnapshot snapshot = snapshotOfChild(makePaintedView(kNodeTag, makeRect(10, 20, value, 100), blue()));

        ASSERT_EQ(snapshot.size(), 1U);
        EXPECT_EQ(snapshot[0].frame, makeRect(0, 0, 0, 0));
    }
}

// The half of the defect the picture cannot show. Before the boundary rule, this press — at the far corner of the
// surface, nowhere near a node whose frame is 200 wide — answered with the node, because every comparison in
// `roundedBoxContainsPoint` is false under `NaN` and the containment test reads "not outside".
TEST(NonFiniteMountBoundary, ANonFiniteFrameDoesNotSwallowEveryPress) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makePaintedView(kNodeTag, makeRect(10, 20, kNotANumber, 100), blue()));

    const SceneHit hit = scene.findNodeAtPoint(kSurfaceTag, Point{.x = 700, .y = 500});

    EXPECT_EQ(hit.tag, kSurfaceTag);
}

// A non-finite origin composes into every descendant, so the child of a poisoned parent is the second victim and
// `inf + -inf` makes it a `NaN` even where the child's own numbers were finite.
TEST(NonFiniteMountBoundary, ANonFiniteFrameDoesNotPoisonDescendants) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeView(kNodeTag, makeRect(kInfinity, 0, 100, 100)));
    addChild(scene, kNodeTag, makePaintedView(kChildTag, makeRect(5, 7, 50, 50), red()));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot[0].frame, makeRect(5, 7, 50, 50));
}

TEST(NonFiniteMountBoundary, ANonFiniteTransformBecomesTheIdentity) {
    for (const float value : kNonFiniteValues) {
        const std::shared_ptr<ViewProps> viewProps = paintedProps();

        viewProps->transform = Transform::Scale(value, 1, 1);

        const SceneSnapshot snapshot = snapshotOfChild(makeStyledView(kNodeTag, makeRect(0, 0, 40, 20), viewProps));

        ASSERT_EQ(snapshot.size(), 1U);
        EXPECT_FLOAT_EQ(snapshot[0].matrix.scaleX, 1.0F);
        EXPECT_FLOAT_EQ(snapshot[0].matrix.scaleY, 1.0F);
    }
}

// Opacity is the field where only `NaN` is a defect: `std::clamp` orders an infinity correctly, so `Infinity`
// already became 1 and `-Infinity` already became 0, and both are values the painter can draw. `NaN` is the one
// the clamp lets through, because both of its comparisons are false.
//
// The colour is asserted rather than the opacity because the alpha is where the poison landed: it reached
// `std::lround`, and the node vanished from the picture with a fully transparent background it was never given.
TEST(NonFiniteMountBoundary, ANotANumberOpacityBecomesFullyOpaque) {
    const std::shared_ptr<ViewProps> viewProps = paintedProps();

    viewProps->opacity = kNotANumber;

    const SceneSnapshot snapshot = snapshotOfChild(makeStyledView(kNodeTag, makeRect(0, 0, 40, 20), viewProps));

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot[0].backgroundColorArgb, kBlueArgb);
}

TEST(NonFiniteMountBoundary, TheClampAlreadyOrdersAnInfiniteOpacity) {
    const std::shared_ptr<ViewProps> opaqueProps = paintedProps();
    const std::shared_ptr<ViewProps> transparentProps = paintedProps();

    opaqueProps->opacity = kInfinity;
    transparentProps->opacity = -kInfinity;

    const SceneSnapshot opaque = snapshotOfChild(makeStyledView(kNodeTag, makeRect(0, 0, 40, 20), opaqueProps));

    ASSERT_EQ(opaque.size(), 1U);
    EXPECT_EQ(opaque[0].backgroundColorArgb, kBlueArgb);
    EXPECT_TRUE(snapshotOfChild(makeStyledView(kNodeTag, makeRect(0, 0, 40, 20), transparentProps)).empty());
}

TEST(NonFiniteMountBoundary, ANonFiniteBorderRadiusBecomesSquare) {
    for (const float value : kNonFiniteValues) {
        const std::shared_ptr<ViewProps> viewProps = paintedProps();

        viewProps->borderRadii.all = ValueUnit{value, UnitType::Point};

        const SceneSnapshot snapshot = snapshotOfChild(makeStyledView(kNodeTag, makeRect(0, 0, 40, 20), viewProps));

        ASSERT_EQ(snapshot.size(), 1U);
        EXPECT_FLOAT_EQ(snapshot[0].borderRadii.topLeft.horizontal, 0.0F);
        EXPECT_FLOAT_EQ(snapshot[0].borderRadii.bottomRight.vertical, 0.0F);
    }
}

// A ScrollView's offset is subtracted from every child's origin, so a non-finite one is the frame case again with
// the whole content view as its blast radius.
TEST(NonFiniteMountBoundary, ANonFiniteContentOffsetBecomesTheOrigin) {
    for (const float value : kNonFiniteValues) {
        RetainedScene scene;

        scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
        addChild(
            scene, kSurfaceTag,
            makeScrollView(kNodeTag, makeRect(0, 0, 100, 100), Point{.x = 0, .y = value}, makeRect(0, 0, 100, 400)));
        addChild(scene, kNodeTag, makePaintedView(kChildTag, makeRect(0, 30, 50, 50), red()));

        const SceneSnapshot snapshot = scene.snapshot();

        ASSERT_EQ(snapshot.size(), 1U);
        EXPECT_EQ(snapshot[0].frame, makeRect(0, 30, 50, 50));
    }
}

// Border widths are the counter-case, and the reason there is no branch for them: they reach the scene only
// through `yogaStyle`, whose `StyleLength::points` folds a non-finite length into undefined. Upstream already
// rejects this one, and this pins that it keeps doing so — if Yoga ever stops, the guard above needs a sixth
// field and this test is what says so.
TEST(NonFiniteMountBoundary, YogaAlreadyRejectsANonFiniteBorderWidth) {
    const std::shared_ptr<ViewProps> viewProps = paintedProps();

    viewProps->yogaStyle.setBorder(yoga::Edge::All, yoga::StyleLength::points(kNotANumber));
    viewProps->borderColors.all = red();

    const SceneSnapshot snapshot = snapshotOfChild(makeStyledView(kNodeTag, makeRect(0, 0, 40, 20), viewProps));

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_FLOAT_EQ(snapshot[0].borderWidths.left, 0.0F);
}

// A well-formed node reports nothing, which is the other half of "reported once": a counter that also counts the
// finite case is a counter nobody reads.
TEST(NonFiniteMountBoundary, AFiniteNodeIsNotReported) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makePaintedView(kNodeTag, makeRect(10, 20, 200, 100), blue()));

    EXPECT_TRUE(scene.takeRejectedNonFiniteProps().empty());
}

TEST(NonFiniteMountBoundary, EveryPoisonedFieldOfOneNodeIsNamed) {
    const std::shared_ptr<ViewProps> viewProps = paintedProps();

    viewProps->opacity = kNotANumber;
    viewProps->borderRadii.all = ValueUnit{kNotANumber, UnitType::Point};
    viewProps->transform = Transform::Scale(kNotANumber, 1, 1);

    RetainedScene scene;

    // `createNode` rather than `addChild`: an insert writes the node a second time, and this case is about which
    // fields one write names, not how many writes a mount performs.
    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    scene.createNode(makeStyledView(kNodeTag, makeRect(0, kNotANumber, 40, 20), viewProps));

    const std::vector<RejectedNonFiniteProp> rejected = scene.takeRejectedNonFiniteProps();
    std::vector<std::string> names;

    for (const RejectedNonFiniteProp& rejectedProp : rejected) {
        EXPECT_EQ(rejectedProp.tag, kNodeTag);
        names.emplace_back(rejectedProp.propName);
    }

    EXPECT_EQ(names, (std::vector<std::string>{"frame", "transform", "opacity", "borderRadius"}));
    EXPECT_TRUE(scene.takeRejectedNonFiniteProps().empty());
}

// The mounting manager is what turns the drained list into something an application author can act on, and it
// logs the first one only — a `NaN` in an animation writes the same prop on every frame, and a log line per frame
// is how rnw#8318 became unreadable.
TEST(NonFiniteMountBoundary, TheMountingManagerNamesTheFirstOneAndCountsTheRest) {
    const std::shared_ptr<ViewProps> viewProps = paintedProps();

    viewProps->opacity = kNotANumber;

    LinuxMountingManager mountingManager;
    const ShadowView child = makeStyledView(kNodeTag, makeRect(0, kNotANumber, 40, 20), viewProps);

    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});
    mountingManager.executeMount(kSurfaceTag,
                                 transactionOf({ShadowViewMutation::CreateMutation(child),
                                                ShadowViewMutation::InsertMutation(kSurfaceTag, child, 0)}));

    const MountDiagnostics diagnostics = mountingManager.mountDiagnostics();

    EXPECT_EQ(diagnostics.rejectedNonFiniteProps, 4U);
    EXPECT_EQ(diagnostics.firstRejectedNonFiniteProp, "frame");
    EXPECT_EQ(diagnostics.firstRejectedNonFiniteTag, kNodeTag);
}

TEST(NonFiniteMountBoundary, TheMessageNamesTheNodeAndTheProp) {
    const std::string message = react_native_linux::rejectedNonFinitePropMessage(kNodeTag, "opacity");

    EXPECT_NE(message.find("node 2"), std::string::npos);
    EXPECT_NE(message.find("opacity"), std::string::npos);
}

// The animated fast path already refused a scalar `NaN`; a transform arrives as an array of operations, so the
// payload it checked was never a double and every number inside it went through unread.
// A scene holding one painted node, animated once with `transform: [{scale}]`.
std::vector<react_native_linux::RejectedAnimatedProp> animateScale(RetainedScene& scene, double scale) {
    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makePaintedView(kNodeTag, makeRect(0, 0, 40, 20), blue()));

    const folly::dynamic operation = folly::dynamic::object("scale", folly::dynamic(scale));

    return scene.applyAnimatedProps(kNodeTag, folly::dynamic::object("transform", folly::dynamic::array(operation)));
}

TEST(NonFiniteAnimatedBoundary, ANonFiniteTransformOperationIsRejected) {
    RetainedScene scene;
    const std::vector<react_native_linux::RejectedAnimatedProp> rejected = animateScale(scene, kNotANumber);

    ASSERT_EQ(rejected.size(), 1U);
    EXPECT_EQ(rejected[0].name, "transform");
    EXPECT_EQ(rejected[0].rejection, react_native_linux::AnimatedPropRejection::NonFinite);
    EXPECT_FLOAT_EQ(scene.snapshot()[0].matrix.scaleX, 1.0F);
}

TEST(NonFiniteAnimatedBoundary, AFiniteTransformOperationStillApplies) {
    RetainedScene scene;

    EXPECT_TRUE(animateScale(scene, 2.0).empty());
    EXPECT_FLOAT_EQ(scene.snapshot()[0].matrix.scaleX, 2.0F);
}
