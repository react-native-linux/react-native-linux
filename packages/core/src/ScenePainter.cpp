#include "ScenePainter.h"

#include "GradientShader.h"
#include "ImageContent.h"
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

#include <array>
#include <cstddef>
#include <cstdint>
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

SkRect toSkRect(const SceneRoundedBox& box) {
    return toSkRect(box.bounds);
}

/**
 * The one `SkRRect` every consumer of a node's shape draws with: the fill, the ring's two edges, the
 * `overflow: hidden` clip, the content clip and — through `roundedBoxContainsPoint` rather than this — the hit
 * region. Nothing in this file computes a rounded rect of its own; issue #99 is that rule.
 */
SkRRect toSkRRect(const SceneRoundedBox& box) {
    const facebook::react::BorderRadii& radii = box.radii;
    const std::array<SkVector, 4> corners{SkVector{radii.topLeft.horizontal, radii.topLeft.vertical},
                                          SkVector{radii.topRight.horizontal, radii.topRight.vertical},
                                          SkVector{radii.bottomRight.horizontal, radii.bottomRight.vertical},
                                          SkVector{radii.bottomLeft.horizontal, radii.bottomLeft.vertical}};
    SkRRect roundedRect;

    roundedRect.setRectRadii(toSkRect(box), corners.data());

    return roundedRect;
}

/**
 * How far each mitre is pushed into the neighbouring side, in the primitive's own coordinates.
 *
 * Two anti-aliased wedges that merely abut leave the diagonal half-covered by each: composited one after the
 * other, a quarter of the background survives between two opaque sides. That is the hairline seam
 * [core#33950](https://github.com/facebook/react-native/issues/33950) reports and that this file used to have.
 * Overlapping them instead means the later side is drawn at full coverage over the earlier one, so the mitre is a
 * colour boundary rather than a gap, at the cost of displacing the diagonal by at most this much.
 */
constexpr SkScalar kMitreOverlap = 1.0F;

/**
 * The wedge one border side owns: its own outer edge, its own inner edge, and the two diagonals between them, so
 * each corner is split on the diagonal and the four wedges tile the ring. Clipping the ring by one isolates one
 * side.
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

/**
 * The four wedges, in paint order: top, right, bottom, left. Each is grown by `kMitreOverlap` along its own
 * tangential axis, which pushes both of its mitres into its neighbours; the clip is intersected with the ring
 * either way, so growing past the frame draws nothing extra.
 */
