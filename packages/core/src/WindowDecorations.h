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
 * We draw only when we are told to, or when there is no one to ask.
 *
 * `configuredMode` is the mode the compositor's most recent `zxdg_toplevel_decoration_v1.configure` named, or
 * `std::nullopt` when it has not answered yet. An answer this platform does not recognise — a mode a later
 * protocol version adds — leaves the window server-decorated, because `WaylandWindow` asked for server-side and
 * drawing a second title bar over one the compositor is already drawing is the worse of the two failures.
 */
DecorationMode decideDecorationMode(bool hasDecorationManager, bool forceClientDecorations,
                                    std::optional<uint32_t> configuredMode) noexcept;

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
 */
DecorationHit hitTestDecorations(const DecorationMetrics& metrics, uint32_t windowWidth, uint32_t windowHeight, float x,
                                 float y) noexcept;

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
 */
struct ContentExtent {
    uint32_t width{0};
    uint32_t height{0};
    float topOffset{0.0F};
};

ContentExtent contentExtentOf(DecorationMode mode, const DecorationMetrics& metrics, uint32_t windowWidth,
                              uint32_t windowHeight) noexcept;

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

private:
    std::optional<bool> capturedToContent_;
};

} // namespace react_native_linux
