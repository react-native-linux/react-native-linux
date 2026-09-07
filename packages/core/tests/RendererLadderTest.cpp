#include "RendererLadder.h"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

using react_native_linux::describeRendererRung;
using react_native_linux::describeRendererStartReason;
using react_native_linux::formatRendererLadderRecord;
using react_native_linux::kRasterSessionsBeforeRetryingTopRung;
using react_native_linux::kVulkanDeviceTypeCpu;
using react_native_linux::kVulkanDeviceTypeDiscreteGpu;
using react_native_linux::kVulkanDeviceTypeIntegratedGpu;
using react_native_linux::kVulkanDeviceTypeOther;
using react_native_linux::kVulkanDeviceTypeVirtualGpu;
using react_native_linux::nextRendererRung;
using react_native_linux::parseRendererLadderRecord;
using react_native_linux::parseRendererRung;
using react_native_linux::recordFirstPresentedFrame;
using react_native_linux::recordRendererAttempt;
using react_native_linux::RendererLadderRecord;
using react_native_linux::rendererLadderStatePath;
using react_native_linux::RendererRung;
using react_native_linux::RendererStart;
using react_native_linux::RendererStartReason;
using react_native_linux::rendererStartRung;
using react_native_linux::selectVulkanDeviceForRung;

constexpr std::string_view kDriver = "llvmpipe 25.1.0";
constexpr std::string_view kOtherDriver = "AMD Radeon 780M 25.2.0";

RendererLadderRecord makeRecord(RendererRung rung, uint32_t consecutiveCrashes, uint32_t successfulRasterSessions,
                                std::string_view driverIdentity) {
    return RendererLadderRecord{.rung = rung,
                                .consecutiveCrashes = consecutiveCrashes,
                                .successfulRasterSessions = successfulRasterSessions,
                                .driverIdentity = std::string(driverIdentity)};
}

TEST(RendererLadderTest, EveryRungHasAStableSpellingThatParsesBack) {
    constexpr std::array<RendererRung, 4> rungs{RendererRung::PreferredVulkanDevice,
                                                RendererRung::AlternateVulkanDevice, RendererRung::SoftwareVulkanDevice,
                                                RendererRung::SharedMemoryRaster};

    for (RendererRung rung : rungs) {
        EXPECT_FALSE(describeRendererRung(rung).empty());
        EXPECT_EQ(parseRendererRung(describeRendererRung(rung)), rung);
    }

    EXPECT_EQ(describeRendererRung(RendererRung::PreferredVulkanDevice), "preferred-vulkan");
    EXPECT_EQ(describeRendererRung(RendererRung::AlternateVulkanDevice), "alternate-vulkan");
    EXPECT_EQ(describeRendererRung(RendererRung::SoftwareVulkanDevice), "software-vulkan");
    EXPECT_EQ(describeRendererRung(RendererRung::SharedMemoryRaster), "raster");
    EXPECT_EQ(parseRendererRung("vulkan"), std::nullopt);
    EXPECT_EQ(parseRendererRung(""), std::nullopt);
}

TEST(RendererLadderTest, TheLadderDescendsToRasterAndStopsThere) {
    EXPECT_EQ(nextRendererRung(RendererRung::PreferredVulkanDevice), RendererRung::AlternateVulkanDevice);
    EXPECT_EQ(nextRendererRung(RendererRung::AlternateVulkanDevice), RendererRung::SoftwareVulkanDevice);
    EXPECT_EQ(nextRendererRung(RendererRung::SoftwareVulkanDevice), RendererRung::SharedMemoryRaster);
    EXPECT_EQ(nextRendererRung(RendererRung::SharedMemoryRaster), std::nullopt);
}

TEST(RendererLadderTest, EveryStartReasonIsPrintable) {
    constexpr std::array<RendererStartReason, 7> reasons{RendererStartReason::NoPersistedDecision,
                                                         RendererStartReason::ForcedByOverride,
                                                         RendererStartReason::ResumedLastSuccessfulRung,
                                                         RendererStartReason::AdvancedAfterCrashBeforeFirstFrame,
                                                         RendererStartReason::RetriedTopRungAfterRasterCap,
                                                         RendererStartReason::RetriedTopRungOnNewDriver,
                                                         RendererStartReason::LadderExhausted};

    for (RendererStartReason reason : reasons) {
        EXPECT_FALSE(describeRendererStartReason(reason).empty());
    }
}

