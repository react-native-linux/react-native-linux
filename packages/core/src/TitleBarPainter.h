#pragma once

#include "WindowDecorations.h"

#include <string>

class SkCanvas;

namespace react_native_linux {

/**
 * Draws the client-side title bar `WindowDecorations` laid out, above the surface root and into the same
 * swapchain-backed `SkSurface` the scene is painted into.
 *
 * It is drawn rather than composited from a subsurface for the reason the scene itself is: one picture, one
 * present, one screenshot. A golden of a client-decorated window therefore contains the bar, and the damage the
 * caller merges for it is the same `SceneDamage` a mutation produces — nothing here knows about either, because
 * this function only puts marks on a canvas.
 *
 * `isActive` is `xdg_toplevel.configure`'s `activated` bit, and it is the only visual state: zed#14202 is a
 * client-decorated window with no way to tell whether it has focus, and the whole of the fix is that the bar and
 * its text are dimmer when the bit is clear.
 *
 * Threading contract: the canvas belongs to the calling thread, which is the frame thread.
 */
void paintTitleBar(SkCanvas& canvas, const TitleBarLayout& layout, const std::string& title, bool isActive);

} // namespace react_native_linux
