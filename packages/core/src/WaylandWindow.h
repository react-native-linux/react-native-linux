#pragma once

#include "FrameTiming.h"
#include "InputPipeline.h"
#include "ToplevelState.h"
#include "WaylandDispatchDiagnostics.h"
#include "WaylandSeat.h"
#include "WaylandSerialLedger.h"
#include "WindowDecorations.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
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
struct wl_shm;
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
struct xdg_activation_v1;
struct xdg_activation_token_v1;
struct xdg_wm_base;
struct xdg_wm_base_listener;
struct zwp_text_input_manager_v3;
struct zxdg_decoration_manager_v1;
struct zxdg_toplevel_decoration_v1;
struct zxdg_toplevel_decoration_v1_listener;

namespace react_native_linux {

struct WindowSize {
    uint32_t width;
    uint32_t height;
};

/**
 * What the desktop matches this window on, and who it is told to decorate it.
 *
 * `applicationIdentifier` is `xdg_toplevel.set_app_id`, and it has to equal the installed desktop file's name
 * without its `.desktop` suffix or none of the matching works: zed#53962 is KWin window rules silently not
 * applying, and zed#33897 is the window missing from the GNOME switcher, both from the same one request being
 * wrong. It is a separate string from the title for exactly that reason — the title is what a human reads and
 * changes per document, the identifier is what the compositor keys on and never changes.
 *
 * `forceClientDecorations` is the test seam: it makes `decorationMode` answer `Client` whatever the compositor
 * says, so the drawn title bar can be proven under a compositor that does implement
 * `zxdg_decoration_manager_v1`. `noDecorations` is its mirror: it makes `decorationMode` answer `Bare` — no
 * request, no drawn bar, zero inset — so the test rigs' scripted coordinates stay content-relative under
 * compositors that offer no `zxdg_decoration_manager_v1` at all. `forceClientDecorations` wins when both are set.
 * See *Decorations and app_id (#329)* in docs/cpp-toolchain.md.
 */
struct WindowIdentity {
    std::string title;
    std::string applicationIdentifier;
    bool forceClientDecorations{false};
    bool noDecorations{false};
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
 * way and `FrameTiming` simply stays empty. See *Frame timing* in docs/cpp-toolchain.md. The first `presented`
 * feedback is also issue #373's readiness signal, exposed as `hasPresentedFirstFrame`; see that method.
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
    WaylandWindow(const WindowIdentity& identity, WindowSize initialSize);
    WaylandWindow(const WaylandWindow&) = delete;
    WaylandWindow(WaylandWindow&&) = delete;
    WaylandWindow& operator=(const WaylandWindow&) = delete;
    WaylandWindow& operator=(WaylandWindow&&) = delete;
    ~WaylandWindow() noexcept;

    wl_display* display() const noexcept;
    wl_surface* surface() const noexcept;

    /**
     * The `wl_shm` the compositor advertises, which the ladder's raster rung attaches its buffers through. Null
     * on a compositor that advertises none, which the Wayland core protocol requires it to and no compositor in
     * practice omits; the rung reports that as its own bring-up failure rather than this constructor doing so.
     */
    wl_shm* sharedMemory() const noexcept;
    WindowSize size() const noexcept;
    bool isClosed() const noexcept;
    bool takePendingResize() noexcept;

    /**
     * Whether an `xdg_surface.configure` has been acknowledged, which is the condition xdg-shell puts on
     * attaching the first buffer. Construction blocks until it holds, so it is true for the whole life of a
     * window that finished constructing; `SurfaceCommitGate` reads it anyway, because a rule that is only ever
     * satisfied is still the rule, and `--window-debug` forces it false to prove the gate holds the buffer back.
     */
    bool isConfigureAcknowledged() const noexcept;

    /**
     * Whether the compositor `discarded` the most recent content update it reported on, cleared by the read. A
     * discarded update never turned into light and is owed no frame callback, so this is what tells the renderer
     * to present again rather than wait. A compositor that advertises no `wp_presentation` reports no discards
     * either, so this stays false there. See *Surface commit ordering* in docs/cpp-toolchain.md.
     */
    bool takeContentUpdateDiscarded() noexcept;

    /**
     * The same fact without consuming it, for the run loop: a discard is a reason to draw a frame the frame clock
     * would otherwise skip, and the frame that draws is the one that consumes it.
     */
    bool hasContentUpdateDiscarded() const noexcept;

    /**
     * Who draws this window's chrome, as `decideDecorationMode` decides it from the manager's presence, the
     * `--force-client-decorations` and `--no-decorations` flags, and the compositor's most recent
     * `zxdg_toplevel_decoration_v1.configure`. Answered live rather than cached, because a compositor may
     * reconfigure the mode at any time.
     */
    DecorationMode decorationMode() const noexcept;
    const std::string& title() const noexcept;