TEST(RendererLadderTest, TheDiscreteDeviceIsPreferredAndTheAlternateIsNeverTheSameDevice) {
    const std::vector<uint32_t> deviceTypes{kVulkanDeviceTypeIntegratedGpu, kVulkanDeviceTypeDiscreteGpu,
                                            kVulkanDeviceTypeCpu};

    EXPECT_EQ(selectVulkanDeviceForRung(deviceTypes, RendererRung::PreferredVulkanDevice), 1U);
    EXPECT_EQ(selectVulkanDeviceForRung(deviceTypes, RendererRung::AlternateVulkanDevice), 0U);
    EXPECT_EQ(selectVulkanDeviceForRung(deviceTypes, RendererRung::SoftwareVulkanDevice), 2U);
    EXPECT_EQ(selectVulkanDeviceForRung(deviceTypes, RendererRung::SharedMemoryRaster), std::nullopt);
}

TEST(RendererLadderTest, TiesKeepEnumerationOrder) {
    const std::vector<uint32_t> deviceTypes{kVulkanDeviceTypeOther, kVulkanDeviceTypeVirtualGpu};

    EXPECT_EQ(selectVulkanDeviceForRung(deviceTypes, RendererRung::PreferredVulkanDevice), 0U);
    EXPECT_EQ(selectVulkanDeviceForRung(deviceTypes, RendererRung::AlternateVulkanDevice), 1U);
}

TEST(RendererLadderTest, ARungTheMachineHasNoDeviceForSelectsNothing) {
    const std::vector<uint32_t> onlyOneDevice{kVulkanDeviceTypeIntegratedGpu};
    const std::vector<uint32_t> noDevices;

    EXPECT_EQ(selectVulkanDeviceForRung(onlyOneDevice, RendererRung::AlternateVulkanDevice), std::nullopt);
    EXPECT_EQ(selectVulkanDeviceForRung(onlyOneDevice, RendererRung::SoftwareVulkanDevice), std::nullopt);
    EXPECT_EQ(selectVulkanDeviceForRung(noDevices, RendererRung::PreferredVulkanDevice), std::nullopt);
}

TEST(RendererLadderTest, AFirstLaunchStartsAtTheTop) {
    const RendererStart start = rendererStartRung(std::nullopt, std::nullopt, kDriver);

    EXPECT_EQ(start.rung, RendererRung::PreferredVulkanDevice);
    EXPECT_EQ(start.reason, RendererStartReason::NoPersistedDecision);
}

TEST(RendererLadderTest, TheOverrideWinsOverAPersistedCrash) {
    const std::optional<RendererLadderRecord> crashed = makeRecord(RendererRung::PreferredVulkanDevice, 3, 0, kDriver);
    const RendererStart start = rendererStartRung(crashed, RendererRung::PreferredVulkanDevice, kDriver);

    EXPECT_EQ(start.rung, RendererRung::PreferredVulkanDevice);
    EXPECT_EQ(start.reason, RendererStartReason::ForcedByOverride);
}

TEST(RendererLadderTest, ARungThatPresentedAFrameIsResumed) {
    const std::optional<RendererLadderRecord> healthy = makeRecord(RendererRung::SoftwareVulkanDevice, 0, 0, kDriver);
    const RendererStart start = rendererStartRung(healthy, std::nullopt, kDriver);

    EXPECT_EQ(start.rung, RendererRung::SoftwareVulkanDevice);
    EXPECT_EQ(start.reason, RendererStartReason::ResumedLastSuccessfulRung);
}

TEST(RendererLadderTest, ACrashBeforeTheFirstFrameAdvancesAndTheFailedRungIsNeverRetried) {
    const std::optional<RendererLadderRecord> crashed = makeRecord(RendererRung::PreferredVulkanDevice, 1, 0, kDriver);
    const RendererStart start = rendererStartRung(crashed, std::nullopt, kDriver);

    EXPECT_EQ(start.rung, RendererRung::AlternateVulkanDevice);
    EXPECT_EQ(start.reason, RendererStartReason::AdvancedAfterCrashBeforeFirstFrame);
}

