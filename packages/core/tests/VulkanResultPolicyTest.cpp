#include "VulkanResultPolicy.h"

#include <array>
#include <cstdint>
#include <gtest/gtest.h>
#include <string_view>

namespace {

using react_native_linux::describeVulkanResult;
using react_native_linux::VulkanRecovery;
using react_native_linux::vulkanRecoveryFor;

struct PolicyRow {
    int32_t result;
    std::string_view name;
    VulkanRecovery recovery;
};

// The whole table, restated as data so the test reads as the decision it is asserting rather than as ten
// assertions that happen to agree with it. The values are the specification's, not the header's, which is the
// same independence the policy itself relies on; SkiaVulkanRenderer.cpp static_asserts them against vulkan_core.h.
constexpr std::array<PolicyRow, 10> kPolicyTable{{
    {0, "VK_SUCCESS", VulkanRecovery::Proceed},
    {1, "VK_NOT_READY", VulkanRecovery::RetryNextFrame},
    {2, "VK_TIMEOUT", VulkanRecovery::RetryNextFrame},
    {1'000'001'003, "VK_SUBOPTIMAL_KHR", VulkanRecovery::PresentThenRecreateSwapchain},
    {-1'000'001'004, "VK_ERROR_OUT_OF_DATE_KHR", VulkanRecovery::RecreateSwapchain},
    {-1'000'000'000, "VK_ERROR_SURFACE_LOST_KHR", VulkanRecovery::RecreateSurface},
    {-4, "VK_ERROR_DEVICE_LOST", VulkanRecovery::RecreateDevice},
    {-1, "VK_ERROR_OUT_OF_HOST_MEMORY", VulkanRecovery::FatalWithDiagnostic},
    {-2, "VK_ERROR_OUT_OF_DEVICE_MEMORY", VulkanRecovery::FatalWithDiagnostic},
    {-1'000'255'000, "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT", VulkanRecovery::FatalWithDiagnostic},
}};

TEST(VulkanResultPolicyTest, EveryTabledResultMapsToItsRecovery) {
    for (const PolicyRow& row : kPolicyTable) {
        EXPECT_EQ(vulkanRecoveryFor(row.result), row.recovery) << row.name;
    }
}

TEST(VulkanResultPolicyTest, EveryTabledResultIsNamedForADiagnostic) {
    for (const PolicyRow& row : kPolicyTable) {
        EXPECT_EQ(describeVulkanResult(row.result), row.name);
    }
}

TEST(VulkanResultPolicyTest, TheConstantsMatchTheValuesTheTableIsWrittenAgainst) {
    EXPECT_EQ(react_native_linux::kVulkanSuccess, 0);
    EXPECT_EQ(react_native_linux::kVulkanNotReady, 1);
    EXPECT_EQ(react_native_linux::kVulkanTimeout, 2);
    EXPECT_EQ(react_native_linux::kVulkanErrorOutOfHostMemory, -1);
    EXPECT_EQ(react_native_linux::kVulkanErrorOutOfDeviceMemory, -2);
    EXPECT_EQ(react_native_linux::kVulkanErrorDeviceLost, -4);
    EXPECT_EQ(react_native_linux::kVulkanErrorSurfaceLost, -1'000'000'000);
    EXPECT_EQ(react_native_linux::kVulkanSuboptimal, 1'000'001'003);
    EXPECT_EQ(react_native_linux::kVulkanErrorOutOfDate, -1'000'001'004);
    EXPECT_EQ(react_native_linux::kVulkanErrorFullScreenExclusiveModeLost, -1'000'255'000);
}

// VK_ERROR_UNKNOWN, and every future result this table has never been read against. Guessing a recovery for one
// would hide the driver contract it represents, so the default is the loud one.
TEST(VulkanResultPolicyTest, AnUntabledResultIsFatal) {
    EXPECT_EQ(vulkanRecoveryFor(-13), VulkanRecovery::FatalWithDiagnostic);
    EXPECT_EQ(vulkanRecoveryFor(3), VulkanRecovery::FatalWithDiagnostic);
}

TEST(VulkanResultPolicyTest, AnUntabledResultHasNoNameToPrint) {
    EXPECT_TRUE(describeVulkanResult(-13).empty());
    EXPECT_TRUE(describeVulkanResult(3).empty());
}

} // namespace
