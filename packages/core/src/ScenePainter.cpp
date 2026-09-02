#include "ScenePainter.h"

#include "TextPipeline.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "modules/skparagraph/include/Paragraph.h"

#include <algorithm>
#include <array>
#include <memory>

namespace react_native_linux {

namespace {

SkMatrix toSkMatrix(const SceneMatrix& matrix) {
    return SkMatrix::MakeAll(matrix.scaleX, matrix.skewX, matrix.translateX, matrix.skewY, matrix.scaleY,
                             matrix.translateY, 0, 0, 1);
}

SkRect toSkRect(const facebook::react::Rect& frame) {
    return SkRect::MakeXYWH(frame.origin.x, frame.origin.y, frame.size.width, frame.size.height);
}

SkRRect toSkRRect(const SkRect& bounds, const facebook::react::BorderRadii& radii) {
    const std::array<SkVector, 4> corners{SkVector{radii.topLeft.horizontal, radii.topLeft.vertical},
                                          SkVector{radii.topRight.horizontal, radii.topRight.vertical},
                                          SkVector{radii.bottomRight.horizontal, radii.bottomRight.vertical},
                                          SkVector{radii.bottomLeft.horizontal, radii.bottomLeft.vertical}};
    SkRRect roundedRect;

    roundedRect.setRectRadii(bounds, corners.data());

    return roundedRect;
}

/**
 * React Native draws borders inside the frame, so the inner edge is the frame inset by the per-side widths and the
 * corner radii reduced by the two widths that meet at that corner, which is the CSS inner-radius rule.
 */
SkRect innerBounds(const SkRect& bounds, const facebook::react::BorderWidths& widths) {
    const SkScalar left = std::min(bounds.fLeft + widths.left, bounds.fRight);
    const SkScalar top = std::min(bounds.fTop + widths.top, bounds.fBottom);

    return SkRect::MakeLTRB(left, top, std::max(bounds.fRight - widths.right, left),
                            std::max(bounds.fBottom - widths.bottom, top));
}

facebook::react::CornerRadii innerCorner(const facebook::react::CornerRadii& corner, facebook::react::Float horizontal,
                                         facebook::react::Float vertical) {
    return facebook::react::CornerRadii{.vertical = std::max(corner.vertical - vertical, 0.0F),
                                        .horizontal = std::max(corner.horizontal - horizontal, 0.0F)};
}

facebook::react::BorderRadii innerRadii(const facebook::react::BorderRadii& radii,
                                        const facebook::react::BorderWidths& widths) {
    return facebook::react::BorderRadii{.topLeft = innerCorner(radii.topLeft, widths.left, widths.top),
                                        .topRight = innerCorner(radii.topRight, widths.right, widths.top),
                                        .bottomLeft = innerCorner(radii.bottomLeft, widths.left, widths.bottom),
                                        .bottomRight = innerCorner(radii.bottomRight, widths.right, widths.bottom)};
}

/**
 * The wedge one border side owns: the two outer corners of that side and the two inner corners behind them, so the
 * corner is split on the diagonal. The four wedges tile the border ring, and clipping the ring by one of them
 * isolates exactly one side.
 */
SkPath edgeWedge(const SkPoint& firstOuter, const SkPoint& secondOuter, const SkPoint& secondInner,
                 const SkPoint& firstInner) {
    SkPathBuilder wedge;

    wedge.moveTo(firstOuter);
    wedge.lineTo(secondOuter);
    wedge.lineTo(secondInner);
    wedge.lineTo(firstInner);
    wedge.close();

    return wedge.detach();
}

void paintEdge(SkCanvas& canvas, const SkRRect& outer, const SkRRect& inner, const SkPath& wedge, uint32_t colorArgb) {
    if (SkColorGetA(colorArgb) == 0) {
        return;
    }

    SkPaint paint;

    paint.setAntiAlias(true);
    paint.setColor(colorArgb);

    const SkAutoCanvasRestore restore(&canvas, true);

    canvas.clipPath(wedge, true);
    canvas.drawDRRect(outer, inner, paint);
}

void paintBorder(SkCanvas& canvas, const ScenePrimitive& primitive, const SkRRect& outer) {
    const SkRect outerBounds = outer.rect();
    const SkRect inner = innerBounds(outerBounds, primitive.borderWidths);
    const SkRRect innerRoundedRect = toSkRRect(inner, innerRadii(primitive.borderRadii, primitive.borderWidths));
    const facebook::react::RectangleEdges<uint32_t>& colors = primitive.borderColorsArgb;

    if (colors.left == colors.top && colors.left == colors.right && colors.left == colors.bottom) {
        if (SkColorGetA(colors.left) == 0) {
            return;
        }

        SkPaint paint;

        paint.setAntiAlias(true);
        paint.setColor(colors.left);
        canvas.drawDRRect(outer, innerRoundedRect, paint);

        return;
    }

    const SkPoint outerTopLeft{outerBounds.fLeft, outerBounds.fTop};
    const SkPoint outerTopRight{outerBounds.fRight, outerBounds.fTop};
    const SkPoint outerBottomRight{outerBounds.fRight, outerBounds.fBottom};
    const SkPoint outerBottomLeft{outerBounds.fLeft, outerBounds.fBottom};
    const SkPoint innerTopLeft{inner.fLeft, inner.fTop};
    const SkPoint innerTopRight{inner.fRight, inner.fTop};
    const SkPoint innerBottomRight{inner.fRight, inner.fBottom};
    const SkPoint innerBottomLeft{inner.fLeft, inner.fBottom};

    paintEdge(canvas, outer, innerRoundedRect,
              edgeWedge(outerTopLeft, outerTopRight, innerTopRight, innerTopLeft), colors.top);
    paintEdge(canvas, outer, innerRoundedRect,
              edgeWedge(outerTopRight, outerBottomRight, innerBottomRight, innerTopRight), colors.right);
    paintEdge(canvas, outer, innerRoundedRect,
              edgeWedge(outerBottomRight, outerBottomLeft, innerBottomLeft, innerBottomRight), colors.bottom);
    paintEdge(canvas, outer, innerRoundedRect,
              edgeWedge(outerBottomLeft, outerTopLeft, innerTopLeft, innerBottomLeft), colors.left);
}

/**
 * Draws the paragraph in the content box the scene resolved, laid out against that box's width.
 *
 * That width is the one Yoga laid the node out with, and the paragraph is rebuilt from the same
 * `AttributedString` through the same `layoutParagraph` the measurement used, so the lines drawn here are the
 * lines that were measured.
 */
void paintText(SkCanvas& canvas, const SceneTextContent& text) {
    const std::unique_ptr<skia::textlayout::Paragraph> paragraph =
        layoutParagraph(text.attributedString, text.paragraphAttributes, static_cast<float>(text.frame.size.width));

    paragraph->paint(&canvas, text.frame.origin.x, text.frame.origin.y);
}

void paintPrimitive(SkCanvas& canvas, const ScenePrimitive& primitive) {
    const SkRRect outer = toSkRRect(toSkRect(primitive.frame), primitive.borderRadii);

    if (SkColorGetA(primitive.backgroundColorArgb) != 0) {
        SkPaint paint;

        paint.setAntiAlias(true);
        paint.setColor(primitive.backgroundColorArgb);

        // drawRRect is documented to accept a square-cornered rrect, but not documented to route it to drawRect.
        // Saying so here keeps the pre-radius goldens byte-identical instead of relying on an internal fast path.
        if (outer.isRect()) {
            canvas.drawRect(outer.rect(), paint);
        } else {
            canvas.drawRRect(outer, paint);
        }
    }

    paintBorder(canvas, primitive, outer);

    if (primitive.text.has_value()) {
        paintText(canvas, primitive.text.value());
    }
}

/**
 * Clips to the union of the damage rectangles, in the canvas' current coordinate space.
 *
 * Each rectangle is rounded out to whole pixels and outset by one before it becomes part of the clip path, and
 * the clip itself is not anti-aliased: a partially covered clip edge would blend the new drawing into whatever
 * the previous frame left there, and a partial pixel is exactly what a full repaint would not produce.
 */
void clipToDamage(SkCanvas& canvas, const SceneDamage& damage) {
    SkPathBuilder region;

    for (const facebook::react::Rect& rect : damage) {
        SkIRect pixels = toSkRect(rect).roundOut();

        pixels.outset(1, 1);
        region.addRect(SkRect::Make(pixels));
    }

    canvas.clipPath(region.detach(), false);
}

} // namespace

void paintScene(SkCanvas& canvas, const SceneSnapshot& scene, const SceneDamage& damage) {
    const SkAutoCanvasRestore damageRestore(&canvas, true);

    if (!damage.empty()) {
        clipToDamage(canvas, damage);
    }

    canvas.clear(kSceneBackgroundColor);

    // Every matrix in a snapshot is absolute, so the canvas matrix is replaced rather than concatenated. The
    // surface's own matrix is preserved as the base so a scaled or translated canvas still composes correctly.
    const SkMatrix baseMatrix = canvas.getLocalToDeviceAs3x3();

    for (const ScenePrimitive& primitive : scene) {
        const SkAutoCanvasRestore restore(&canvas, true);

        for (const SceneClip& clip : primitive.clips) {
            canvas.setMatrix(SkMatrix::Concat(baseMatrix, toSkMatrix(clip.matrix)));
            canvas.clipRRect(toSkRRect(toSkRect(clip.frame), clip.borderRadii), true);
        }

        canvas.setMatrix(SkMatrix::Concat(baseMatrix, toSkMatrix(primitive.matrix)));
        paintPrimitive(canvas, primitive);
    }
}

} // namespace react_native_linux