    /**
     * The four requests the drawn title bar makes of the compositor. `move` and `resize` take their serial from
     * the ledger's `InteractiveMove` kind (#330) and are simply not sent when nothing has recorded a qualifying
     * press yet, because a request built from a zero serial is declined exactly as silently as one built from a
     * stale one. `edge` is an `xdg_toplevel::resize_edge` wire value, which is what `resizeEdgeOfHit` produces.
     */
    void startInteractiveMove();
    void startInteractiveResize(uint32_t edge);
    /** `xdg_toplevel::show_window_menu`, at the surface point the secondary press that asked for it landed on. */
    void showWindowMenu(int32_t x, int32_t y);
    void toggleMaximized();
    void minimize();
    /** What the drawn close button does: the same flag `xdg_toplevel.close` sets, so teardown takes one path. */
    void requestClose() noexcept;

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

    /**
     * One `wp_presentation_feedback` outcome, in the order the compositor delivered it: the presented frame, or
     * `std::nullopt` for a `discarded` content update. The frame journal (#345) needs the order, not just the
     * counts — a presented frame followed by a discarded one is a closed interval and then a discontinuity, and
     * charging the discontinuity first would throw away the presentation's latency and its hang.
     */
    using PresentationEvent = std::optional<FrameTiming::Frame>;

    /** The feedback events recorded since the last call, in the order the compositor reported them. */
    std::vector<PresentationEvent> takePresentationEvents();
    FrameTiming::Summary frameTimingSummary() const;

    /**
     * The clock `wp_presentation.clock_id` named for its timestamps, absent until that event arrives (and on a
     * compositor advertising no `wp_presentation` at all). `WindowMain` converts presentation timestamps into
     * `steady_clock`'s domain with it; see `presentationClockOffsetNanoseconds` in FrameJournal.h.
     */
    std::optional<uint32_t> presentationClockId() const noexcept;

    /**
     * The readiness signal of issue #373: true from the first `wp_presentation_feedback.presented` this window has
     * ever received, or, when the compositor advertises no `wp_presentation`, from the first `wl_surface.frame`
     * callback. Both mechanisms only ever fire for a commit that already carried an attached buffer — every
     * `requestFrameCallback`/`requestPresentationFeedback` pair is armed immediately before the present that
     * performs that attach — so this can only turn true after a real frame reached the compositor. It never turns
     * false again: a discarded or failed presentation simply leaves it as it was, because #373 asks for "the
     * window is up", not "the window is up right now".
     */
    bool hasPresentedFirstFrame() const noexcept;

    /**
     * Completes the startup activation of #336: the launcher put `XDG_ACTIVATION_TOKEN` in this process's
     * environment, and handing it back — `xdg_activation_token_v1.set_token` + `.activate` on this window's
     * surface — is what tells the compositor the application it was waiting for has shown its window. A no-op
     * when the compositor advertises no `xdg_activation_v1`: there is no notification to complete there.
     */
    void completeStartupActivation(const std::string& token);

    /**
     * Requests an activation token for the outbound direction of #336 — `Linking.openURL` handing one to the
     * handler it spawns so the browser it opens takes focus rather than opening behind this window. The request
     * carries the ledger's latest press serial and this surface; the compositor answers on the next dispatch
     * round trip, and the token is retrieved with `takeActivationToken`.
     */
    void requestActivationToken();

    /** The token the last `requestActivationToken` produced, exactly once; `std::nullopt` until the `done` event. */
    std::optional<std::string> takeActivationToken();

    bool waitForRedraw(std::chrono::milliseconds fallbackTimeout);

    /**
     * Drains whatever the compositor has already sent on the display socket, without blocking, and reports it as
     * the structured protocol/display line if it is one. A fatal failure outside this class — an unrecoverable
     * `VkResult` from the swapchain path, for instance — can be the *symptom* of a Wayland protocol error the
     * process has not read off the socket yet, since libwayland only updates `wl_display_get_error` once it has
     * dispatched the event carrying it: without this, the process reports the symptom (a bare `VkResult` name)
     * and never the structured line #331 exists to produce. The caller (`WindowMain`'s top-level catch) calls
     * this before its own fatal report so the Wayland cause, if any, is on the trace before the symptom is.
     * Returns whether it reported one.
     */
    bool reportPendingDisplayError();

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
    /** The serial ledger every request that needs one — set_selection, an interactive move, ack_configure — reads
     * from. See #330 and *The serial ledger* in docs/cpp-toolchain.md. */
    const WaylandSerialLedger& serialLedger() const noexcept;

