#pragma once

#include <chrono>
#include <cstdint>
#include <string>

struct wl_array;
struct wl_callback;
struct wl_callback_listener;
struct wl_compositor;
struct wl_display;
struct wl_registry;
struct wl_registry_listener;
struct wl_surface;
struct xdg_surface;
struct xdg_surface_listener;
struct xdg_toplevel;
struct xdg_toplevel_listener;
struct xdg_wm_base;
struct xdg_wm_base_listener;

namespace react_native_linux {

struct WindowSize {
    uint32_t width;
    uint32_t height;
};

/**
 * One xdg-shell toplevel window on one Wayland connection.
 *
 * The window owns no drawing state: `vkCreateWaylandSurfaceKHR` takes the `wl_display` and `wl_surface` handles
 * this class exposes, and `vkQueuePresentKHR` is what attaches a buffer and commits the surface. Nothing here ever
 * calls `wl_surface_attach`.
 *
 * Pacing follows ADR-0001 decision 3. `wl_surface.frame` callbacks throttle redraw, and because a compositor is
 * allowed to withhold them for a surface that is not visible — Hyprland sends them only for the active and active
 * special workspaces — `waitForRedraw` also returns when its fallback timeout expires. The fallback keeps the
 * connection dispatching and the close event reachable on an occluded window; it is not a frame source for a
 * visible one.
 *
 * Threading contract: every member runs on the thread that constructed the window, which is the thread that owns
 * the process run loop. The Wayland connection is never touched from another thread. The Vulkan WSI dispatches the
 * same connection on its own private event queue, which is why this class uses the prepare-read/read-events
 * protocol rather than `wl_display_dispatch`.
 */
class WaylandWindow final {
public:
    WaylandWindow(const std::string& title, WindowSize initialSize);
    WaylandWindow(const WaylandWindow&) = delete;
    WaylandWindow(WaylandWindow&&) = delete;
    WaylandWindow& operator=(const WaylandWindow&) = delete;
    WaylandWindow& operator=(WaylandWindow&&) = delete;
    ~WaylandWindow() noexcept;

    wl_display* display() const noexcept;
    wl_surface* surface() const noexcept;
    WindowSize size() const noexcept;
    bool isClosed() const noexcept;
    bool takePendingResize() noexcept;

    void requestFrameCallback();
    bool waitForRedraw(std::chrono::milliseconds fallbackTimeout);

private:
    void bindGlobal(wl_registry* registry, uint32_t name, const char* interfaceName, uint32_t version);
    void dispatchWithTimeout(std::chrono::milliseconds timeout);
    void onToplevelConfigure(int32_t width, int32_t height);
    void destroyFrameCallback() noexcept;

    static void handleRegistryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interfaceName,
                                     uint32_t version);
    static void handleRegistryGlobalRemove(void* data, wl_registry* registry, uint32_t name);
    static void handleWmBasePing(void* data, xdg_wm_base* wmBase, uint32_t serial);
    static void handleSurfaceConfigure(void* data, xdg_surface* xdgSurface, uint32_t serial);
    static void handleToplevelConfigure(void* data, xdg_toplevel* toplevel, int32_t width, int32_t height,
                                        wl_array* states);
    static void handleToplevelClose(void* data, xdg_toplevel* toplevel);
    static void handleToplevelConfigureBounds(void* data, xdg_toplevel* toplevel, int32_t width, int32_t height);
    static void handleToplevelWmCapabilities(void* data, xdg_toplevel* toplevel, wl_array* capabilities);
    static void handleFrameDone(void* data, wl_callback* callback, uint32_t time);

    static const wl_registry_listener kRegistryListener;
    static const xdg_wm_base_listener kWmBaseListener;
    static const xdg_surface_listener kXdgSurfaceListener;
    static const xdg_toplevel_listener kToplevelListener;
    static const wl_callback_listener kFrameCallbackListener;

    wl_display* display_{nullptr};
    wl_compositor* compositor_{nullptr};
    xdg_wm_base* wmBase_{nullptr};
    wl_surface* surface_{nullptr};
    xdg_surface* xdgSurface_{nullptr};
    xdg_toplevel* toplevel_{nullptr};
    wl_callback* frameCallback_{nullptr};
    WindowSize size_;
    bool configured_{false};
    bool frameCallbackFired_{false};
    bool pendingResize_{false};
    bool closed_{false};
};

} // namespace react_native_linux
