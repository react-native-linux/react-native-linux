#pragma once

#include "RetainedScene.h"

#include "include/core/SkColor.h"

class SkCanvas;

namespace react_native_linux {

constexpr SkColor kSceneBackgroundColor = SkColorSetRGB(0x14, 0x16, 0x1A);

/**
 * Draws one scene snapshot onto a canvas: the background clear plus one filled rectangle per painted node.
 *
 * There is exactly one implementation of this because there is exactly one picture. The window draws it into a
 * swapchain-backed `SkSurface` every frame and the golden-image rig draws it into an offscreen raster surface once;
 * if the two ever diverged, a golden would stop describing what the window shows.
 *
 * Threading contract: the canvas and the snapshot both belong to the calling thread. The snapshot is already a
 * copy taken under the mounting manager's mutex, so nothing here touches the retained scene.
 */
void paintScene(SkCanvas& canvas, const SceneSnapshot& scene);

} // namespace react_native_linux
