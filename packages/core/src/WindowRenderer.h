#pragma once

#include "RetainedScene.h"
#include "WaylandWindow.h"

#include <functional>
#include <string>

class SkCanvas;

namespace react_native_linux {

/**
 * What the window's frame loop needs of a renderer, and nothing else: the four calls that are the same question
 * on every rung of the ladder in `RendererLadder.h`.
 *
 * This exists because the ladder's bottom rung is a different renderer rather than a different device —
 * `SharedMemoryRasterRenderer` has no `VkInstance` to configure — so the loop holds whichever one came up. Every
 * capability that is Vulkan's alone, including the `--window-debug` fault injections, stays on
 * `SkiaVulkanRenderer` and is reached through the pointer the loop keeps beside this one.
 *
 * Threading contract: as `SkiaVulkanRenderer`'s — every member runs on the thread that owns the run loop and the
 * Wayland connection, and nothing here is safe to call concurrently.
 */
class WindowRenderer {
public:
    WindowRenderer() = default;
    WindowRenderer(const WindowRenderer&) = delete;
    WindowRenderer(WindowRenderer&&) = delete;
    WindowRenderer& operator=(const WindowRenderer&) = delete;
    WindowRenderer& operator=(WindowRenderer&&) = delete;
    virtual ~WindowRenderer() = default;

    virtual void resize(WindowSize size) = 0;
    virtual void captureNextFrame(std::string outputPath) = 0;
    [[nodiscard]] virtual bool hasPendingCapture() const noexcept = 0;
    virtual bool drawFrame(WaylandWindow& window, const SceneDamage& frameDamage,
                           const std::function<void(SkCanvas&, WindowSize, const SceneDamage&)>& paint) = 0;
};

} // namespace react_native_linux
