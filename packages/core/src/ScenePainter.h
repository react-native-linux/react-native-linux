#pragma once

#include "RetainedScene.h"

#include "include/core/SkColor.h"

class SkCanvas;

namespace react_native_linux {

constexpr SkColor kSceneBackgroundColor = SkColorSetRGB(0x14, 0x16, 0x1A);

/**
 * The focus ring's colour and width. A desktop focus ring is the platform's accent colour rather than the app's,
 * and this platform has no theme service to ask yet — following the compositor's accent is a portal round trip
 * and a deferral, named in *Focus and keyboard* in docs/cpp-toolchain.md.
 */
constexpr SkColor kFocusRingColor = SkColorSetRGB(0x59, 0x9E, 0xFF);
constexpr float kFocusRingWidth = 2.0F;

/**
 * Draws one scene snapshot onto a canvas: the background clear, then per painted node its `overflow: hidden`
 * ancestor clips, its absolute transform, its rounded background fill, its per-side borders, and — for a
 * `<Paragraph>` — the SkParagraph built from the attributed string the node carries.
 *
 * `damage` restricts all of that to the region that changed. An empty list means no restriction, which is the
 * full repaint the golden rig and the first frame of any surface want. A non-empty list is clipped to the union
 * of its rectangles, rounded out to whole pixels and outset by one so no anti-aliased edge falls outside the
 * clip; the background clear is `SkBlendMode::kSrc` and respects that clip, so a damaged region is replaced
 * rather than blended and pixels outside it are left exactly as the previous frame drew them.
 *
 * Every number the snapshot carries is already absolute and already composed — opacity is multiplied into the
 * colours, radii are clamped, transforms are reduced to 2D affines — so nothing here computes scene state. What
 * remains is Skia geometry: the `SkRRect` built from the one `SceneRoundedBox` the scene hands over, `drawDRRect`
 * for the border ring, one overlapping mitred wedge clip per side when the four side colours are not identical,
 * and the paragraph build, which is Skia's own text layout rather than anything this project computes. Nothing
 * here derives a second rounded rect; issue #99 is that rule, and `RetainedScene::findNodeAtPoint` is the
 * consumer that proves it, because it answers with the same box and links no Skia at all.
 *
 * There is exactly one implementation of this because there is exactly one picture. The window draws it into a
 * swapchain-backed `SkSurface` every frame and the golden-image rig draws it into an offscreen raster surface once;
 * if the two ever diverged, a golden would stop describing what the window shows.
 *
 * Threading contract: the canvas and the snapshot both belong to the calling thread. The snapshot is already a
 * copy taken under the mounting manager's mutex, so nothing here touches the retained scene.
 */
void paintScene(SkCanvas& canvas, const SceneSnapshot& scene, const SceneDamage& damage);

} // namespace react_native_linux
