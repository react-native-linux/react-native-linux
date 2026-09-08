#pragma once

#include <cstdint>
#include <string_view>

namespace react_native_linux {

/**
 * What the renderer does about a `VkResult` returned on the swapchain path, which is `vkAcquireNextImageKHR` and
 * `vkQueuePresentKHR`.
 *
 * There is no fence wait in that path to police: the acquire signals a semaphore and passes `VK_NULL_HANDLE` for
 * its fence, and every submission fence belongs to Ganesh, which owns its own device-lost reporting. The two
 * calls above are the whole surface this table covers.
 */
enum class VulkanRecovery : uint8_t {
    Proceed,
    PresentThenRecreateSwapchain,
    RecreateSwapchain,
    RecreateSurface,
    RecreateDevice,
    RetryNextFrame,
    FatalWithDiagnostic,
};

/**
 * The `VkResult` values the swapchain path can return, as their own wire constants rather than through
 * `vulkan_core.h`. They are frozen by the Vulkan specification's compatibility guarantee, the same way
 * `ToplevelState`'s four xdg-shell values are, and stating them here is what keeps this file free of Vulkan and
 * Skia and therefore inside the unit-test coverage gate. `SkiaVulkanRenderer.cpp` compiles a `static_assert` per
 * constant against the real enumerator, so a drift between the two is a build error rather than a wrong policy.
 */
constexpr int32_t kVulkanSuccess = 0;
constexpr int32_t kVulkanNotReady = 1;
constexpr int32_t kVulkanTimeout = 2;
constexpr int32_t kVulkanErrorOutOfHostMemory = -1;
constexpr int32_t kVulkanErrorOutOfDeviceMemory = -2;
constexpr int32_t kVulkanErrorDeviceLost = -4;
constexpr int32_t kVulkanErrorSurfaceLost = -1'000'000'000;
constexpr int32_t kVulkanSuboptimal = 1'000'001'003;
constexpr int32_t kVulkanErrorOutOfDate = -1'000'001'004;
constexpr int32_t kVulkanErrorFullScreenExclusiveModeLost = -1'000'255'000;

/**
 * The decision table. Every value not named below is `FatalWithDiagnostic`, which is the only defensible default:
 * an unrecognised result on the present path is a driver contract this code has never been read against, and
 * guessing a recovery for it would hide the bug rather than fix it.
 *
 * - `VK_SUBOPTIMAL_KHR` presents anyway and rebuilds afterwards. The image is valid and already drawn, so
 *   throwing it away would drop a frame the compositor can display; the swapchain merely no longer matches the
 *   surface exactly, which the next frame fixes.
 * - `VK_ERROR_OUT_OF_DATE_KHR` rebuilds before drawing. The image is not presentable at all, so there is nothing
 *   to salvage; this is the lid-close and resize case.
 * - `VK_ERROR_SURFACE_LOST_KHR` rebuilds the `VkSurfaceKHR` from the `wl_surface` and then the swapchain. An
 *   output hotplug or a compositor restart invalidates the surface without invalidating the device, so rebuilding
 *   the device would be both wrong and slower — see zed#14225 and zed#43851, where this was unwrapped instead.
 * - `VK_TIMEOUT` and `VK_NOT_READY` retry on the next frame with nothing rebuilt: no image was free within the
 *   acquire timeout, which is a pacing outcome and not a fault.
 * - `VK_ERROR_DEVICE_LOST` rebuilds the device, the queue and the `GrDirectContext` and then the swapchain, and
 *   drops every cached resource `GpuResourceInvalidation.h` marks as device-owned first. A lid close, a suspend
 *   and resume, a driver update and a GPU reset all arrive as this result, and an application that dies on it is
 *   not shippable — zed#23288. The dropping is the whole of the difficulty and is why it is a contract rather
 *   than a bigger rebuild: a handle replayed into the fresh device is the crash zed#62998 reports and the
 *   garbling zed#58382 reports.
 * - Both out-of-memory results and `VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT` are fatal with a named
 *   diagnostic, because no recovery exists that does not first free the allocation that failed.
 */
VulkanRecovery vulkanRecoveryFor(int32_t result) noexcept;

/**
 * The spelling of a result for a diagnostic. An unnamed value returns an empty view and the caller prints the
 * number, because inventing a name for a result this table has never seen would be a lie in a crash message.
 */
std::string_view describeVulkanResult(int32_t result) noexcept;

} // namespace react_native_linux
