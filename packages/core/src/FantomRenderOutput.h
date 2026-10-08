#pragma once

#include <string>

#include <react/renderer/mounting/stubs/StubViewTree.h>

namespace react_native_linux {

/**
 * `NativeFantom.getRenderedOutput`'s JSON (#115, #210): upstream's tester `RenderOutput::render`
 * (`private/react-native-fantom/tester/src/render/RenderOutput.cpp`), ported unchanged over the same
 * `StubViewTree`. A view is its component name, its debug props as strings, `layoutMetrics-` props when asked
 * for, and its children; a `Paragraph`'s children are its attributed string's fragments. Without the root, one
 * child is answered as itself and several as an array, which is the shape `getFantomRenderedOutput` parses.
 */
std::string renderFantomOutput(const facebook::react::StubViewTree& tree, bool includeRoot, bool includeLayoutMetrics);

} // namespace react_native_linux
