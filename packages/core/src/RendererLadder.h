#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace react_native_linux {

/**
 * The rungs the window tries, in order, when it brings a renderer up.
 *
 * ADR-0001 accepts Vulkan driver defects "for which we have no workaround" as a risk and stops there, which
 * leaves the third consecutive failed initialisation with no answer but `exit`. Chromium's answer, in
 * `content/browser/gpu/gpu_data_manager_impl_private.cc`, is a list built bottom-up whose bottom entry always
 * works and whose failed entries are erased rather than retried; electron#32317 is what a user sees when only
 * the bottom is left and nothing says so. This enumeration is that list, ordered top-down so a smaller value is
 * a better renderer.
 *
 * `SharedMemoryRaster` is the rung that always works: Skia's CPU backend painting into a `wl_shm` buffer needs
 * no driver, no `VkInstance` and no GPU, so a machine on which every Vulkan rung fails still shows its window.
 */
enum class RendererRung : uint8_t {
    PreferredVulkanDevice = 0,
    AlternateVulkanDevice = 1,
    SoftwareVulkanDevice = 2,
    SharedMemoryRaster = 3,
};

constexpr RendererRung kTopRendererRung = RendererRung::PreferredVulkanDevice;
constexpr RendererRung kBottomRendererRung = RendererRung::SharedMemoryRaster;

/**
 * How many consecutive sessions that presented a frame on `SharedMemoryRaster` are allowed before the ladder
 * starts at the top again. Without it a single crash on a machine whose driver is later fixed would keep that
 * machine on the CPU forever, which is the failure electron#32317's reporters describe: an installation that is
 * permanently degraded with no way back short of deleting state.
 */
constexpr uint32_t kRasterSessionsBeforeRetryingTopRung = 10;

/**
 * `VkPhysicalDeviceType` as its own wire constants rather than through `vulkan_core.h`, for the reason
 * `VulkanResultPolicy.h` states for `VkResult`: the values are frozen by the Vulkan specification's
 * compatibility guarantee, and repeating them here is what keeps this file free of Vulkan and Skia and therefore
 * inside the unit-test coverage gate. `SkiaVulkanRenderer.cpp` compiles a `static_assert` per constant against
 * the real enumerator.
 */
constexpr uint32_t kVulkanDeviceTypeOther = 0;
constexpr uint32_t kVulkanDeviceTypeIntegratedGpu = 1;
constexpr uint32_t kVulkanDeviceTypeDiscreteGpu = 2;
constexpr uint32_t kVulkanDeviceTypeVirtualGpu = 3;
constexpr uint32_t kVulkanDeviceTypeCpu = 4;

/** The spelling `--renderer` accepts and `--window-debug` prints. */
std::string_view describeRendererRung(RendererRung rung) noexcept;
std::optional<RendererRung> parseRendererRung(std::string_view name) noexcept;

/** The rung below `rung`, or `std::nullopt` at `SharedMemoryRaster`, which has nothing below it. */
std::optional<RendererRung> nextRendererRung(RendererRung rung) noexcept;

/**
 * Which of the devices that present to the window's surface a rung means, given their `VkPhysicalDeviceType`s in
 * enumeration order. Discrete devices sort before everything else and ties keep enumeration order, so the answer
 * is deterministic on a machine whose device list is.
 *
 * `PreferredVulkanDevice` is the first in that order and `AlternateVulkanDevice` the second, which is what makes
 * the failed rung unretryable rather than merely deprioritised: a second attempt cannot land on the device the
 * first one crashed. `SoftwareVulkanDevice` is the first `VK_PHYSICAL_DEVICE_TYPE_CPU` device, which is lavapipe
 * where it is installed. `SharedMemoryRaster` uses no Vulkan device at all and answers `std::nullopt`, as does
 * any rung the machine has no device for.
 */
std::optional<size_t> selectVulkanDeviceForRung(std::span<const uint32_t> presentableDeviceTypes,
                                                RendererRung rung) noexcept;

/**
 * What the previous session left behind, in `$XDG_STATE_HOME/react-native-linux/renderer-ladder`.
 *
 * `consecutiveCrashes` counts attempts on `rung` that were written before bring-up and never followed by a
 * presented frame. A crash inside a driver is a fatal signal on the render thread, not an error code — see #369
 * — so it cannot be recorded after the fact; recording the attempt first and clearing it on the first presented
 * frame is what turns a process that never came back into a number the next launch can read.
 *
 * `driverIdentity` is the string the renderer reports for the device it ran on. Any change to it invalidates the
 * record, which is how a driver update, a new GPU or a changed adapter set gets the top rung back.
 */
struct RendererLadderRecord {
    RendererRung rung{kTopRendererRung};
    uint32_t consecutiveCrashes{0};
    uint32_t successfulRasterSessions{0};
    std::string driverIdentity;
};

/** Why the ladder starts where it does, printed verbatim by `--window-debug`. */
enum class RendererStartReason : uint8_t {
    NoPersistedDecision,
    ForcedByOverride,
    ResumedLastSuccessfulRung,
    AdvancedAfterCrashBeforeFirstFrame,
    RetriedTopRungAfterRasterCap,
    RetriedTopRungOnNewDriver,
    LadderExhausted,
};

struct RendererStart {
    RendererRung rung{kTopRendererRung};
    RendererStartReason reason{RendererStartReason::NoPersistedDecision};
};

std::string_view describeRendererStartReason(RendererStartReason reason) noexcept;

/**
 * The whole start policy, as one pure function over the persisted record, the `--renderer` override and the
 * driver identity this launch observes.
 *
 * The override wins outright, including over a record that says the rung it names crashed: it is the one
 * documented escape hatch, and a user who types it is answering a question this table got wrong. Otherwise a
 * changed driver identity or an exhausted raster cap restarts at the top, a record with no outstanding crash
 * resumes its rung, and a record with one advances past it.
 */
RendererStart rendererStartRung(const std::optional<RendererLadderRecord>& persisted,
                                std::optional<RendererRung> forcedRung, std::string_view driverIdentity);

/**
 * The record to persist immediately before bring-up. The crash count carries over only while both the rung and
 * the driver identity are unchanged, so moving down the ladder or onto a new driver starts counting again.
 */
RendererLadderRecord recordRendererAttempt(const std::optional<RendererLadderRecord>& persisted,
                                           RendererRung attemptedRung, std::string_view driverIdentity);

/**
 * The record to persist on the first presented frame, which is #373's signal that this rung works. It clears the
 * crash count and moves the raster cap: a session that presented on raster counts towards retrying the top rung,
 * and a session that presented on any Vulkan rung resets that count, because the machine is no longer degraded.
 */
RendererLadderRecord recordFirstPresentedFrame(const RendererLadderRecord& attempt);

std::string formatRendererLadderRecord(const RendererLadderRecord& record);

/** `std::nullopt` for anything this writer did not write, which is treated exactly as a missing file. */
std::optional<RendererLadderRecord> parseRendererLadderRecord(std::string_view contents);

/**
 * Where the record lives, from `$XDG_STATE_HOME` and `$HOME` as the caller read them. The XDG base directory
 * specification requires both to be absolute and says a relative value must be ignored, which leaves a process
 * with neither no place to persist and therefore no ladder memory — the top rung every launch.
 */
std::optional<std::string> rendererLadderStatePath(std::string_view xdgStateHome, std::string_view homeDirectory);

} // namespace react_native_linux
