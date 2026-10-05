#include "SceneTestSupport.h"

#include <array>
#include <cmath>
#include <folly/dynamic.h>
#include <gtest/gtest.h>
#include <numbers>

#include <react/renderer/components/view/conversions.h>
#include <react/renderer/core/PropsParserContext.h>
#include <react/utils/ContextContainer.h>

namespace {

using react_native_linux::SceneMatrix;

constexpr float kQuarterTurn = std::numbers::pi_v<float> / 2.0F;
constexpr float kTolerance = 1e-3F;
const Rect kFrame = makeRect(10, 20, 100, 50);

/** What the scene paints the frame with: the snapshot's own matrix for a view carrying these props. */
SceneMatrix paintedMatrix(const Transform& transform, const facebook::react::TransformOrigin& origin) {
    const std::shared_ptr<ViewProps> props = propsWithBackground(blue());
    RetainedScene scene;

    props->transform = transform;
    props->transformOrigin = origin;
    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeStyledView(2, kFrame, props));

    return scene.snapshot().front().matrix;
}

/** CSS Transforms: a point moves by `origin + R(point - origin)`, the origin taken from the border box. */
Point cssQuarterTurn(Point origin, Point point) {
    return Point{.x = origin.x - (point.y - origin.y), .y = origin.y + (point.x - origin.x)};
}

struct OriginCase {
    const char* name;
    facebook::react::TransformOrigin origin;
    Point resolved;
};

/** #104 item 1: every component shape `processTransformOrigin` can hand to C++, against CSS's own resolution. */
const std::array kOriginCases{
    OriginCase{.name = "unset is the centre", .origin = {}, .resolved = {.x = 60, .y = 45}},
    OriginCase{.name = "points from the top-left",
               .origin = {.xy = {ValueUnit{0, UnitType::Point}, ValueUnit{0, UnitType::Point}}},
               .resolved = {.x = 10, .y = 20}},
    OriginCase{.name = "percentages of the frame",
               .origin = {.xy = {ValueUnit{100, UnitType::Percent}, ValueUnit{100, UnitType::Percent}}},
               .resolved = {.x = 110, .y = 70}},
    OriginCase{.name = "a percentage and a length mixed",
               .origin = {.xy = {ValueUnit{25, UnitType::Percent}, ValueUnit{10, UnitType::Point}}},
               .resolved = {.x = 35, .y = 30}},
    OriginCase{.name = "fifty percent is the centre",
               .origin = {.xy = {ValueUnit{50, UnitType::Percent}, ValueUnit{50, UnitType::Percent}}},
               .resolved = {.x = 60, .y = 45}},
};

TEST(TransformOriginTest, AQuarterTurnMovesEveryCornerAboutTheResolvedOriginAsCssDoes) {
    for (const OriginCase& testCase : kOriginCases) {
        const SceneMatrix matrix = paintedMatrix(Transform::RotateZ(kQuarterTurn), testCase.origin);

        for (const Point corner :
             {kFrame.origin, Point{.x = 110, .y = 20}, Point{.x = 110, .y = 70}, Point{.x = 10, .y = 70}}) {
            const Point painted = react_native_linux::mapPoint(matrix, corner);
            const Point expected = cssQuarterTurn(testCase.resolved, corner);

            EXPECT_NEAR(painted.x, expected.x, kTolerance) << testCase.name;
            EXPECT_NEAR(painted.y, expected.y, kTolerance) << testCase.name;
        }
    }
}

Transform parseTransform(const folly::dynamic& operations) {
    const facebook::react::ContextContainer contextContainer;
    const facebook::react::PropsParserContext parserContext{0, contextContainer};
    Transform transform;

    facebook::react::fromRawValue(parserContext, facebook::react::RawValue{operations}, transform);

    return transform;
}

/** #104 item 3, react-native#47467: a `matrix` operation and the operation list it spells paint identically. */
TEST(TransformOriginTest, AMatrixOperationAndTheOperationListItSpellsPaintTheSame) {
    const facebook::react::TransformOrigin topLeft{
        .xy = {ValueUnit{0, UnitType::Point}, ValueUnit{0, UnitType::Point}}};
    const SceneMatrix fromList =
        paintedMatrix(parseTransform(folly::dynamic::array(folly::dynamic::object("rotate", "90deg"))), topLeft);
    const SceneMatrix fromMatrix =
        paintedMatrix(parseTransform(folly::dynamic::array(folly::dynamic::object(
                          "matrix", folly::dynamic::array(0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1)))),
                      topLeft);

    EXPECT_NEAR(fromList.scaleX, fromMatrix.scaleX, kTolerance);
    EXPECT_NEAR(fromList.skewX, fromMatrix.skewX, kTolerance);
    EXPECT_NEAR(fromList.skewY, fromMatrix.skewY, kTolerance);
    EXPECT_NEAR(fromList.scaleY, fromMatrix.scaleY, kTolerance);
    EXPECT_NEAR(fromList.translateX, fromMatrix.translateX, kTolerance);
    EXPECT_NEAR(fromList.translateY, fromMatrix.translateY, kTolerance);
}

/**
 * The largest distance, over the frame's corners, between where the full 4x4 projects a corner (with the
 * perspective divide) and where the scene's 2D affine reduction paints it. Both about the frame's centre, as React
 * Native applies a transform.
 */
float largestReductionError(const Transform& transform) {
    const SceneMatrix affine = paintedMatrix(transform, {});
    const Point center{.x = 60, .y = 45};
    float largest = 0;

    for (const Point corner :
         {kFrame.origin, Point{.x = 110, .y = 20}, Point{.x = 110, .y = 70}, Point{.x = 10, .y = 70}}) {
        const auto& matrix = transform.matrix;
        const float x = corner.x - center.x;
        const float y = corner.y - center.y;
        const float w = (x * matrix[3]) + (y * matrix[7]) + matrix[15];
        const float projectedX = (((x * matrix[0]) + (y * matrix[4]) + matrix[12]) / w) + center.x;
        const float projectedY = (((x * matrix[1]) + (y * matrix[5]) + matrix[13]) / w) + center.y;
        const Point painted = react_native_linux::mapPoint(affine, corner);

        largest = std::max(largest, std::hypot(projectedX - painted.x, projectedY - painted.y));
    }

    return largest;
}

/**
 * #104 item 4: what the affine reduction costs, as numbers rather than adjectives. A rotation about X or Y with no
 * `perspective` is exact for a flat view, because the dropped depth column only ever multiplies a zero; with
 * `perspective`, the dropped divide is the whole error, and these are its recorded sizes on a 100x50 frame turned
 * 30 degrees about Y: 1.28 pixels at `perspective: 1000`, 7.14 at `perspective: 200`.
 */
TEST(TransformOriginTest, TheAffineReductionIsExactWithoutPerspectiveAndItsErrorWithPerspectiveIsRecorded) {
    const float thirtyDegrees = std::numbers::pi_v<float> / 6.0F;

    EXPECT_NEAR(largestReductionError(Transform::RotateX(thirtyDegrees)), 0.0F, kTolerance);
    EXPECT_NEAR(largestReductionError(Transform::RotateY(thirtyDegrees)), 0.0F, kTolerance);
    EXPECT_NEAR(largestReductionError(Transform::Perspective(1000) * Transform::RotateY(thirtyDegrees)), 1.282F,
                0.001F);
    EXPECT_NEAR(largestReductionError(Transform::Perspective(200) * Transform::RotateY(thirtyDegrees)), 7.143F, 0.001F);
}

} // namespace
