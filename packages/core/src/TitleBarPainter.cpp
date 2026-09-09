#include "TitleBarPainter.h"

#include "ResourceResolver.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkScalar.h"
#include "include/core/SkTextBlob.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontMgr_directory.h"

namespace react_native_linux {

namespace {

constexpr SkColor kActiveBarColor = SkColorSetRGB(0x22, 0x26, 0x2E);
constexpr SkColor kInactiveBarColor = SkColorSetRGB(0x1A, 0x1D, 0x22);
constexpr SkColor kSeparatorColor = SkColorSetRGB(0x2E, 0x34, 0x40);
constexpr SkColor kActiveForegroundColor = SkColorSetRGB(0xE6, 0xE9, 0xEF);
constexpr SkColor kInactiveForegroundColor = SkColorSetRGB(0x7A, 0x81, 0x8E);

constexpr SkScalar kSeparatorHeight = 1.0F;
constexpr SkScalar kTitleLeftPadding = 12.0F;
constexpr SkScalar kTitleSize = 13.0F;
constexpr SkScalar kGlyphHalfExtent = 5.0F;
constexpr SkScalar kGlyphStrokeWidth = 1.5F;

constexpr char kTitleFontFamily[] = "Noto Sans";

SkPoint centreOf(const DecorationRect& rectangle) {
    return SkPoint::Make((rectangle.left + rectangle.right) / 2.0F, (rectangle.top + rectangle.bottom) / 2.0F);
}

SkRect toSkRect(const DecorationRect& rectangle) {
    return SkRect::MakeLTRB(rectangle.left, rectangle.top, rectangle.right, rectangle.bottom);
}

const SkFont& titleFont() {
    static const SkFont font(
        SkFontMgr_New_Custom_Directory(react_native_linux::ResourceResolver::runtimeFontDirectory(
                                           std::optional<std::filesystem::path>(RNL_BUNDLED_FONT_DIR))
                                           .string()
                                           .c_str())
            ->matchFamilyStyle(kTitleFontFamily, SkFontStyle()),
        kTitleSize);

    return font;
}

void paintMinimizeGlyph(SkCanvas& canvas, const DecorationRect& button, const SkPaint& paint) {
    const SkPoint centre = centreOf(button);

    canvas.drawLine(centre.fX - kGlyphHalfExtent, centre.fY + kGlyphHalfExtent, centre.fX + kGlyphHalfExtent,
                    centre.fY + kGlyphHalfExtent, paint);
}

void paintMaximizeGlyph(SkCanvas& canvas, const DecorationRect& button, const SkPaint& paint) {
    const SkPoint centre = centreOf(button);

    canvas.drawRect(SkRect::MakeLTRB(centre.fX - kGlyphHalfExtent, centre.fY - kGlyphHalfExtent,
                                     centre.fX + kGlyphHalfExtent, centre.fY + kGlyphHalfExtent),
                    paint);
}

void paintCloseGlyph(SkCanvas& canvas, const DecorationRect& button, const SkPaint& paint) {
    const SkPoint centre = centreOf(button);

    canvas.drawLine(centre.fX - kGlyphHalfExtent, centre.fY - kGlyphHalfExtent, centre.fX + kGlyphHalfExtent,
                    centre.fY + kGlyphHalfExtent, paint);
    canvas.drawLine(centre.fX + kGlyphHalfExtent, centre.fY - kGlyphHalfExtent, centre.fX - kGlyphHalfExtent,
                    centre.fY + kGlyphHalfExtent, paint);
}

} // namespace

void paintTitleBar(SkCanvas& canvas, const TitleBarLayout& layout, const std::string& title, bool isActive) {
    const SkColor foregroundColor = isActive ? kActiveForegroundColor : kInactiveForegroundColor;

    SkPaint barPaint;
    barPaint.setColor(isActive ? kActiveBarColor : kInactiveBarColor);
    canvas.drawRect(toSkRect(layout.bar), barPaint);

    SkPaint separatorPaint;
    separatorPaint.setColor(kSeparatorColor);
    canvas.drawRect(
        SkRect::MakeLTRB(layout.bar.left, layout.bar.bottom - kSeparatorHeight, layout.bar.right, layout.bar.bottom),
        separatorPaint);

    SkPaint textPaint;
    textPaint.setColor(foregroundColor);
    textPaint.setAntiAlias(true);

    const SkFont& font = titleFont();
    const SkScalar baseline = (layout.bar.bottom + font.getSize()) / 2.0F;

    canvas.save();
    canvas.clipRect(toSkRect(layout.drag));
    canvas.drawTextBlob(SkTextBlob::MakeFromString(title.c_str(), font), layout.bar.left + kTitleLeftPadding, baseline,
                        textPaint);
    canvas.restore();

    SkPaint glyphPaint;
    glyphPaint.setColor(foregroundColor);
    glyphPaint.setAntiAlias(true);
    glyphPaint.setStyle(SkPaint::kStroke_Style);
    glyphPaint.setStrokeWidth(kGlyphStrokeWidth);

    paintMinimizeGlyph(canvas, layout.minimize, glyphPaint);
    paintMaximizeGlyph(canvas, layout.maximize, glyphPaint);
    paintCloseGlyph(canvas, layout.close, glyphPaint);
}

} // namespace react_native_linux
