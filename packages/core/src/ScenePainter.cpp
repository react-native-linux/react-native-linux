#include "ScenePainter.h"

#include "GradientShader.h"
#include "ImageContent.h"
#include "ImagePipeline.h"
#include "TextGeometry.h"
#include "TextPipeline.h"

#include "include/core/SkBlendMode.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkImage.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPoint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkShader.h"
#include "include/core/SkTileMode.h"
#include "modules/skparagraph/include/Paragraph.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>

namespace react_native_linux {

namespace {

constexpr facebook::react::Float kCompositionUnderlineHeight = 1.0F;

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
void paintText(SkCanvas& canvas, const SceneTextContent& text, float layoutWidth) {
    const std::unique_ptr<skia::textlayout::Paragraph> paragraph =
        layoutParagraph(text.attributedString, text.paragraphAttributes, layoutWidth);

    paragraph->paint(&canvas, text.frame.origin.x, text.frame.origin.y);
}

void fillRect(SkCanvas& canvas, const facebook::react::Rect& rect, uint32_t colorArgb) {
    SkPaint paint;

    paint.setAntiAlias(false);
    paint.setColor(colorArgb);
    canvas.drawRect(toSkRect(rect), paint);
}

facebook::react::Rect offsetRect(const facebook::react::Rect& rect, facebook::react::Point origin) {
    return facebook::react::Rect{.origin = rect.origin + origin, .size = rect.size};
}

/**
 * A `<TextInput>`: the selection behind the text, the text, the composing run's underline, and the caret.
 *
 * All four are clipped to the field's content box and shifted by the field's own scroll offset, which is the
 * whole of horizontal scrolling for a single-line field — a caret that walked past the right edge moves the
 * paragraph left instead of drawing outside the box. react-native-macos#2905 is the same feature missing.
 *
 * The geometry is measured through `measureEditorGeometry`, which lays the paragraph out exactly as
 * `paintText` does two lines later. That is two layouts of one string per frame, and it is deliberate for now:
 * the alternative is a second paragraph type crossing the Skia-free header boundary, and SkParagraph's own
 * shaped-run cache absorbs the repeat. It belongs with the rest of the frame-time work in issue #20.
 */
void paintEditor(SkCanvas& canvas, const SceneTextContent& text, const SceneEditorContent& editor) {
    const SkAutoCanvasRestore restore(&canvas, true);
    const EditorGeometryRequest request{.caretUtf16 = editor.state.caretUtf16,
                                        .selectionBeginUtf16 = editor.state.selectionBeginUtf16,
                                        .selectionEndUtf16 = editor.state.selectionEndUtf16,
                                        .compositionBeginUtf16 = editor.state.compositionBeginUtf16,
                                        .compositionEndUtf16 = editor.state.compositionEndUtf16,
                                        .isMultiline = editor.isMultiline};
    const EditorGeometry geometry = measureEditorGeometry(text.attributedString, text.paragraphAttributes,
                                                          static_cast<float>(text.frame.size.width), request);

    canvas.clipRect(toSkRect(text.frame), false);
    canvas.translate(-editor.state.scrollOffsetX, 0);

    for (const facebook::react::Rect& selection : geometry.selection) {
        fillRect(canvas, offsetRect(selection, text.frame.origin), editor.selectionColorArgb);
    }

    paintText(canvas, text, geometry.layoutWidth);

    for (const facebook::react::Rect& composition : geometry.composition) {
        const facebook::react::Rect underline{
            .origin = facebook::react::Point{.x = composition.origin.x + text.frame.origin.x,
                                             .y = composition.origin.y + text.frame.origin.y +
                                                  composition.size.height - kCompositionUnderlineHeight},
            .size = facebook::react::Size{.width = composition.size.width,
                                          .height = kCompositionUnderlineHeight}};

        fillRect(canvas, underline, editor.caretColorArgb);
    }

    if (editor.state.isCaretVisible) {
        fillRect(canvas, offsetRect(geometry.caret, text.frame.origin), editor.caretColorArgb);
    }
}

/**
 * Draws the decoded image inside the node's border box, clipped to the same rounded rectangle the background is
 * filled with, so a rounded `<Image>` is cut by its own corners and a `cover` fit is cut by its own frame.
 *
 * A source that has not finished decoding draws nothing. That is not an error state: the decode damages the frame
 * when it completes, so the next frame draws it.
 *
 * `repeat` is the one mode that is not a single `drawImageRect`: the placement rectangle is the first tile, and a
 * repeating shader anchored at it fills the frame.
 */
void paintImage(SkCanvas& canvas, const ScenePrimitive& primitive, const SceneImageContent& image,
                const SkRRect& outer) {
    const sk_sp<SkImage> decoded = decodedImage(image.uri);

    if (decoded == nullptr) {
        return;
    }

    const facebook::react::Size imageSize{.width = static_cast<facebook::react::Float>(decoded->width()),
                                          .height = static_cast<facebook::react::Float>(decoded->height())};
    const facebook::react::Rect placement = imagePlacement(image.resizeMode, primitive.frame, imageSize);
    const SkAutoCanvasRestore restore(&canvas, true);
    const SkSamplingOptions sampling{SkFilterMode::kLinear, SkMipmapMode::kNone};
    SkPaint paint;

    canvas.clipRRect(outer, true);
    paint.setAntiAlias(true);

    // A tint carries the inherited opacity in its own alpha, because `SkSrcIn` multiplies the constant colour by
    // the image's coverage; setting the paint alpha as well would apply it twice.
    if (SkColorGetA(image.tintColorArgb) != 0) {
        paint.setColorFilter(SkColorFilters::Blend(image.tintColorArgb, SkBlendMode::kSrcIn));
    } else {
        paint.setAlphaf(image.opacity);
    }

    if (image.resizeMode == SceneImageResizeMode::Repeat) {
        paint.setShader(decoded->makeShader(SkTileMode::kRepeat, SkTileMode::kRepeat, sampling,
                                            SkMatrix::Translate(placement.origin.x, placement.origin.y)));
        canvas.drawRect(toSkRect(primitive.frame), paint);

        return;
    }

    canvas.drawImageRect(decoded, toSkRect(placement), sampling, &paint);
}

/**
 * The focus ring, stroked on the inside edge of the node's own rounded border box.
 *
 * Inside rather than around it, which is the one place this departs from the macOS ring: an outset ring would
 * paint outside the frame the scene damages for that node, and every damage rectangle in this renderer is a
 * primitive's own transformed frame. A ring the damage math already covers needs no term of its own, and
 * react-native-macos#2063 is what an outset ring that disagrees with its own geometry looks like.
 */
void paintFocusRing(SkCanvas& canvas, const ScenePrimitive& primitive) {
    // Half the stroke, because a stroke is centred on its path: the ring then occupies exactly the outermost
    // kFocusRingWidth points of the frame and not one point outside it.
    constexpr facebook::react::Float kRingInset = kFocusRingWidth / 2;
    const facebook::react::BorderWidths ringWidths{
        .left = kRingInset, .top = kRingInset, .right = kRingInset, .bottom = kRingInset};
    const SkRect ringBounds = innerBounds(toSkRect(primitive.frame), ringWidths);
    const SkRRect ring = toSkRRect(ringBounds, innerRadii(primitive.borderRadii, ringWidths));

    SkPaint paint;

    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(kFocusRingWidth);
    paint.setColor(kFocusRingColor);
    canvas.drawRRect(ring, paint);
}

/**
 * Fills the node's own rounded border box, which is the one geometry the solid background colour and every
 * gradient layer share, so a rounded corner cuts all of them identically.
 */
void fillBorderBox(SkCanvas& canvas, const SkRRect& outer, const SkPaint& paint) {
    // drawRRect is documented to accept a square-cornered rrect, but not documented to route it to drawRect.
    // Saying so here keeps the pre-radius goldens byte-identical instead of relying on an internal fast path.
    if (outer.isRect()) {
        canvas.drawRect(outer.rect(), paint);
    } else {
        canvas.drawRRect(outer, paint);
    }
}

/**
 * The `experimental_backgroundImage` gradient layers, over the solid background colour and under everything else.
 *
 * Back to front, because CSS paints the first background image nearest the viewer — which is why React Native's
 * Android drawable walks the same list in reverse. The node's opacity is the paint alpha rather than something
 * multiplied into every stop.
 */
void paintBackgroundImages(SkCanvas& canvas, const ScenePrimitive& primitive, const SkRRect& outer) {
    for (size_t remaining = primitive.backgroundImage.size(); remaining > 0; remaining--) {
        const sk_sp<SkShader> shader = makeGradientShader(primitive.backgroundImage[remaining - 1], primitive.frame);

        if (shader == nullptr) {
            continue;
        }

        SkPaint paint;

        paint.setAntiAlias(true);
        paint.setAlphaf(primitive.backgroundImageOpacity);
        paint.setShader(shader);
        fillBorderBox(canvas, outer, paint);
    }
}

void paintPrimitive(SkCanvas& canvas, const ScenePrimitive& primitive) {
    const SkRRect outer = toSkRRect(toSkRect(primitive.frame), primitive.borderRadii);

    if (SkColorGetA(primitive.backgroundColorArgb) != 0) {
        SkPaint paint;

        paint.setAntiAlias(true);
        paint.setColor(primitive.backgroundColorArgb);
        fillBorderBox(canvas, outer, paint);
    }

    paintBackgroundImages(canvas, primitive, outer);

    // Before the border, because React Native draws borders inside the frame and therefore over the content.
    if (primitive.image.has_value()) {
        paintImage(canvas, primitive, primitive.image.value(), outer);
    }

    paintBorder(canvas, primitive, outer);

    if (primitive.text.has_value() && primitive.editor.has_value()) {
        paintEditor(canvas, primitive.text.value(), primitive.editor.value());
    } else if (primitive.text.has_value()) {
        paintText(canvas, primitive.text.value(), static_cast<float>(primitive.text.value().frame.size.width));
    }

    // Last, so the ring is never covered by the node's own content.
    if (primitive.focusRing) {
        paintFocusRing(canvas, primitive);
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
