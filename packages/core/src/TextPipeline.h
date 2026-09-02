#pragma once

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>

#include "modules/skparagraph/include/Paragraph.h"

#include <memory>

namespace react_native_linux {

/**
 * Turns React Native's text model into a laid-out SkParagraph. This is the whole text pipeline: shaping is
 * HarfBuzz inside `libskshaper`, segmentation is ICU inside `libskunicode_icu`, rasterisation is FreeType, and
 * font lookup is the `FontCollection` built here. There is no font abstraction between them, and there is exactly
 * one of these functions, because measurement and painting disagreeing about line breaks is the failure mode this
 * whole design exists to avoid.
 *
 * `TextLayoutManager::measure` calls it during Yoga layout and `paintScene` calls it while drawing, with the same
 * `AttributedString` and `ParagraphAttributes`: the scene carries those two values from `ParagraphState` to the
 * painter untouched apart from the inherited opacity folded into fragment colours, which changes no metric.
 *
 * Fonts resolve in two steps. The asset font manager is a directory manager over the fonts vendored into
 * `packages/core/fonts`, so the default family is one pinned file rather than whatever the machine happens to
 * have installed; that is what makes a text golden reproducible on both CI and a developer machine. Anything the
 * vendored family cannot supply — a font family a bundle asks for by name, or a codepoint the bundled font has no
 * glyph for — falls through to fontconfig, which is the system font manager and is not reproducible across
 * machines. Goldens must therefore stay inside the vendored font's coverage.
 *
 * Threading contract: the returned `Paragraph` belongs to the caller and is never shared, but the `FontCollection`
 * behind it is a process-wide singleton carrying Skia's own paragraph cache, and neither it nor `layout` is
 * documented as thread-safe. Layout therefore runs under one mutex, which is what makes it safe for the commit
 * thread to measure while the frame thread paints.
 */
std::unique_ptr<skia::textlayout::Paragraph>
layoutParagraph(const facebook::react::AttributedString& attributedString,
                const facebook::react::ParagraphAttributes& paragraphAttributes, float maximumWidth);

} // namespace react_native_linux
