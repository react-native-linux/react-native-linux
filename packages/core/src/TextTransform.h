#pragma once

#include <react/renderer/attributedstring/primitives.h>

#include <string>

namespace react_native_linux {

/**
 * React Native's `textTransform`, applied to a fragment's string before it reaches SkParagraph.
 *
 * This has to run before shaping, not after: it changes the string's *length*, and a transform applied to the
 * already-shaped run would let the measured string and the painted string disagree about where a line breaks —
 * the exact failure mode `layoutParagraph` (`src/TextPipeline.cpp`) exists to avoid. Applying it here, to every
 * fragment before `ParagraphBuilder::addText`, means both `TextLayoutManager::measure` and the painter see the
 * transformed string, because both call `layoutParagraph`.
 *
 * It is pure string arithmetic — no Skia, no ICU — so it is under the 100% coverage gate rather than proven only
 * by a golden. Case mapping covers ASCII and the Latin-1 Supplement letters (U+00C0-U+00DE upper, U+00E0-U+00FE
 * lower, excluding the multiplication and division signs at U+00D7/U+00F7), which is what the vendored Noto Sans
 * carries alongside plain ASCII; a codepoint outside that range is copied through unchanged rather than
 * approximated. `Capitalize` uppercases the first letter of the string and of every run following ASCII
 * whitespace and lowercases the rest of each word, matching React Native's actual behaviour
 * (react/react-native#34117): "I am PSYCHED" becomes "I Am Psyched", not CSS's "I am PSYCHED" with only the
 * first letter of each word touched. A hyphen is still not a word boundary either way.
 */
std::string applyTextTransform(const std::string& utf8Text, facebook::react::TextTransform transform);

} // namespace react_native_linux
