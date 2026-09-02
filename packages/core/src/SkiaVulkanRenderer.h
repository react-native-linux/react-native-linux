#pragma once

#include "RetainedScene.h"
#include "WaylandWindow.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/vk/VulkanExtensions.h"

#include <cstdint>
#include <functional>
#include <vector>
#include <vulkan/vulkan_core.h>

class SkCanvas;

namespace react_native_linux {

/**
 * The Ganesh Vulkan half of the window: one `VkInstance`, one `VkDevice`, one FIFO swapchain on a
 * `VK_KHR_wayland_surface`, and one `GrDirectContext` built on that device.
 *
 * Each swapchain image is wrapped directly as an `SkSurface` through `GrBackendRenderTargets::MakeVk`, so Skia
 * renders straight into the presented image and there is no offscreen surface and no blit. Skia owns every image
 * barrier: `GrDirectContext::flush` is given a `MutableTextureState` requesting `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR`,
 * which is the whole of the layout handling this class performs. The structure follows Skia's own
 * `tools/window/VulkanWindowContext.cpp`, including the extra backbuffer that lets a command buffer retire before
 * its semaphore is reused.
 *
 * Presentation is `vkQueuePresentKHR`, which is what attaches a buffer to the `wl_surface` and commits it. The
 * frame callback for the next frame must therefore be requested before `drawFrame` returns to the run loop, which
 * is why `drawFrame` takes the window rather than a bare surface handle.
 *
 * Partial redraw is where the swapchain stops being an implementation detail. `vkAcquireNextImageKHR` hands back
 * whichever image is free, so the pixels already in it are not last frame's — they are from the last frame that
 * used *that* image, several frames ago. This class therefore keeps one damage list per swapchain image, adds
 * every frame's damage to all of them, and hands the acquired image its own accumulated list, which is the
 * buffer-age approach without the extension: the region a given image has to repaint is everything that changed
 * since that image was last drawn. A new swapchain seeds every list with the full surface, so the first frame
 * after startup or a resize is a full repaint by construction. `VK_KHR_incremental_present` and
 * `wl_surface.damage_buffer` would additionally tell the compositor what changed; neither is bound here.
 *
 * Threading contract: every member runs on the thread that owns the process run loop, the same thread the Wayland
 * connection is dispatched on. Nothing here is safe to call concurrently.
 */
class SkiaVulkanRenderer final {
public:
    SkiaVulkanRenderer(wl_display* waylandDisplay, wl_surface* waylandSurface, WindowSize initialSize);
    SkiaVulkanRenderer(const SkiaVulkanRenderer&) = delete;
    SkiaVulkanRenderer(SkiaVulkanRenderer&&) = delete;
    SkiaVulkanRenderer& operator=(const SkiaVulkanRenderer&) = delete;
    SkiaVulkanRenderer& operator=(SkiaVulkanRenderer&&) = delete;
    ~SkiaVulkanRenderer() noexcept;

    void resize(WindowSize size);
    void drawFrame(WaylandWindow& window, const SceneDamage& frameDamage,
                   const std::function<void(SkCanvas&, WindowSize, const SceneDamage&)>& paint);

private:
    struct Backbuffer {
        VkSemaphore renderSemaphore{VK_NULL_HANDLE};
        uint32_t imageIndex{0};
    };

    void createInstance();
    void createWaylandSurface(wl_display* waylandDisplay, wl_surface* waylandSurface);
    void selectPhysicalDevice();
    void createDevice();
    void createDirectContext();
    void createSwapchain();
    void createBackbuffers(VkFormat imageFormat, VkImageUsageFlags imageUsage);
    void destroyBackbuffers() noexcept;

    VkInstance instance_{VK_NULL_HANDLE};
    VkSurfaceKHR vulkanSurface_{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice_{VK_NULL_HANDLE};
    VkDevice device_{VK_NULL_HANDLE};
    VkQueue queue_{VK_NULL_HANDLE};
    uint32_t queueFamilyIndex_{0};
    VkPhysicalDeviceFeatures deviceFeatures_{};
    skgpu::VulkanExtensions extensions_;
    sk_sp<GrDirectContext> directContext_;
    VkSwapchainKHR swapchain_{VK_NULL_HANDLE};
    WindowSize requestedSize_;
    WindowSize swapchainSize_;
    std::vector<sk_sp<SkSurface>> imageSurfaces_;
    std::vector<SceneDamage> imageDamage_;
    std::vector<Backbuffer> backbuffers_;
    size_t currentBackbufferIndex_{0};
};

} // namespace react_native_linux
