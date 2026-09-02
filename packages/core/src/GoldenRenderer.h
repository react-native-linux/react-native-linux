#pragma once

#include <react/renderer/graphics/Point.h>

#include <string>

namespace react_native_linux {

/**
 * Renders one bundle's final scene into an offscreen raster surface and writes it as a PNG.
 *
 * This is the golden-image rig's producer. It is deliberately CPU-only: `SkSurfaces::Raster` needs no GPU, no
 * Vulkan driver and no Wayland compositor, which is what lets a golden run on a machine where screen capture is
 * permission-gated and inside a CI container with nothing graphical installed. It is the same `paintScene` the
 * window calls, so the golden describes the window's picture rather than a second implementation of it.
 *
 * The full-window golden — lavapipe software Vulkan under a headless compositor, exercising the swapchain path of
 * `SkiaVulkanRenderer` — is a separate rig and is not this one.
 *
 * Returns a process exit status: 0 when the file was written and the bundle reported no fatal JavaScript error.
 */
int renderGolden(const std::string& bundlePath, const std::string& outputPath, int width, int height);

/**
 * The same rig, after a mouse wheel has been turned `wheelNotches` times over `surfacePoint`.
 *
 * A `<ScrollView>` renders nothing a static golden could not already prove until it has been scrolled: the
 * translation of its children, the clip at its edges and the deceleration curve that decided where they stopped
 * are all properties of a scroll position other than zero. This is how one gets into a checked-in PNG. See
 * *ScrollView* in docs/cpp-toolchain.md.
 */
int renderScrollGolden(const std::string& bundlePath, const std::string& outputPath,
                       facebook::react::Point surfacePoint, int wheelNotches, int width, int height);

/**
 * Runs a bundle that commits twice and proves that painting only the damaged region produces the same picture as
 * repainting everything.
 *
 * The first commit's scene is painted in full onto two surfaces. The second commit's scene is then painted over
 * one of them in full and over the other clipped to the damage the scene accumulated, which is exactly what the
 * window does per frame minus the swapchain. The two are compared byte by byte; a single differing pixel fails the
 * run with its coordinate, because a partial redraw that is nearly right is a partial redraw that is wrong. The
 * damage-clipped surface is what gets written, so the checked-in golden is the partial redraw rather than a full
 * one that happens to match it.
 *
 * Returns a process exit status: 0 when the two redraws matched, the file was written, and the bundle reported no
 * fatal JavaScript error.
 */
int renderDamageGolden(const std::string& bundlePath, const std::string& outputPath, int width, int height);

} // namespace react_native_linux
