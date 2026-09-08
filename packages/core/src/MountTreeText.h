#pragma once

#include "RetainedScene.h"

#include <string>

namespace react_native_linux {

/**
 * The committed mount tree as one deterministic string, in the shape upstream's Fantom tester renders its
 * `StubViewTree` in (`private/react-native-fantom/tester/src/render/RenderOutput.cpp`): one element per mounted
 * node, named `rn-` plus the lower-cased component name, carrying the frame Yoga computed for it.
 *
 * It is a golden with no pixels: a test asserts the whole tree inline, so the diff on a failure names the node
 * and the number that moved rather than a rectangle of changed pixels, and it needs neither a compositor nor a
 * GPU to produce. Nodes that never form a view — `Text`, `RawText` — are not in the scene at all, so they are
 * not here either; that is Fabric's flattening, and asserting on it is the point.
 *
 * Roots are rendered in tag order so a scene carrying more than one surface renders the same way on every run,
 * because `SceneNodes` is a hash map and its iteration order is not one.
 */
std::string renderMountTree(const SceneNodes& nodes);

} // namespace react_native_linux
