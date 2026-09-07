#pragma once

#include <cstdint>
#include <optional>

namespace react_native_linux {

/**
 * Who draws the window's chrome, and the geometry of the chrome we draw when it is us.
 *
 * GNOME and Mutter implement no `zxdg_decoration_manager_v1` at all, so under the desktop most Linux users run
 * there is nobody to ask and nobody else to draw: a window with no client-side title bar has no title, no
 * buttons, no resize gutter and no focus indication. This file is the whole decision — which mode, how tall the
 * bar is, how wide the resize edges are, which rectangle a pointer landed in, and whether two presses were a
 * double click — and it is deliberately free of libwayland, of Skia and of every generated header, so it
 * compiles under `RNL_BUILD_TESTS` (which probes for neither) and sits in the 100% line-and-branch coverage
 * gate the same way `ToplevelState` and `WaylandSerialLedger` do. `WaylandWindow` is the seam that negotiates,
 * and `TitleBarPainter` is the one that draws; neither of them decides anything. See *Decorations and app_id
 * (#329)* in docs/cpp-toolchain.md.
 */
enum class DecorationMode : uint8_t {
    Server,
    Client,
    Bare,
};

/**
 * `zxdg_toplevel_decoration_v1::mode`'s own wire values, as `xdg-decoration-unstable-v1.xml` freezes them:
 * `client_side = 1`, `server_side = 2`. They are named here rather than taken from the generated enum for the
 * reason `ToplevelState`'s four are — the generated header exists only under `RNL_ENABLE_WINDOW`, and this
 * translation unit has to compile without it.
 */
constexpr uint32_t kDecorationModeClientSide = 1;
constexpr uint32_t kDecorationModeServerSide = 2;

/**
 * We draw only when we are told to, or when there is no one to ask — unless `--no-decorations` says to draw
 * nothing at all.
 *
 * `configuredMode` is the mode the compositor's most recent `zxdg_toplevel_decoration_v1.configure` named, or
 * `std::nullopt` when it has not answered yet. An answer this platform does not recognise — a mode a later
 * protocol version adds — leaves the window server-decorated, because `WaylandWindow` asked for server-side and
 * drawing a second title bar over one the compositor is already drawing is the worse of the two failures.
 *
 * `noDecorations` is the test rig's seam, the mirror image of `forceClientDecorations`: both cage (e2e) and
 * weston (window goldens) offer no `zxdg_decoration_manager_v1`, which used to fall through to the drawn bar and
 * put every scripted coordinate `titleBarHeight` points off content. `Bare` is that seam's answer — no request,
 * no drawn bar, zero inset — and it is checked ahead of the no-manager fallback so it wins there, but still loses
 * to `forceClientDecorations`: forcing the bar is how the bar itself gets proven under these same compositors, so
 * asking for both at once has to keep drawing it.
 */
DecorationMode decideDecorationMode(bool hasDecorationManager, bool forceClientDecorations, bool noDecorations,
                                    std::optional<uint32_t> configuredMode) noexcept;

/**
 * Whether there is a drawn bar to paint or to route pointer events into, right now.
 *
 * `contentExtentOf` already zeroes the bar's inset in fullscreen — the content should fill the surface, bar or
 * not — but painting and input routing used to key off `mode` alone, so a `Client`-decorated window that went
 * fullscreen kept drawing the bar at `topOffset` zero, over the content, and kept stealing the pointer events
 * landing in that band. `isChromeActive` is the one predicate both `paintDecoratedFrame` and
 * `routeDecorationInput` read instead, so the fullscreen exception cannot drift between the two the way the
 * inset itself is not allowed to (#374): only `Client` and not fullscreen draws or routes anything; `Server` and
 * `Bare` never do, fullscreen or not, because there is no bar in either to begin with.
 */
constexpr bool isChromeActive(DecorationMode mode, bool isFullscreen) noexcept {
    return mode == DecorationMode::Client && !isFullscreen;
}

/**
 * The four numbers the drawn chrome is made of, in surface points.
 *
 * There is no shadow gutter, and that is a decision rather than an omission: zed#44528 is what a gutter costs —
 * the client-side inset and `xdg_surface.set_window_geometry` disagreed, so the resize handles landed outside
 * the window the compositor believed existed. With no gutter the window geometry is the surface's own bounding
 * box, which is already xdg-shell's default, so there is no second number to keep in step and no
 * `set_window_geometry` call at all.
 */
struct DecorationMetrics {
    float titleBarHeight{32.0F};
    float resizeEdgeWidth{8.0F};
    float buttonWidth{40.0F};
};

struct DecorationRect {
    float left{0.0F};
    float top{0.0F};
    float right{0.0F};
    float bottom{0.0F};

