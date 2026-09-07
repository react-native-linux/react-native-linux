#include "RendererLadder.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace react_native_linux {

namespace {

constexpr std::string_view kRungKey = "rung";
constexpr std::string_view kCrashesKey = "crashes";
constexpr std::string_view kRasterSessionsKey = "raster-sessions";
constexpr std::string_view kDriverKey = "driver";
constexpr std::string_view kStateFileSuffix = "/react-native-linux/renderer-ladder";
constexpr std::string_view kHomeStateDirectory = "/.local/state";

std::optional<uint32_t> parseCount(std::string_view value) noexcept {
    uint32_t count = 0;
    const std::from_chars_result parsed = std::from_chars(value.data(), value.data() + value.size(), count);

    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) {
        return std::nullopt;
    }

    return count;
}

std::vector<size_t> devicePreferenceOrder(std::span<const uint32_t> presentableDeviceTypes) {
    std::vector<size_t> order(presentableDeviceTypes.size());

    for (size_t index = 0; index < order.size(); ++index) {
        order[index] = index;
    }

    std::ranges::stable_partition(order, [presentableDeviceTypes](size_t index) {
        return presentableDeviceTypes[index] == kVulkanDeviceTypeDiscreteGpu;
    });

    return order;
}

} // namespace

std::string_view describeRendererRung(RendererRung rung) noexcept {
    switch (rung) { // COV_EXCL: every RendererRung value has a case, so no-match cannot execute
    case RendererRung::PreferredVulkanDevice:
        return "preferred-vulkan";
    case RendererRung::AlternateVulkanDevice:
        return "alternate-vulkan";
    case RendererRung::SoftwareVulkanDevice:
        return "software-vulkan";
    case RendererRung::SharedMemoryRaster:
        return "raster";
    }

    return "raster"; // COV_EXCL: every RendererRung value has a case above, so this cannot execute
}

std::optional<RendererRung> parseRendererRung(std::string_view name) noexcept {
    if (name == describeRendererRung(RendererRung::PreferredVulkanDevice)) {
        return RendererRung::PreferredVulkanDevice;
    }

    if (name == describeRendererRung(RendererRung::AlternateVulkanDevice)) {
        return RendererRung::AlternateVulkanDevice;
    }

    if (name == describeRendererRung(RendererRung::SoftwareVulkanDevice)) {
        return RendererRung::SoftwareVulkanDevice;
    }

    if (name == describeRendererRung(RendererRung::SharedMemoryRaster)) {
        return RendererRung::SharedMemoryRaster;
    }

    return std::nullopt;
}

std::optional<RendererRung> nextRendererRung(RendererRung rung) noexcept {
    switch (rung) { // COV_EXCL: every RendererRung value has a case, so no-match cannot execute
    case RendererRung::PreferredVulkanDevice:
        return RendererRung::AlternateVulkanDevice;
    case RendererRung::AlternateVulkanDevice:
        return RendererRung::SoftwareVulkanDevice;
    case RendererRung::SoftwareVulkanDevice:
        return RendererRung::SharedMemoryRaster;
    case RendererRung::SharedMemoryRaster:
        return std::nullopt;
    }

    return std::nullopt; // COV_EXCL: every RendererRung value has a case above, so this cannot execute
}

std::optional<size_t> selectVulkanDeviceForRung(std::span<const uint32_t> presentableDeviceTypes,
                                                RendererRung rung) noexcept {
    if (rung == RendererRung::SharedMemoryRaster) {
        return std::nullopt;
    }

    if (rung == RendererRung::SoftwareVulkanDevice) {
        for (size_t index = 0; index < presentableDeviceTypes.size(); ++index) {
            if (presentableDeviceTypes[index] == kVulkanDeviceTypeCpu) {
                return index;
            }
        }

        return std::nullopt;
    }

    const std::vector<size_t> order = devicePreferenceOrder(presentableDeviceTypes);
    const size_t position = rung == RendererRung::PreferredVulkanDevice ? 0 : 1;

    if (position >= order.size()) {
        return std::nullopt;
    }

    return order[position];
}

std::string_view describeRendererStartReason(RendererStartReason reason) noexcept {
    switch (reason) { // COV_EXCL: every RendererStartReason value has a case, so no-match cannot execute
    case RendererStartReason::NoPersistedDecision:
        return "no persisted decision";
    case RendererStartReason::ForcedByOverride:
        return "forced by --renderer";
    case RendererStartReason::ResumedLastSuccessfulRung:
        return "resumed the rung that last presented a frame";
    case RendererStartReason::AdvancedAfterCrashBeforeFirstFrame:
        return "advanced after a crash before the first presented frame";
    case RendererStartReason::RetriedTopRungAfterRasterCap:
        return "retried the top rung after the raster session cap";
    case RendererStartReason::RetriedTopRungOnNewDriver:
        return "retried the top rung on a new driver";
    case RendererStartReason::LadderExhausted:
        return "the ladder is exhausted; raster is the last rung";
    }

    return "no persisted decision"; // COV_EXCL: every RendererStartReason value has a case above
}