    /**
     * `--inject-protocol-error`/`--inject-protocol-error-after-frame`'s hook (#331): acknowledges the initial
     * `xdg_surface.configure` a second time with a serial no `configure` ever sent, which xdg-shell requires the
     * compositor to reject. The object named in the rejection is compositor-specific, not fixed by the spec: the
     * wlroots-based compositor CI runs under posts it against `xdg_wm_base`, code 4, rather than against
     * `xdg_surface` itself. It exists to prove, under a real compositor, that the next dispatch reports that
     * rejection through `reportNativeError` instead of a bare "broken pipe". See *Window host* in
     * docs/cpp-toolchain.md.
     */
    void injectInvalidAckConfigureForTesting();

private:
    void bindGlobal(wl_registry* registry, uint32_t name, const char* interfaceName, uint32_t version);
    void dispatchWithTimeout(std::chrono::milliseconds timeout);
    /**
     * Classifies one dispatch/flush/read-events return value through `classifyWaylandDispatchResult`
     * (`WaylandDispatchDiagnostics.h`), passing both `wl_display_get_error` and the plain `errno` the call itself
     * left behind, and reports a protocol or display error through `reportNativeError` when the outcome is one.
     * Returns the outcome so the caller can retry on `Retry` instead of just knowing not to close on it. See
     * *Window host* in docs/cpp-toolchain.md.
     */
    WaylandDispatchOutcome reportDispatchFailure(int result);
    void onToplevelConfigure(int32_t width, int32_t height, const wl_array* states);
    void negotiateDecorations();
    void destroyFrameCallback() noexcept;

    static void handleRegistryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interfaceName,
                                     uint32_t version);
    static void handleRegistryGlobalRemove(void* data, wl_registry* registry, uint32_t name);
    static void handleWmBasePing(void* data, xdg_wm_base* wmBase, uint32_t serial);
    static void handleActivationTokenDone(void* data, xdg_activation_token_v1* token, const char* tokenString);
    static void handleSurfaceConfigure(void* data, xdg_surface* xdgSurface, uint32_t serial);
    static void handleSurfaceEnter(void* data, wl_surface* surface, wl_output* output);
    static void handleSurfaceLeave(void* data, wl_surface* surface, wl_output* output);
    static void handleToplevelConfigure(void* data, xdg_toplevel* toplevel, int32_t width, int32_t height,
                                        wl_array* states);
    static void handleToplevelClose(void* data, xdg_toplevel* toplevel);
    static void handleToplevelConfigureBounds(void* data, xdg_toplevel* toplevel, int32_t width, int32_t height);
    static void handleToplevelWmCapabilities(void* data, xdg_toplevel* toplevel, wl_array* capabilities);
    static void handleDecorationConfigure(void* data, zxdg_toplevel_decoration_v1* decoration, uint32_t mode);
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
    static const zxdg_toplevel_decoration_v1_listener kDecorationListener;
    static const wl_callback_listener kFrameCallbackListener;
    static const wp_presentation_listener kPresentationListener;
    static const wp_presentation_feedback_listener kPresentationFeedbackListener;

    wl_display* display_{nullptr};
    wl_compositor* compositor_{nullptr};
    WaylandSerialLedger serialLedger_;
    wl_shm* sharedMemory_{nullptr};
    std::unique_ptr<WaylandSeat> seat_;
    zwp_text_input_manager_v3* textInputManager_{nullptr};
    zxdg_decoration_manager_v1* decorationManager_{nullptr};
    xdg_activation_v1* activation_{nullptr};
    xdg_activation_token_v1* pendingActivationToken_{nullptr};
    std::optional<std::string> requestedActivationToken_;
    zxdg_toplevel_decoration_v1* toplevelDecoration_{nullptr};
    wp_presentation* presentation_{nullptr};
    xdg_wm_base* wmBase_{nullptr};
    wl_surface* surface_{nullptr};
    xdg_surface* xdgSurface_{nullptr};
    xdg_toplevel* toplevel_{nullptr};
    wl_callback* frameCallback_{nullptr};
    FrameTiming frameTiming_;
    std::vector<PresentationEvent> presentationEvents_;
    std::optional<uint32_t> presentationClockId_;
    WindowSize size_;
    std::string title_;
    bool forceClientDecorations_{false};
    bool noDecorations_{false};
    std::optional<uint32_t> configuredDecorationMode_;
    ToplevelState toplevelState_;
    bool configured_{false};
    bool frameCallbackFired_{false};
    bool presentedFirstFrame_{false};
    bool pendingResize_{false};
    bool contentUpdateDiscarded_{false};
    bool pendingStateChange_{false};
    bool closed_{false};
    uint32_t outputEnterCount_{0};
    uint32_t outputLeaveCount_{0};
};

} // namespace react_native_linux
