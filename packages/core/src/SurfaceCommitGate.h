#pragma once

#include <cstdint>
#include <string_view>

namespace react_native_linux {

/**
 * The ordering rule for the first — and every subsequent — buffer this window attaches to its `wl_surface`.
 *
 * A Wayland client that is mapped but never shown is this platform's default failure rather than an exotic one,
 * and it has four reachable causes that all end in the same symptom of an empty window with no error anywhere: a
 * buffer committed before the initial `xdg_surface.configure` was acknowledged, which leaves the toplevel
 * unmapped (zed#37918); a buffer whose extent disagrees with the extent the compositor configured; an acquire
 * that keeps returning no image, which a software driver given the minimum number of swapchain images does under
 * load (zed#16414); and a content update the compositor `discarded`, after which a client waiting on a frame
 * callback that will never arrive presents nothing again (zed#16428, zed#52062).
 *
 * All four are one question asked once per frame — may this frame attach a buffer, and if not, what has to happen
 * instead — so they are one table here rather than four guards spread through the renderer. The table is pure and
 * includes neither Vulkan nor Wayland, exactly as `VulkanResultPolicy` and `ToplevelState` do, which is what puts
 * it inside the `rnl_core_tests` coverage gate. `SkiaVulkanRenderer` observes the state and carries the action
 * out; it decides nothing. See *Surface commit ordering* in docs/cpp-toolchain.md.
 */
enum class SurfaceCommitAction : uint8_t {
    WaitForConfigure,
    RecreateSwapchainAtConfiguredExtent,
    RecreateSwapchainWithMoreImages,
    RepresentDiscardedFrame,
    AttachBuffer,
};

/**
 * Everything the rule reads, gathered once at the top of a frame. `consecutiveAcquireStarvations` counts acquires
 * that returned no image in a row, and any acquire that returns one resets it.
 */
struct SurfaceCommitState {
    bool isConfigureAcknowledged{false};
    bool doesBufferExtentMatchConfigure{false};
    bool wasLastContentUpdateDiscarded{false};
    uint32_t consecutiveAcquireStarvations{0};
    uint32_t extraSwapchainImages{0};
};

/**
 * How many acquires may starve in a row before the swapchain is rebuilt with one more image. A single starved
 * acquire is the pacing outcome `VulkanResultPolicy` already answers with `RetryNextFrame`; only a run of them is
 * evidence that the swapchain itself is too small to make progress under this driver.
 */
constexpr uint32_t kAcquireStarvationLimit = 3;

/**
 * The ceiling on that recovery, which is what separates a shortage from an occluded window. A compositor is
 * allowed to hold every buffer of a surface it is not showing — Hyprland does, for a window on an inactive
 * workspace — and such a window starves every acquire forever without anything being wrong. Growing the swapchain
 * a bounded number of times distinguishes the two: a real shortage is relieved within a couple of images, and an
 * occluded window simply keeps retrying, which is what it should do, instead of allocating a swapchain per frame.
 */
constexpr uint32_t kMaxExtraSwapchainImages = 2;

/**
 * The rule, in precedence order, because more than one cause can hold at once and only the earliest of them is
 * actionable:
 *
 * 1. **No acknowledged configure — `WaitForConfigure`.** xdg-shell forbids attaching a buffer before the initial
 *    configure is acknowledged, and a compositor that receives one either raises a protocol error or never maps
 *    the toplevel. Nothing below can be assessed yet, because the configured extent does not exist until then.
 * 2. **The buffer extent disagrees with the configure — `RecreateSwapchainAtConfiguredExtent`.** Attaching it
 *    would commit a buffer the compositor did not ask for, so the swapchain is rebuilt at the configured extent
 *    and this frame attaches nothing.
 * 3. **The acquire has starved `kAcquireStarvationLimit` times running, and the swapchain has not already grown
 *    `kMaxExtraSwapchainImages` times — `RecreateSwapchainWithMoreImages`.** The swapchain cannot hand an image
 *    back, so retrying it forever is exactly the invisible window. One more image is the recovery, because the
 *    starvation is a shortage rather than a fault — up to the ceiling, past which it is an occluded window and
 *    retrying is correct.
 * 4. **The last content update was discarded — `RepresentDiscardedFrame`.** That frame never turned into light
 *    and no frame callback is owed for it, so the client re-presents instead of waiting for one. The re-present
 *    is a full repaint rather than an idle present, so what the compositor gets this time is a complete frame.
 * 5. **Otherwise — `AttachBuffer`.** The ordinary frame.
 */
SurfaceCommitAction surfaceCommitActionFor(const SurfaceCommitState& state) noexcept;

/**
 * The state a `--window-debug` run forces, so that each of the four ways a window ends up invisible is reached on
 * demand rather than only when a driver or a compositor happens to produce it. A fault is applied to the state the
 * frame actually observed, so the action it provokes and the recovery that follows are the real ones.
 */
enum class SurfaceCommitFault : uint8_t {
    None,
    CommitBeforeConfigure,
    BufferExtentMismatch,
    AcquireStarvation,
    ContentUpdateDiscarded,
};

SurfaceCommitState applySurfaceCommitFault(const SurfaceCommitState& state, SurfaceCommitFault fault) noexcept;

/** The spelling of an action and of a fault, for the `--window-debug` trace. */
std::string_view describeSurfaceCommitAction(SurfaceCommitAction action) noexcept;
std::string_view describeSurfaceCommitFault(SurfaceCommitFault fault) noexcept;

} // namespace react_native_linux