TEST(RendererLadderTest, EveryTierExhaustedLandsOnRasterAndStays) {
    const std::optional<RendererLadderRecord> crashedOnRaster =
        makeRecord(RendererRung::SharedMemoryRaster, 4, 0, kDriver);
    const RendererStart start = rendererStartRung(crashedOnRaster, std::nullopt, kDriver);

    EXPECT_EQ(start.rung, RendererRung::SharedMemoryRaster);
    EXPECT_EQ(start.reason, RendererStartReason::LadderExhausted);
}

TEST(RendererLadderTest, AMachineIsNotStuckOnRasterForever) {
    const std::optional<RendererLadderRecord> belowTheCap =
        makeRecord(RendererRung::SharedMemoryRaster, 0, kRasterSessionsBeforeRetryingTopRung - 1, kDriver);
    const std::optional<RendererLadderRecord> atTheCap =
        makeRecord(RendererRung::SharedMemoryRaster, 0, kRasterSessionsBeforeRetryingTopRung, kDriver);

    EXPECT_EQ(rendererStartRung(belowTheCap, std::nullopt, kDriver).rung, RendererRung::SharedMemoryRaster);
    EXPECT_EQ(rendererStartRung(belowTheCap, std::nullopt, kDriver).reason,
              RendererStartReason::ResumedLastSuccessfulRung);
    EXPECT_EQ(rendererStartRung(atTheCap, std::nullopt, kDriver).rung, RendererRung::PreferredVulkanDevice);
    EXPECT_EQ(rendererStartRung(atTheCap, std::nullopt, kDriver).reason,
              RendererStartReason::RetriedTopRungAfterRasterCap);
}

TEST(RendererLadderTest, ANewDriverInvalidatesThePersistedDecision) {
    const std::optional<RendererLadderRecord> crashedOnTheOldDriver =
        makeRecord(RendererRung::SharedMemoryRaster, 2, 0, kOtherDriver);
    const RendererStart start = rendererStartRung(crashedOnTheOldDriver, std::nullopt, kDriver);

    EXPECT_EQ(start.rung, RendererRung::PreferredVulkanDevice);
    EXPECT_EQ(start.reason, RendererStartReason::RetriedTopRungOnNewDriver);
}

TEST(RendererLadderTest, AnAttemptOnTheSameRungAccumulatesTheCrashCount) {
    const std::optional<RendererLadderRecord> persisted =
        makeRecord(RendererRung::PreferredVulkanDevice, 2, 7, kDriver);
    const RendererLadderRecord attempt = recordRendererAttempt(persisted, RendererRung::PreferredVulkanDevice, kDriver);

    EXPECT_EQ(attempt.rung, RendererRung::PreferredVulkanDevice);
    EXPECT_EQ(attempt.consecutiveCrashes, 3U);
    EXPECT_EQ(attempt.successfulRasterSessions, 7U);
    EXPECT_EQ(attempt.driverIdentity, kDriver);
}

TEST(RendererLadderTest, MovingDownTheLadderStartsTheCrashCountAgain) {
    const std::optional<RendererLadderRecord> persisted =
        makeRecord(RendererRung::PreferredVulkanDevice, 2, 7, kDriver);
    const RendererLadderRecord attempt = recordRendererAttempt(persisted, RendererRung::AlternateVulkanDevice, kDriver);

    EXPECT_EQ(attempt.consecutiveCrashes, 1U);
    EXPECT_EQ(attempt.successfulRasterSessions, 0U);
}

TEST(RendererLadderTest, ANewDriverDropsTheRasterSessionCountToo) {
    const std::optional<RendererLadderRecord> persisted =
        makeRecord(RendererRung::SharedMemoryRaster, 2, 7, kOtherDriver);
    const RendererLadderRecord attempt = recordRendererAttempt(persisted, RendererRung::SharedMemoryRaster, kDriver);

    EXPECT_EQ(attempt.consecutiveCrashes, 1U);
    EXPECT_EQ(attempt.successfulRasterSessions, 0U);
    EXPECT_EQ(attempt.driverIdentity, kDriver);
}

TEST(RendererLadderTest, TheFirstAttemptEverIsOneCrashUntilAFramePresents) {
    const RendererLadderRecord attempt =
        recordRendererAttempt(std::nullopt, RendererRung::PreferredVulkanDevice, kDriver);

    EXPECT_EQ(attempt.consecutiveCrashes, 1U);
    EXPECT_EQ(attempt.successfulRasterSessions, 0U);
}

