#include "WaylandWindow.h"

#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <wayland-client.h>

namespace react_native_linux {

namespace {

constexpr uint32_t kMaximumCompositorVersion = 4;
constexpr uint32_t kMaximumWmBaseVersion = 5;

} // namespace

const wl_registry_listener WaylandWindow::kRegistryListener{
    .global = WaylandWindow::handleRegistryGlobal,
    .global_remove = WaylandWindow::handleRegistryGlobalRemove,
};

const xdg_wm_base_listener WaylandWindow::kWmBaseListener{
    .ping = WaylandWindow::handleWmBasePing,
};

const xdg_surface_listener WaylandWindow::kXdgSurfaceListener{
    .configure = WaylandWindow::handleSurfaceConfigure,
};

const xdg_toplevel_listener WaylandWindow::kToplevelListener{
    .configure = WaylandWindow::handleToplevelConfigure,
    .close = WaylandWindow::handleToplevelClose,
    .configure_bounds = WaylandWindow::handleToplevelConfigureBounds,
    .wm_capabilities = WaylandWindow::handleToplevelWmCapabilities,
};

const wl_callback_listener WaylandWindow::kFrameCallbackListener{
    .done = WaylandWindow::handleFrameDone,
};

WaylandWindow::WaylandWindow(const std::string& title, WindowSize initialSize) : size_(initialSize) {
    display_ = wl_display_connect(nullptr);

    if (display_ == nullptr) {
        throw std::runtime_error("wl_display_connect failed; is WAYLAND_DISPLAY set?");
    }

    wl_registry* registry = wl_display_get_registry(display_);
    wl_registry_add_listener(registry, &kRegistryListener, this);
    wl_display_roundtrip(display_);
    wl_registry_destroy(registry);

    if (compositor_ == nullptr) {
        throw std::runtime_error("compositor does not advertise wl_compositor");
    }

    if (wmBase_ == nullptr) {
        throw std::runtime_error("compositor does not advertise xdg_wm_base");
    }

    xdg_wm_base_add_listener(wmBase_, &kWmBaseListener, this);

    surface_ = wl_compositor_create_surface(compositor_);
    xdgSurface_ = xdg_wm_base_get_xdg_surface(wmBase_, surface_);
    xdg_surface_add_listener(xdgSurface_, &kXdgSurfaceListener, this);

    toplevel_ = xdg_surface_get_toplevel(xdgSurface_);
    xdg_toplevel_add_listener(toplevel_, &kToplevelListener, this);
    xdg_toplevel_set_title(toplevel_, title.c_str());
    xdg_toplevel_set_app_id(toplevel_, title.c_str());

    wl_surface_commit(surface_);

    // The first buffer may only be attached after the initial xdg_surface.configure has been acknowledged, so the
    // swapchain cannot be created until this roundtrip completes.
    while (!configured_ && wl_display_roundtrip(display_) != -1) {
    }

    if (!configured_) {
        throw std::runtime_error("compositor never sent the initial xdg_surface configure");
    }
}

WaylandWindow::~WaylandWindow() noexcept {
    destroyFrameCallback();

    if (toplevel_ != nullptr) {
        xdg_toplevel_destroy(toplevel_);
    }

    if (xdgSurface_ != nullptr) {
        xdg_surface_destroy(xdgSurface_);
    }

    if (surface_ != nullptr) {
        wl_surface_destroy(surface_);
    }

    if (wmBase_ != nullptr) {
        xdg_wm_base_destroy(wmBase_);
    }

    if (compositor_ != nullptr) {
        wl_compositor_destroy(compositor_);
    }

    if (display_ != nullptr) {
        wl_display_disconnect(display_);
    }
}

wl_display* WaylandWindow::display() const noexcept { return display_; }

wl_surface* WaylandWindow::surface() const noexcept { return surface_; }

WindowSize WaylandWindow::size() const noexcept { return size_; }

bool WaylandWindow::isClosed() const noexcept { return closed_; }

bool WaylandWindow::takePendingResize() noexcept {
    const bool resized = pendingResize_;
    pendingResize_ = false;

    return resized;
}

void WaylandWindow::requestFrameCallback() {
    destroyFrameCallback();

    frameCallbackFired_ = false;
    frameCallback_ = wl_surface_frame(surface_);
    wl_callback_add_listener(frameCallback_, &kFrameCallbackListener, this);
}

bool WaylandWindow::waitForRedraw(std::chrono::milliseconds fallbackTimeout) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + fallbackTimeout;

