#pragma once

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

} // namespace react_native_linux