TEST(RendererLadderTest, TheFirstPresentedFrameClearsTheCrashCount) {
    const RendererLadderRecord attempt =
        recordRendererAttempt(std::nullopt, RendererRung::PreferredVulkanDevice, kDriver);
    const RendererLadderRecord presented = recordFirstPresentedFrame(attempt);

    EXPECT_EQ(presented.consecutiveCrashes, 0U);
    EXPECT_EQ(presented.successfulRasterSessions, 0U);
    EXPECT_EQ(rendererStartRung(presented, std::nullopt, kDriver).rung, RendererRung::PreferredVulkanDevice);
}

TEST(RendererLadderTest, ARasterSessionCountsTowardsTheCapAndAVulkanSessionResetsIt) {
    const RendererLadderRecord rasterAttempt = recordRendererAttempt(
        makeRecord(RendererRung::SharedMemoryRaster, 0, 4, kDriver), RendererRung::SharedMemoryRaster, kDriver);
    const RendererLadderRecord vulkanAttempt = recordRendererAttempt(
        makeRecord(RendererRung::SharedMemoryRaster, 0, 4, kDriver), RendererRung::SoftwareVulkanDevice, kDriver);

    EXPECT_EQ(recordFirstPresentedFrame(rasterAttempt).successfulRasterSessions, 5U);
    EXPECT_EQ(recordFirstPresentedFrame(vulkanAttempt).successfulRasterSessions, 0U);
}

TEST(RendererLadderTest, ThePersistedRecordRoundTrips) {
    const RendererLadderRecord record = makeRecord(RendererRung::SoftwareVulkanDevice, 2, 5, kDriver);
    const std::optional<RendererLadderRecord> parsed = parseRendererLadderRecord(formatRendererLadderRecord(record));

    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->rung, record.rung);
    EXPECT_EQ(parsed->consecutiveCrashes, record.consecutiveCrashes);
    EXPECT_EQ(parsed->successfulRasterSessions, record.successfulRasterSessions);
    EXPECT_EQ(parsed->driverIdentity, record.driverIdentity);
}

TEST(RendererLadderTest, AnythingThisWriterDidNotWriteIsTreatedAsNoRecord) {
    EXPECT_EQ(parseRendererLadderRecord("rung=raster\ncrashes=0\nraster-sessions=0\ndriver=x"), std::nullopt);
    EXPECT_EQ(parseRendererLadderRecord("rung=raster\nnonsense\n"), std::nullopt);
    EXPECT_EQ(parseRendererLadderRecord("rung=nonsense\n"), std::nullopt);
    EXPECT_EQ(parseRendererLadderRecord("crashes=x\n"), std::nullopt);
    EXPECT_EQ(parseRendererLadderRecord("crashes=1x\n"), std::nullopt);
    EXPECT_EQ(parseRendererLadderRecord("raster-sessions=-1\n"), std::nullopt);
    EXPECT_EQ(parseRendererLadderRecord("raster-sessions=2 \n"), std::nullopt);
    EXPECT_EQ(parseRendererLadderRecord("unexpected=1\n"), std::nullopt);
    EXPECT_EQ(parseRendererLadderRecord("rung=raster\ncrashes=0\nraster-sessions=0\n"), std::nullopt);
    EXPECT_EQ(parseRendererLadderRecord("rung=raster\nraster-sessions=0\ndriver=x\n"), std::nullopt);
    EXPECT_EQ(parseRendererLadderRecord("rung=raster\ncrashes=0\ndriver=x\n"), std::nullopt);
    EXPECT_EQ(parseRendererLadderRecord("crashes=0\nraster-sessions=0\ndriver=x\n"), std::nullopt);
    EXPECT_EQ(parseRendererLadderRecord(""), std::nullopt);
}

TEST(RendererLadderTest, TheStateFileFollowsTheXdgBaseDirectorySpecification) {
    EXPECT_EQ(rendererLadderStatePath("/var/lib/state", "/home/person"),
              "/var/lib/state/react-native-linux/renderer-ladder");
    EXPECT_EQ(rendererLadderStatePath("relative/state", "/home/person"),
              "/home/person/.local/state/react-native-linux/renderer-ladder");
    EXPECT_EQ(rendererLadderStatePath("", ""), std::nullopt);
}

} // namespace
