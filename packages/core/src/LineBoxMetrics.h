#pragma once

#include <optional>

namespace react_native_linux {

/**
 * Where one line of text sits inside its line box, in points, measured from the top of that box.
 *
 * `halfLeading` is the space `lineHeight` adds beyond the glyphs, split in two. It is **negative** when
 * `lineHeight` is smaller than the font's own ascent plus descent: the line box is then exactly `lineHeight`, the
 * glyphs overflow it, and nothing clips them. That is the case react-native#49886 reports as clipped descenders,
 * and the policy is written up as *Vertical metrics (#110)* in docs/cpp-toolchain.md.
 */
struct LineBoxMetrics {
    float lineBoxHeight{0.0F};
    float halfLeading{0.0F};
    float baselineFromTop{0.0F};
};

/**
 * React Native's absolute `lineHeight` as the multiple-of-font-size `height` SkParagraph's `TextStyle` takes.
 *
 * Empty when there is nothing to override — `lineHeight` is absent, which React Native spells NaN, or the font
 * size is not positive and the ratio has no meaning.
 */
std::optional<float> lineHeightRatio(float fontSize, float lineHeight);

/**
 * The line box one line of `fontSize` text occupies in a font reporting `ascent` and `descent`, with `lineHeight`
 * applied when there is one.
 *
 * The leading is split evenly above and below the glyphs — react-native#39145 is the one-sided version of this —
 * and every line of a paragraph gets the same box, first and last included.
 *
 * Nothing reports a baseline to Fabric yet: the vendored `TextLayoutManager` header declares no `measureLines`,
 * so `measure` answers Yoga with a size and no line boxes. This is the arithmetic that path will report when it
 * exists; `lineHeightRatio` is the half SkParagraph already consumes.
 */
LineBoxMetrics measureLineBox(float ascent, float descent, float fontSize, float lineHeight);

} // namespace react_native_linux
