#pragma once

#include <react/renderer/attributedstring/TextAttributes.h>

namespace react_native_linux {

/**
 * A paragraph's base direction (#72): an explicit `writingDirection` decides it, and a natural or unset one
 * follows the direction the paragraph is laid out in, which Yoga inherits from the nearest `direction` above it.
 * The base direction is what `textAlign: 'auto' | 'start' | 'end'` aligns against and where the bidi algorithm
 * puts neutral characters, so `"Hello, world!"` in an RTL paragraph ends with its `!` on the left.
 */
bool isRightToLeft(const facebook::react::TextAttributes& attributes) noexcept;

} // namespace react_native_linux