    bool operator==(const DecorationRect&) const = default;
};

/**
 * The bar and the four rectangles inside it, in surface coordinates. Button order is fixed — minimize, maximize,
 * close, left to right against the right edge — rather than read from `org.gnome.desktop.wm.preferences
 * button-layout`; that setting needs a GSettings or portal read this platform has no client for yet, and the
 * gap is named on #329.
 *
 * `drag` is what is left of the bar once the buttons are taken out of it, so a narrow window whose buttons fill
 * the bar has an empty drag region rather than one that overlaps them.
 */
struct TitleBarLayout {
    DecorationRect bar{};
    DecorationRect drag{};
    DecorationRect minimize{};
    DecorationRect maximize{};
    DecorationRect close{};
};

TitleBarLayout layoutTitleBar(uint32_t windowWidth, const DecorationMetrics& metrics) noexcept;

enum class DecorationHit : uint8_t {
    Content,
    Drag,
    Minimize,
    Maximize,
    Close,
    ResizeTop,
    ResizeBottom,
    ResizeLeft,
    ResizeRight,
    ResizeTopLeft,
    ResizeTopRight,
    ResizeBottomLeft,
    ResizeBottomRight,
};

/**
 * What is under a point of the client-decorated window, in surface coordinates.
 *
 * The resize edges are tested first and win over everything, including the buttons: the outermost eight points
 * of the title bar belong to the corner handles, because a window whose top-right corner cannot be grabbed
 * because the close button is there is the complaint zed#31884 and its neighbours are made of. `Content` means
 * the point belongs to the application, and the caller subtracts the bar height before handing it to the scene.
 *
 * `isEffectivelyTiled` (#374, `ToplevelState::isEffectivelyTiled`) drops the resize-edge hits, all four of them
 * together rather than per edge, because there is no per-edge signal here worth trusting either — a tiled window
 * has nothing to grab on the edge the compositor placed against the screen's own border, and GNOME's habit of
 * reporting every edge tiled even when only one is means a per-edge answer would silently disable resizing on
 * edges that are still free. A tiled window is still moved and closed the same way, so only the eight resize
 * hits fall back to `Content`; the bar's own buttons and drag region are untouched.
 */
DecorationHit hitTestDecorations(const DecorationMetrics& metrics, uint32_t windowWidth, uint32_t windowHeight,
                                 bool isEffectivelyTiled, float x, float y) noexcept;

/**
 * The `xdg_toplevel::resize_edge` wire value one hit asks for, or `none` (zero) for a hit that is not a resize
 * edge. xdg-shell's own numbering: `top = 1`, `bottom = 2`, `left = 4`, `right = 8`, and a corner is the two
 * summed.
 */
uint32_t resizeEdgeOfHit(DecorationHit hit) noexcept;

/**
 * The extent the React surface is laid out at, and how far down the surface it is drawn.
 *
 * The height is floored at one point because the configured window can be shorter than the bar during an
 * interactive resize, and a zero-sized Fabric surface — like a zero window geometry — is not something to hand
 * downstream.
 *
 * `isFullscreen` (#374) drops the bar to zero regardless of `mode`: a fullscreen window draws no chrome under
 * either decoration mode, and this is the one call site both `WindowMain`'s paint routing and every consumer of
 * `contentExtentOf` share, so the rule lives once here rather than once per caller. Before this it was possible
 * for a client-decorated window to keep reporting a bar-height inset — to `Dimensions`, to the Fabric root, to
 * hit-testing — for a bar `TitleBarPainter` had stopped drawing, which is the "one consumer disagreeing with
 * another" electron#51679 is made of.
 */
struct ContentExtent {
    uint32_t width{0};
    uint32_t height{0};
    float topOffset{0.0F};
};

ContentExtent contentExtentOf(DecorationMode mode, bool isFullscreen, const DecorationMetrics& metrics,
                              uint32_t windowWidth, uint32_t windowHeight) noexcept;

/**
 * A plain width/height pair, in whichever of the two spaces `surfaceToLogical`/`logicalToSurface` names it: the
 * surface's own points, as `xdg_toplevel.configure` reports them, or the logical points the application and
 * `Dimensions` deal in. There is no `topOffset` here — that is `ContentExtent`'s alone, for the one caller that
 * still has to know where the content starts on the surface itself in order to paint and hit-test it.
 */
struct WindowExtent {
    uint32_t width{0};
    uint32_t height{0};

