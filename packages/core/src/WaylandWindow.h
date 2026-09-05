#pragma once

#include "FrameTiming.h"
#include "InputPipeline.h"
#include "ToplevelState.h"
#include "WaylandSeat.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct wl_array;
struct wl_callback;
struct wl_callback_listener;
struct wl_compositor;
struct wl_display;
struct wl_output;
struct wl_registry;
struct wl_registry_listener;
struct wl_seat;
struct wl_surface;
struct wl_surface_listener;
struct wp_presentation;
struct wp_presentation_feedback;
struct wp_presentation_feedback_listener;
struct wp_presentation_listener;
struct xdg_surface;
struct xdg_surface_listener;
struct xdg_toplevel;
struct xdg_toplevel_listener;
struct xdg_wm_base;
struct xdg_wm_base_listener;
struct zwp_text_input_manager_v3;

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
 * `wp_presentation` is the third mechanism of that decision and the only one that measures: one
 * `wp_presentation_feedback` object per committed frame, reporting when that content update actually turned into
 * light. It is bound when the compositor advertises it and skipped when it does not, so the window runs either
 * way and `FrameTiming` simply stays empty. See *Frame timing* in docs/cpp-toolchain.md.
 *
 * Input arrives on the same connection and therefore on the same thread. `WaylandSeat` fills a queue from inside
 * the dispatch this class performs, and `takeInputEvents` empties it once per frame; the window itself makes no
 * decision about what an event means. A compositor that advertises no `wl_seat` leaves that queue permanently
 * empty rather than failing construction, because a window without input is still a window.
 *
 * `zwp_text_input_manager_v3` is bound on the same terms: with it, the seat gets a `TextInputClient` and
 * composition events join that queue; without it, `textInput` is null and typing is whatever the keyboard sends.
 * Text composition is per-seat, so the text input is created once the seat exists, not per surface.
 *
 * The desktop lifecycle contract (#218) is four more seams beyond resize, each decoded and coalesced the same
 * way `DimensionsSource` coalesces a burst of configures into one change: `toplevelState`/`takeStateChange` for
 * `xdg_toplevel.configure`'s activated/maximized/fullscreen/resizing bits, decoded by the pure, unit-tested
 * `decodeToplevelStates`; `outputEnterCount`/`outputLeaveCount` for `wl_surface.enter`/`.leave`; and
 * `hasKeyboardFocus`, forwarded from `WaylandSeat`, for `wl_keyboard.enter`/`.leave`. None of the three feeds a
 * JS-visible event yet — no `AppState`-equivalent module exists on this platform to carry activation there, and
 * inventing one is out of this issue's scope — so today they are observable only at this seam and in a caller
 * that polls it, exactly as `takePendingResize` is. See *Window host* in docs/cpp-toolchain.md.
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

    /** The activated/maximized/fullscreen/resizing bits from the most recent `xdg_toplevel.configure`. */
    ToplevelState toplevelState() const noexcept;
    /** Whether `toplevelState` changed since the last call. Coalesces a burst of configures into one change. */
    bool takeStateChange() noexcept;

    /** How many `wl_surface.enter`/`.leave` events this surface has received, since construction. */
    uint32_t outputEnterCount() const noexcept;
    uint32_t outputLeaveCount() const noexcept;

    /** `wl_keyboard.enter` most recently reached this surface and no `.leave` has followed it yet. */
    bool hasKeyboardFocus() const noexcept;

    void requestFrameCallback();

    /**
     * Asks for `wp_presentation_feedback` on the content update the next `wl_surface.commit` carries, which for
     * this window is the commit `vkQueuePresentKHR` performs. A compositor that advertises no `wp_presentation`
     * makes this a no-op and leaves `isPresentationSupported` false; it is never an error, because measurement is
     * not what keeps the window running. See *Frame timing* in docs/cpp-toolchain.md.
     */
    void requestPresentationFeedback();
    bool isPresentationSupported() const noexcept;
    /** The presented frames recorded since the last call, in the order the compositor reported them. */
    std::vector<FrameTiming::Frame> takePresentedFrames();
    FrameTiming::Summary frameTimingSummary() const;

    bool waitForRedraw(std::chrono::milliseconds fallbackTimeout);

    /**
     * Whether the compositor has sent `wl_callback.done` for the frame callback currently or most recently
     * requested. `requestFrameCallback` — called from inside `SkiaVulkanRenderer::drawFrame` — is what resets this,
     * so a caller that skips drawing on a fallback timeout leaves it exactly as `waitForRedraw` last left it: a
     * caller can use it right after `waitForRedraw` returns to tell a callback-driven wake from a timeout one. See
     * *Frame clock* in docs/cpp-toolchain.md.
     */
    bool hasFrameCallbackFired() const noexcept;
    std::vector<InputEvent> takeInputEvents();
    TextInputClient* textInput() const noexcept;

private:
    void bindGlobal(wl_registry* registry, uint32_t name, const char* interfaceName, uint32_t version);
    void dispatchWithTimeout(std::chrono::milliseconds timeout);
    void onToplevelConfigure(int32_t width, int32_t height, const wl_array* states);
    void destroyFrameCallback() noexcept;

    static void handleRegistryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interfaceName,
                                     uint32_t version);
    static void handleRegistryGlobalRemove(void* data, wl_registry* registry, uint32_t name);
    static void handleWmBasePing(void* data, xdg_wm_base* wmBase, uint32_t serial);
    static void handleSurfaceConfigure(void* data, xdg_surface* xdgSurface, uint32_t serial);
    static void handleSurfaceEnter(void* data, wl_surface* surface, wl_output* output);
    static void handleSurfaceLeave(void* data, wl_surface* surface, wl_output* output);
    static void handleToplevelConfigure(void* data, xdg_toplevel* toplevel, int32_t width, int32_t height,
                                        wl_array* states);
    static void handleToplevelClose(void* data, xdg_toplevel* toplevel);
    static void handleToplevelConfigureBounds(void* data, xdg_toplevel* toplevel, int32_t width, int32_t height);
    static void handleToplevelWmCapabilities(void* data, xdg_toplevel* toplevel, wl_array* capabilities);
    static void handleFrameDone(void* data, wl_callback* callback, uint32_t time);
    // presentation-time's generated header declares a *function* named wp_presentation_feedback, which hides the
    // struct of the same name in C++, so the type needs its elaborated spelling everywhere it is named.
    static void handlePresentationClockId(void* data, wp_presentation* presentation, uint32_t clockId);
    static void handleFeedbackSyncOutput(void* data, struct wp_presentation_feedback* feedback, wl_output* output);
    static void handleFeedbackPresented(void* data, struct wp_presentation_feedback* feedback, uint32_t secondsHigh,
                                        uint32_t secondsLow, uint32_t nanoseconds, uint32_t refresh,
                                        uint32_t sequenceHigh, uint32_t sequenceLow, uint32_t flags);
    static void handleFeedbackDiscarded(void* data, struct wp_presentation_feedback* feedback);

    static const wl_registry_listener kRegistryListener;
    static const wl_surface_listener kSurfaceListener;
    static const xdg_wm_base_listener kWmBaseListener;
    static const xdg_surface_listener kXdgSurfaceListener;
    static const xdg_toplevel_listener kToplevelListener;
    static const wl_callback_listener kFrameCallbackListener;
    static const wp_presentation_listener kPresentationListener;
    static const wp_presentation_feedback_listener kPresentationFeedbackListener;

    wl_display* display_{nullptr};
    wl_compositor* compositor_{nullptr};
    std::unique_ptr<WaylandSeat> seat_;
    zwp_text_input_manager_v3* textInputManager_{nullptr};
    wp_presentation* presentation_{nullptr};
    xdg_wm_base* wmBase_{nullptr};
    wl_surface* surface_{nullptr};
    xdg_surface* xdgSurface_{nullptr};
    xdg_toplevel* toplevel_{nullptr};
    wl_callback* frameCallback_{nullptr};
    FrameTiming frameTiming_;
    std::vector<FrameTiming::Frame> presentedFrames_;
    WindowSize size_;
    ToplevelState toplevelState_;
    bool configured_{false};
    bool frameCallbackFired_{false};
    bool pendingResize_{false};
    bool pendingStateChange_{false};
    bool closed_{false};
    uint32_t outputEnterCount_{0};
    uint32_t outputLeaveCount_{0};
};

} // namespace react_native_linux