std::array<SkPath, 4> edgeWedges(const SkRect& outer, const SkRect& inner) {
    return {edgeWedge({outer.fLeft - kMitreOverlap, outer.fTop}, {outer.fRight + kMitreOverlap, outer.fTop},
                      {inner.fRight + kMitreOverlap, inner.fTop}, {inner.fLeft - kMitreOverlap, inner.fTop}),
            edgeWedge({outer.fRight, outer.fTop - kMitreOverlap}, {outer.fRight, outer.fBottom + kMitreOverlap},
                      {inner.fRight, inner.fBottom + kMitreOverlap}, {inner.fRight, inner.fTop - kMitreOverlap}),
            edgeWedge({outer.fRight + kMitreOverlap, outer.fBottom}, {outer.fLeft - kMitreOverlap, outer.fBottom},
                      {inner.fLeft - kMitreOverlap, inner.fBottom}, {inner.fRight + kMitreOverlap, inner.fBottom}),
            edgeWedge({outer.fLeft, outer.fBottom + kMitreOverlap}, {outer.fLeft, outer.fTop - kMitreOverlap},
                      {inner.fLeft, inner.fTop - kMitreOverlap}, {inner.fLeft, inner.fBottom + kMitreOverlap})};
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

/**
 * The border ring: the difference between the node's rounded border box and the content box the per-side widths
 * leave inside it, painted in each side's own colour.
 *
 * `drawDRRect` rather than four filled trapezoids, because the ring is where the two rounded rects differ and
 * Skia already has that primitive: building the outer arc, the inner arc and the two mitres into a path per side
 * would re-derive by hand the geometry `SceneRoundedBox` already carries, and would have to re-derive the CSS
 * inner-radius rule with it. Four identical colours skip the clips entirely and draw the whole ring at once,
 * which is both the common case and the only way an entirely mitre-free ring is drawn.
 *
 * A side whose colour is fully transparent draws nothing and takes nothing away from its neighbours, which is the
 * [core#34722](https://github.com/facebook/react-native/issues/34722) case: one transparent side must not erase
 * the other three. The ring is drawn straight onto the canvas with no layer, so a translucent border composites
 * against whatever is already there — [core#39286](https://github.com/facebook/react-native/issues/39286) — and
 * a translucent border on a rounded box is no different from an opaque one:
 * [core#49606](https://github.com/facebook/react-native/issues/49606).
 */
void paintBorder(SkCanvas& canvas, const ScenePrimitive& primitive, const SceneRoundedBox& borderBox,
                 const SkRRect& outer) {
    const SceneRoundedBox contentBox = roundedContentBox(borderBox, primitive.borderWidths);
    const SkRRect inner = toSkRRect(contentBox);
    const facebook::react::RectangleEdges<uint32_t>& colors = primitive.borderColorsArgb;

    if (colors.left == colors.top && colors.left == colors.right && colors.left == colors.bottom) {
        if (SkColorGetA(colors.left) == 0) {
            return;
        }

        SkPaint paint;

        paint.setAntiAlias(true);
        paint.setColor(colors.left);
        canvas.drawDRRect(outer, inner, paint);

        return;
    }

    const std::array<SkPath, 4> wedges = edgeWedges(toSkRect(borderBox), toSkRect(contentBox));
    const std::array<uint32_t, 4> sideColors{colors.top, colors.right, colors.bottom, colors.left};

    for (size_t side = 0; side < wedges.size(); side++) {
        paintEdge(canvas, outer, inner, wedges[side], sideColors[side]);
    }
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
    const EditorGeometry geometry = measureEditorGeometry(text, editor);

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
    // The node's own pixels, not a lookup by URI: the scene attached them when it mounted the node or when the
    // decode reported, so an eviction since then cannot blank a node this frame was snapshotted with (#108).
    SkImage* const decoded = static_cast<SkImage*>(image.pixels.get());

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
void paintFocusRing(SkCanvas& canvas, const SceneRoundedBox& borderBox) {
    // Half the stroke, because a stroke is centred on its path: the ring then occupies exactly the outermost
    // kFocusRingWidth points of the frame and not one point outside it.
    constexpr facebook::react::Float kRingInset = kFocusRingWidth / 2;
    const facebook::react::BorderWidths ringWidths{
        .left = kRingInset, .top = kRingInset, .right = kRingInset, .bottom = kRingInset};
    const SkRRect ring = toSkRRect(roundedContentBox(borderBox, ringWidths));

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
    const SceneRoundedBox borderBox = roundedBorderBox(primitive.frame, primitive.borderRadii);
    const SkRRect outer = toSkRRect(borderBox);

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

    paintBorder(canvas, primitive, borderBox, outer);

    if (primitive.text.has_value() && primitive.editor.has_value()) {
        paintEditor(canvas, primitive.text.value(), primitive.editor.value());
    } else if (primitive.text.has_value()) {
        paintText(canvas, primitive.text.value(), static_cast<float>(primitive.text.value().frame.size.width));
    }

    // Last, so the ring is never covered by the node's own content.
    if (primitive.focusRing) {
        paintFocusRing(canvas, borderBox);
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
            canvas.clipRRect(toSkRRect(roundedBorderBox(clip.frame, clip.borderRadii)), true);
        }

        canvas.setMatrix(SkMatrix::Concat(baseMatrix, toSkMatrix(primitive.matrix)));
        paintPrimitive(canvas, primitive);
    }
}

} // namespace react_native_linux
