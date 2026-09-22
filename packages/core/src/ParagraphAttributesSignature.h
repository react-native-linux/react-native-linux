#pragma once

#include <string>

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>

namespace react_native_linux {

/**
 * Everything a paragraph measurement depends on except the text itself and the wrap width: the paragraph's line
 * limit and ellipsize mode, then, per fragment, its length and every attribute that can move a glyph, or an
 * attachment's own dimensions.
 *
 * Pure over React Native's types and Skia-free, which is what lets the collision case be asserted directly: two
 * strings that join to the same text but split the style boundary differently — `["ab" bold, "c" plain]` and
 * `["a" bold, "bc" plain]` — measure differently, and without each fragment's length in the signature they
 * would collide in the cache key. Colour is deliberately absent: the metrics answer no colour question, so a
 * re-tinted row reuses its measurement.
 */
std::string toAttributesSignature(const facebook::react::AttributedString& attributedString,
                                  const facebook::react::ParagraphAttributes& paragraphAttributes);

} // namespace react_native_linux