RendererStart rendererStartRung(const std::optional<RendererLadderRecord>& persisted,
                                std::optional<RendererRung> forcedRung, std::string_view driverIdentity) {
    if (forcedRung.has_value()) {
        return RendererStart{.rung = *forcedRung, .reason = RendererStartReason::ForcedByOverride};
    }

    if (!persisted.has_value()) {
        return RendererStart{.rung = kTopRendererRung, .reason = RendererStartReason::NoPersistedDecision};
    }

    if (persisted->driverIdentity != driverIdentity) {
        return RendererStart{.rung = kTopRendererRung, .reason = RendererStartReason::RetriedTopRungOnNewDriver};
    }

    if (persisted->consecutiveCrashes == 0) {
        if (persisted->rung == kBottomRendererRung &&
            persisted->successfulRasterSessions >= kRasterSessionsBeforeRetryingTopRung) {
            return RendererStart{.rung = kTopRendererRung, .reason = RendererStartReason::RetriedTopRungAfterRasterCap};
        }

        return RendererStart{.rung = persisted->rung, .reason = RendererStartReason::ResumedLastSuccessfulRung};
    }

    const std::optional<RendererRung> next = nextRendererRung(persisted->rung);

    if (!next.has_value()) {
        return RendererStart{.rung = kBottomRendererRung, .reason = RendererStartReason::LadderExhausted};
    }

    return RendererStart{.rung = *next, .reason = RendererStartReason::AdvancedAfterCrashBeforeFirstFrame};
}

RendererLadderRecord recordRendererAttempt(const std::optional<RendererLadderRecord>& persisted,
                                           RendererRung attemptedRung, std::string_view driverIdentity) {
    RendererLadderRecord attempt{.rung = attemptedRung,
                                 .consecutiveCrashes = 0,
                                 .successfulRasterSessions = 0,
                                 .driverIdentity = std::string(driverIdentity)};

    if (persisted.has_value() && persisted->driverIdentity == driverIdentity) {
        attempt.successfulRasterSessions = persisted->successfulRasterSessions;

        if (persisted->rung == attemptedRung) {
            attempt.consecutiveCrashes = persisted->consecutiveCrashes;
        }
    }

    attempt.consecutiveCrashes += 1;

    return attempt;
}

RendererLadderRecord recordFirstPresentedFrame(const RendererLadderRecord& attempt) {
    RendererLadderRecord success = attempt;

    success.consecutiveCrashes = 0;
    success.successfulRasterSessions = attempt.rung == kBottomRendererRung ? attempt.successfulRasterSessions + 1 : 0;

    return success;
}

std::string formatRendererLadderRecord(const RendererLadderRecord& record) {
    std::string contents;

    contents.append(kRungKey).append("=").append(describeRendererRung(record.rung)).append("\n");
    contents.append(kCrashesKey).append("=").append(std::to_string(record.consecutiveCrashes)).append("\n");
    contents.append(kRasterSessionsKey)
        .append("=")
        .append(std::to_string(record.successfulRasterSessions))
        .append("\n");
    contents.append(kDriverKey).append("=").append(record.driverIdentity).append("\n");

    return contents;
}

std::optional<RendererLadderRecord> parseRendererLadderRecord(std::string_view contents) {
    RendererLadderRecord record;
    bool hasRung = false;
    bool hasCrashes = false;
    bool hasRasterSessions = false;
    bool hasDriver = false;

    while (!contents.empty()) {
        const size_t lineEnd = contents.find('\n');

        if (lineEnd == std::string_view::npos) {
            return std::nullopt;
        }

        const std::string_view line = contents.substr(0, lineEnd);
        contents.remove_prefix(lineEnd + 1);

        const size_t separator = line.find('=');

        if (separator == std::string_view::npos) {
            return std::nullopt;
        }

        const std::string_view key = line.substr(0, separator);
        const std::string_view value = line.substr(separator + 1);

        if (key == kRungKey) {
            const std::optional<RendererRung> rung = parseRendererRung(value);

            if (!rung.has_value()) {
                return std::nullopt;
            }

            record.rung = *rung;
            hasRung = true;
        } else if (key == kCrashesKey) {
            const std::optional<uint32_t> crashes = parseCount(value);

            if (!crashes.has_value()) {
                return std::nullopt;
            }

            record.consecutiveCrashes = *crashes;
            hasCrashes = true;
        } else if (key == kRasterSessionsKey) {
            const std::optional<uint32_t> sessions = parseCount(value);

            if (!sessions.has_value()) {
                return std::nullopt;
            }

            record.successfulRasterSessions = *sessions;
            hasRasterSessions = true;
        } else if (key == kDriverKey) {
            record.driverIdentity = std::string(value);
            hasDriver = true;
        } else {
            return std::nullopt;
        }
    }

    if (!hasRung || !hasCrashes || !hasRasterSessions || !hasDriver) {
        return std::nullopt;
    }

    return record;
}

std::optional<std::string> rendererLadderStatePath(std::string_view xdgStateHome, std::string_view homeDirectory) {
    if (xdgStateHome.starts_with('/')) {
        return std::string(xdgStateHome).append(kStateFileSuffix);
    }

    if (homeDirectory.starts_with('/')) {
        return std::string(homeDirectory).append(kHomeStateDirectory).append(kStateFileSuffix);
    }

    return std::nullopt;
}

} // namespace react_native_linux
