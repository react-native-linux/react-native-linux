#include "VulkanResultPolicy.h"

namespace react_native_linux {

VulkanRecovery vulkanRecoveryFor(int32_t result) noexcept {
    switch (result) {
    case kVulkanSuccess:
        return VulkanRecovery::Proceed;
    case kVulkanSuboptimal:
        return VulkanRecovery::PresentThenRecreateSwapchain;
    case kVulkanErrorOutOfDate:
        return VulkanRecovery::RecreateSwapchain;
    case kVulkanErrorSurfaceLost:
        return VulkanRecovery::RecreateSurface;
    case kVulkanTimeout:
    case kVulkanNotReady:
        return VulkanRecovery::RetryNextFrame;
    default:
        return VulkanRecovery::FatalWithDiagnostic;
    }
}

std::string_view describeVulkanResult(int32_t result) noexcept {
    switch (result) {
    case kVulkanSuccess:
        return "VK_SUCCESS";
    case kVulkanNotReady:
        return "VK_NOT_READY";
    case kVulkanTimeout:
        return "VK_TIMEOUT";
    case kVulkanErrorOutOfHostMemory:
        return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case kVulkanErrorOutOfDeviceMemory:
        return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case kVulkanErrorDeviceLost:
        return "VK_ERROR_DEVICE_LOST";
    case kVulkanErrorSurfaceLost:
        return "VK_ERROR_SURFACE_LOST_KHR";
    case kVulkanSuboptimal:
        return "VK_SUBOPTIMAL_KHR";
    case kVulkanErrorOutOfDate:
        return "VK_ERROR_OUT_OF_DATE_KHR";
    case kVulkanErrorFullScreenExclusiveModeLost:
        return "VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT";
    default:
        return {};
    }
}

} // namespace react_native_linux