    while (!closed_ && !frameCallbackFired_) {
        const std::chrono::milliseconds remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());

        if (remaining <= std::chrono::milliseconds::zero()) {
            break;
        }

        dispatchWithTimeout(remaining);
    }

    return !closed_;
}

void WaylandWindow::bindGlobal(wl_registry* registry, uint32_t name, const char* interfaceName, uint32_t version) {
    if (std::strcmp(interfaceName, wl_compositor_interface.name) == 0) {
        void* bound =
            wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, kMaximumCompositorVersion));
        compositor_ = static_cast<wl_compositor*>(bound);
    } else if (std::strcmp(interfaceName, xdg_wm_base_interface.name) == 0) {
        void* bound =
            wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, kMaximumWmBaseVersion));
        wmBase_ = static_cast<xdg_wm_base*>(bound);
    }
}

void WaylandWindow::dispatchWithTimeout(std::chrono::milliseconds timeout) {
    while (wl_display_prepare_read(display_) != 0) {
        if (wl_display_dispatch_pending(display_) < 0) {
            closed_ = true;

            return;
        }
    }

    if (wl_display_flush(display_) < 0 && errno != EAGAIN) {
        wl_display_cancel_read(display_);
        closed_ = true;

        return;
    }

    pollfd pollDescriptor{.fd = wl_display_get_fd(display_), .events = POLLIN, .revents = 0};
    const int readyDescriptors = poll(&pollDescriptor, 1, static_cast<int>(timeout.count()));

    if (readyDescriptors > 0 && (pollDescriptor.revents & POLLIN) != 0) {
        if (wl_display_read_events(display_) < 0) {
            closed_ = true;

            return;
        }
    } else {
        wl_display_cancel_read(display_);
    }

    if (wl_display_dispatch_pending(display_) < 0) {
        closed_ = true;
    }
}

void WaylandWindow::onToplevelConfigure(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    const WindowSize configuredSize{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};

    if (configuredSize.width != size_.width || configuredSize.height != size_.height) {
        size_ = configuredSize;
        pendingResize_ = true;
    }
}

void WaylandWindow::destroyFrameCallback() noexcept {
    if (frameCallback_ != nullptr) {
        wl_callback_destroy(frameCallback_);
        frameCallback_ = nullptr;
    }
}

void WaylandWindow::handleRegistryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interfaceName,
                                         uint32_t version) {
    static_cast<WaylandWindow*>(data)->bindGlobal(registry, name, interfaceName, version);
}

void WaylandWindow::handleRegistryGlobalRemove(void* /*data*/, wl_registry* /*registry*/, uint32_t /*name*/) {}

void WaylandWindow::handleWmBasePing(void* /*data*/, xdg_wm_base* wmBase, uint32_t serial) {
    xdg_wm_base_pong(wmBase, serial);
}

void WaylandWindow::handleSurfaceConfigure(void* data, xdg_surface* xdgSurface, uint32_t serial) {
    xdg_surface_ack_configure(xdgSurface, serial);
    static_cast<WaylandWindow*>(data)->configured_ = true;
}

void WaylandWindow::handleToplevelConfigure(void* data, xdg_toplevel* /*toplevel*/, int32_t width, int32_t height,
                                            wl_array* /*states*/) {
    static_cast<WaylandWindow*>(data)->onToplevelConfigure(width, height);
}

void WaylandWindow::handleToplevelClose(void* data, xdg_toplevel* /*toplevel*/) {
    static_cast<WaylandWindow*>(data)->closed_ = true;
}

void WaylandWindow::handleToplevelConfigureBounds(void* /*data*/, xdg_toplevel* /*toplevel*/, int32_t /*width*/,
                                                  int32_t /*height*/) {}

void WaylandWindow::handleToplevelWmCapabilities(void* /*data*/, xdg_toplevel* /*toplevel*/,
                                                 wl_array* /*capabilities*/) {}

void WaylandWindow::handleFrameDone(void* data, wl_callback* callback, uint32_t /*time*/) {
    WaylandWindow* window = static_cast<WaylandWindow*>(data);

    wl_callback_destroy(callback);
    window->frameCallback_ = nullptr;
    window->frameCallbackFired_ = true;
}

} // namespace react_native_linux