    bool operator==(const WindowExtent&) const = default;
};

/**
 * The single conversion pair every consumer of a window's size is meant to read from, in one direction or the
 * other, rather than re-deriving "how big is the inset" at its own call site — the mistake behind three separate
 * open Electron bugs (electron#50017, electron#51679, electron#45916), all three traced to
 * `ElectronDesktopWindowTreeHostLinux::GetMinimumSizeForWindow`/`GetMaximumSizeForWindow`
 * (`shell/browser/ui/electron_desktop_window_tree_host_linux.cc:101-127`) inflating a size constraint by an inset
 * a different call site had already applied, or had not applied at all.
 *
 * `surfaceToLogical` removes exactly what `contentExtentOf` removes — the bar, when `mode` is `Client` and
 * `isFullscreen` is false — and then converts the result from surface points to logical ones by `scale`.
 * `logicalToSurface` reverses that order: scale first, then add the bar back. Under server-side decorations, the
 * frameless case, or fullscreen, the bar term is zero and the pair degenerates to a pure scale conversion — "one
 * arithmetic path covers server-side decorations and the frameless case", in the words of the Electron comment
 * this fixes.
 *
 * The two are round-trip inverses, not bit-exact ones: both directions round independently, with one rule
 * (`roundedRatio`'s `std::llround`, i.e. round half away from zero). `logicalToSurface(surfaceToLogical(x))` is
 * guaranteed to land back on `x` when `x` divides evenly by `scale`'s numerator in lowest terms — every 3 px at
 * scale 1.5 (`= 3/2`), every 5 px at 1.25 (`= 5/4`), every 4 px at 4 (`= 4/1`) — but that grid is a sufficient
 * condition, not a necessary one: an off-grid value can still land back exactly (surface `2` at scale 1.5 does),
 * it is simply not promised to. What is promised off the grid is a bound, not exactness: at most
 * `ceil(scale / 2)` surface pixels of drift, because each of the two roundings can move its own value by up to
 * half a unit — one half surface pixel from `logicalToSurface`'s rounding, inflated to at most `ceil(scale / 2)`
 * surface pixels once `surfaceToLogical`'s own half-logical-pixel rounding is scaled back up. That bound grows
 * with `scale` and is not capped at one pixel the way an earlier version of this comment claimed — at scale 4,
 * for instance, surface `{2, 2}` round-trips through logical `{1, 1}` to `{4, 4}`, two pixels off, matching
 * `ceil(4 / 2) = 2`. `DimensionsSource::configure` and `WaylandWindow` accept any positive `scale`; neither
 * clamps it to a fractional-scale protocol's range, so the bound above is the one every `scale` this pair is
 * ever called with has to satisfy, not one assumed for a fixed set of compositor-advertised values.
 *
 * `scale` is always `1.0` at every call site today, the same way `DimensionsSource::configure`'s `scale`
 * parameter is: neither `wp_fractional_scale_v1` nor `wl_surface.preferred_buffer_scale` is bound yet. The
 * parameter exists so the day that changes is a call-site change here too, not a second redesign of this pair.
 *
 * `logicalToSurface` leaves a zero component at zero instead of inflating it by the bar or the scale, because
 * zero is xdg-shell's and Chromium's shared spelling of "no constraint" — a `set_max_size` of `(0, 0)` means
 * unbounded, not "one bar's height, one row of content" — the exact rule `GetMaximumSizeForWindow`'s own comment
 * states ("a maximum of 0 is deliberately not inflated, because zero means no constraint") and the rule this
 * function mirrors so it cannot drift between the two directions the way it did across Electron's three bugs.
 */
WindowExtent surfaceToLogical(DecorationMode mode, bool isFullscreen, const DecorationMetrics& metrics, double scale,
                              WindowExtent surfaceExtent) noexcept;
WindowExtent logicalToSurface(DecorationMode mode, bool isFullscreen, const DecorationMetrics& metrics, double scale,
                              WindowExtent logicalExtent) noexcept;

/** The interval within which two presses on the drag region are one double click, in milliseconds. */
constexpr uint64_t kDoubleClickIntervalMilliseconds = 400;

/**
 * The double-click-to-maximize rule, as a two-press state machine rather than a timer.
 *
 * A completed double click clears the memory of both presses, so three presses in quick succession are one
 * double click and one lone press rather than two overlapping double clicks.
 *
 * Threading contract: not synchronised, and driven only from the frame thread that owns the Wayland connection.
 */
class DoubleClickDetector final {
public:
    /** Whether the press at `milliseconds` completes a double click with the press before it. */
    bool recordPress(uint64_t milliseconds) noexcept;

private:
    std::optional<uint64_t> previousPress_;
};

/**
 * Pointer capture across a primary-button press/release pair.
 *
 * A hit test alone routes each pointer event by where it currently lands, which drops the release half of a
 * drag that crosses the bar boundary: a press that starts in the content and whose release lands on the bar (or
 * the reverse) never reaches the side that got the press, per #399. The fix is the one pointer-capture rule
 * every toolkit applies — once a primary press picks a side, every event that follows, wherever it hit-tests,
 * stays on that side until the matching primary release, which also ends the capture.
 *
 * Threading contract: not synchronised, and driven only from the frame thread that owns the Wayland connection,
 * the same as `DoubleClickDetector`.
 */
class PointerCapture final {
public:
    /**
     * Whether the event with this hit test result belongs to the content rather than the chrome. `isPrimaryPress`
     * and `isPrimaryRelease` name whether this event is the primary button's press or release; every other event
     * (motion, other buttons, scroll) passes both as `false` and only reads the capture already in progress.
     */
    bool routeToContent(DecorationHit hit, bool isPrimaryPress, bool isPrimaryRelease) noexcept;

    /**
     * Drops a capture in progress without waiting for the primary release that would normally end it.
     *
     * A decoration mode change mid-drag is the case this exists for: `Client` routes through `routeToContent`
     * and `Server`/`Bare` never call it at all, so a capture taken under `Client` would otherwise survive a
     * switch away and back, routing the first press after the switch by a hit test the pointer never made.
     */
    void release() noexcept;

private:
    std::optional<bool> capturedToContent_;
};

} // namespace react_native_linux
