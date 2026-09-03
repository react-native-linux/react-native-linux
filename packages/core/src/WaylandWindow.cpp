#include "WaylandWindow.h"

#include "presentation-time-client-protocol.h"
#include "text-input-unstable-v3-client-protocol.h"
#include "xdg-shell-client-protocol.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <poll.h>
#include <stdexcept>
#include <vector>
#include <wayland-client.h>

namespace react_native_linux {

namespace {

constexpr uint32_t kMaximumCompositorVersion = 4;
constexpr uint32_t kMaximumWmBaseVersion = 5;
// Version 1 is every event and request this platform uses. wayland-protocols 1.49 added version 2 — actions,
// language reporting, pre-edit style hints and the input-panel requests — and binding it would oblige us to
// answer events that no `<TextInput>` exists to render yet. See *IME* in docs/cpp-toolchain.md.
constexpr uint32_t kMaximumTextInputManagerVersion = 1;
// ADR-0001 decision 3 binds version 2: Hyprland zeroes the refresh hint for version 1 clients while VRR is
// active. A compositor that only advertises version 1 still binds, at its own version.
constexpr uint32_t kMaximumPresentationVersion = 2;
constexpr uint64_t kNanosecondsPerSecond = 1'000'000'000;
constexpr uint32_t kHighWordShift = 32;

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

const wp_presentation_listener WaylandWindow::kPresentationListener{
    .clock_id = WaylandWindow::handlePresentationClockId,
};

const wp_presentation_feedback_listener WaylandWindow::kPresentationFeedbackListener{
    .sync_output = WaylandWindow::handleFeedbackSyncOutput,
    .presented = WaylandWindow::handleFeedbackPresented,
    .discarded = WaylandWindow::handleFeedbackDiscarded,
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

    if (seat_ != nullptr && textInputManager_ != nullptr) {
        seat_->attachTextInput(textInputManager_);
    }
}

WaylandWindow::~WaylandWindow() noexcept {
    destroyFrameCallback();

    // Before the connection goes away: releasing the pointer, the keyboard and the seat are all requests on it.
    // The seat also owns the text input, which is destroyed before the manager that created it.
    seat_.reset();

    if (textInputManager_ != nullptr) {
        zwp_text_input_manager_v3_destroy(textInputManager_);
    }

    if (presentation_ != nullptr) {
        wp_presentation_destroy(presentation_);
    }

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

void WaylandWindow::requestPresentationFeedback() {
    if (presentation_ == nullptr) {
        return;
    }

    struct wp_presentation_feedback* feedback = wp_presentation_feedback(presentation_, surface_);
    wp_presentation_feedback_add_listener(feedback, &kPresentationFeedbackListener, this);
}

bool WaylandWindow::isPresentationSupported() const noexcept { return presentation_ != nullptr; }

std::vector<FrameTiming::Frame> WaylandWindow::takePresentedFrames() {
    std::vector<FrameTiming::Frame> taken;
    taken.swap(presentedFrames_);

    return taken;
}

FrameTiming::Summary WaylandWindow::frameTimingSummary() const { return frameTiming_.summarise(); }

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

bool WaylandWindow::hasFrameCallbackFired() const noexcept { return frameCallbackFired_; }

std::vector<InputEvent> WaylandWindow::takeInputEvents() {
    if (seat_ == nullptr) {
        return {};
    }

    return seat_->takeEvents();
}

TextInputClient* WaylandWindow::textInput() const noexcept {
    return seat_ == nullptr ? nullptr : seat_->textInput();
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
    } else if (std::strcmp(interfaceName, wl_seat_interface.name) == 0 && version >= kMinimumSeatVersion) {
        void* bound = wl_registry_bind(registry, name, &wl_seat_interface, kMinimumSeatVersion);
        seat_ = std::make_unique<WaylandSeat>(static_cast<wl_seat*>(bound));
    } else if (std::strcmp(interfaceName, zwp_text_input_manager_v3_interface.name) == 0) {
        void* bound = wl_registry_bind(registry, name, &zwp_text_input_manager_v3_interface,
                                       std::min(version, kMaximumTextInputManagerVersion));
        textInputManager_ = static_cast<zwp_text_input_manager_v3*>(bound);
    } else if (std::strcmp(interfaceName, wp_presentation_interface.name) == 0) {
        void* bound = wl_registry_bind(registry, name, &wp_presentation_interface,
                                       std::min(version, kMaximumPresentationVersion));
        presentation_ = static_cast<wp_presentation*>(bound);
        wp_presentation_add_listener(presentation_, &kPresentationListener, this);
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

// The presentation clock is whatever clock_id names — CLOCK_MONOTONIC on every compositor we run under. Only the
// differences between two presentation timestamps are ever used, so the domain does not have to be resolved.
void WaylandWindow::handlePresentationClockId(void* /*data*/, wp_presentation* /*presentation*/, uint32_t /*clock*/) {}

void WaylandWindow::handleFeedbackSyncOutput(void* /*data*/, struct wp_presentation_feedback* /*feedback*/,
                                             wl_output* /*output*/) {}

// `presented` and `discarded` are both destructor events: the compositor has already destroyed the server-side
// object, and the client-side proxy is this handler's to free.
void WaylandWindow::handleFeedbackPresented(void* data, struct wp_presentation_feedback* feedback, uint32_t secondsHigh,
                                            uint32_t secondsLow, uint32_t nanoseconds, uint32_t refresh,
                                            uint32_t sequenceHigh, uint32_t sequenceLow, uint32_t flags) {
    WaylandWindow* window = static_cast<WaylandWindow*>(data);
    const uint64_t seconds = (static_cast<uint64_t>(secondsHigh) << kHighWordShift) | secondsLow;
    const uint64_t presentedNanoseconds = (seconds * kNanosecondsPerSecond) + nanoseconds;
    const uint64_t sequence = (static_cast<uint64_t>(sequenceHigh) << kHighWordShift) | sequenceLow;

    window->presentedFrames_.push_back(
        window->frameTiming_.recordPresented(sequence, presentedNanoseconds, refresh, flags));
    wp_presentation_feedback_destroy(feedback);
}

void WaylandWindow::handleFeedbackDiscarded(void* data, struct wp_presentation_feedback* feedback) {
    static_cast<WaylandWindow*>(data)->frameTiming_.recordDiscarded();
    wp_presentation_feedback_destroy(feedback);
}

} // namespace react_native_linux
