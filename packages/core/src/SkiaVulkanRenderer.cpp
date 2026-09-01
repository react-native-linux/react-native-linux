#include "SkiaVulkanRenderer.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkColorType.h"
#include "include/gpu/MutableTextureState.h"
#include "include/gpu/ganesh/GrBackendSemaphore.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/GrTypes.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/vk/GrVkBackendSemaphore.h"
#include "include/gpu/ganesh/vk/GrVkBackendSurface.h"
#include "include/gpu/ganesh/vk/GrVkDirectContext.h"
#include "include/gpu/ganesh/vk/GrVkTypes.h"
#include "include/gpu/GpuTypes.h"
#include "include/gpu/vk/VulkanBackendContext.h"
#include "include/gpu/vk/VulkanMemoryAllocator.h"

namespace skgpu {
enum class ThreadSafe : bool { kNo = false, kYes = true };

namespace VulkanMemoryAllocators {
sk_sp<VulkanMemoryAllocator> Make(const VulkanBackendContext& backendContext, ThreadSafe threadSafe);
}
}
#include "include/gpu/vk/VulkanMutableTextureState.h"

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <vulkan/vulkan_wayland.h>

namespace react_native_linux {

namespace {

constexpr std::array<const char*, 2> kInstanceExtensions{
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME,
};

constexpr std::array<const char*, 1> kDeviceExtensions{
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

constexpr uint32_t kUndefinedExtent = std::numeric_limits<uint32_t>::max();
constexpr uint32_t kSwapchainImageMargin = 2;

void checkVulkanResult(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " + std::to_string(result));
    }
}

SkColorType colorTypeForFormat(VkFormat format) {
    if (format == VK_FORMAT_B8G8R8A8_UNORM) {
        return kBGRA_8888_SkColorType;
    }

    if (format == VK_FORMAT_R8G8B8A8_UNORM) {
        return kRGBA_8888_SkColorType;
    }

    return kUnknown_SkColorType;
}

PFN_vkVoidFunction resolveVulkanProc(const char* procedureName, VkInstance instance, VkDevice device) {
    if (device != VK_NULL_HANDLE) {
        return vkGetDeviceProcAddr(device, procedureName);
    }

    return vkGetInstanceProcAddr(instance, procedureName);
}

} // namespace

SkiaVulkanRenderer::SkiaVulkanRenderer(wl_display* waylandDisplay, wl_surface* waylandSurface, WindowSize initialSize)
    : requestedSize_(initialSize), swapchainSize_(initialSize) {
    createInstance();
    createWaylandSurface(waylandDisplay, waylandSurface);
    selectPhysicalDevice();
    createDevice();
    createDirectContext();
    createSwapchain();
}

SkiaVulkanRenderer::~SkiaVulkanRenderer() noexcept {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }

    destroyBackbuffers();
    directContext_.reset();

    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    }

    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
    }

    if (vulkanSurface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, vulkanSurface_, nullptr);
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
}

void SkiaVulkanRenderer::resize(WindowSize size) {
    if (size.width == swapchainSize_.width && size.height == swapchainSize_.height) {
        return;
    }

    requestedSize_ = size;
    createSwapchain();
}

void SkiaVulkanRenderer::drawFrame(WaylandWindow& window, const std::function<void(SkCanvas&, WindowSize)>& paint) {
    if (backbuffers_.empty()) {
        return;
    }

    currentBackbufferIndex_ = (currentBackbufferIndex_ + 1) % backbuffers_.size();
    Backbuffer& backbuffer = backbuffers_[currentBackbufferIndex_];

    const VkSemaphoreCreateInfo semaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
    VkSemaphore acquireSemaphore = VK_NULL_HANDLE;
    checkVulkanResult(vkCreateSemaphore(device_, &semaphoreCreateInfo, nullptr, &acquireSemaphore),
                      "vkCreateSemaphore");

    const VkResult acquireResult = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, acquireSemaphore,
                                                         VK_NULL_HANDLE, &backbuffer.imageIndex);

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        vkDestroySemaphore(device_, acquireSemaphore, nullptr);
        createSwapchain();

        return;
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        vkDestroySemaphore(device_, acquireSemaphore, nullptr);
        checkVulkanResult(acquireResult, "vkAcquireNextImageKHR");
    }

    SkSurface* imageSurface = imageSurfaces_[backbuffer.imageIndex].get();
    GrBackendSemaphore acquiredSemaphore = GrBackendSemaphores::MakeVk(acquireSemaphore);

    imageSurface->wait(1, &acquiredSemaphore);
    paint(*imageSurface->getCanvas(), swapchainSize_);

    GrBackendSemaphore renderedSemaphore = GrBackendSemaphores::MakeVk(backbuffer.renderSemaphore);
    GrFlushInfo flushInfo;
    flushInfo.fNumSemaphores = 1;
    flushInfo.fSignalSemaphores = &renderedSemaphore;

    const skgpu::MutableTextureState presentState =
        skgpu::MutableTextureStates::MakeVulkan(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, queueFamilyIndex_);

    directContext_->flush(imageSurface, flushInfo, &presentState);
    directContext_->submit();

    // vkQueuePresentKHR is what commits the wl_surface, and wl_surface.frame applies to the next commit on the
    // connection, so the callback for the next frame has to be requested before the present request is issued.
    window.requestFrameCallback();

    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &backbuffer.renderSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &swapchain_,
        .pImageIndices = &backbuffer.imageIndex,
        .pResults = nullptr,
    };

    const VkResult presentResult = vkQueuePresentKHR(queue_, &presentInfo);

    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        createSwapchain();
    } else {
        checkVulkanResult(presentResult, "vkQueuePresentKHR");
    }
}

void SkiaVulkanRenderer::createInstance() {
    const VkApplicationInfo applicationInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = "react-native-linux",
        .applicationVersion = 0,
        .pEngineName = "react-native-linux",
        .engineVersion = 0,
        .apiVersion = VK_API_VERSION_1_1,
    };
    const VkInstanceCreateInfo instanceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(kInstanceExtensions.size()),
        .ppEnabledExtensionNames = kInstanceExtensions.data(),
    };

    checkVulkanResult(vkCreateInstance(&instanceCreateInfo, nullptr, &instance_), "vkCreateInstance");
}

void SkiaVulkanRenderer::createWaylandSurface(wl_display* waylandDisplay, wl_surface* waylandSurface) {
    const VkWaylandSurfaceCreateInfoKHR surfaceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .display = waylandDisplay,
        .surface = waylandSurface,
    };

    checkVulkanResult(vkCreateWaylandSurfaceKHR(instance_, &surfaceCreateInfo, nullptr, &vulkanSurface_),
                      "vkCreateWaylandSurfaceKHR");
}

void SkiaVulkanRenderer::selectPhysicalDevice() {
    uint32_t physicalDeviceCount = 0;
    checkVulkanResult(vkEnumeratePhysicalDevices(instance_, &physicalDeviceCount, nullptr),
                      "vkEnumeratePhysicalDevices");

    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    checkVulkanResult(vkEnumeratePhysicalDevices(instance_, &physicalDeviceCount, physicalDevices.data()),
                      "vkEnumeratePhysicalDevices");

    bool selectedIsDiscrete = false;

    for (VkPhysicalDevice candidate : physicalDevices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);

        if (properties.apiVersion < VK_API_VERSION_1_1) {
            continue;
        }

        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);

        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, queueFamilies.data());

        for (uint32_t familyIndex = 0; familyIndex < queueFamilyCount; ++familyIndex) {
            if ((queueFamilies[familyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
                continue;
            }

            VkBool32 presentSupported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(candidate, familyIndex, vulkanSurface_, &presentSupported);

            if (presentSupported == VK_FALSE) {
                continue;
            }

            const bool candidateIsDiscrete = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;

            if (physicalDevice_ == VK_NULL_HANDLE || (candidateIsDiscrete && !selectedIsDiscrete)) {
                physicalDevice_ = candidate;
                queueFamilyIndex_ = familyIndex;
                selectedIsDiscrete = candidateIsDiscrete;
            }

            break;
        }
    }

    if (physicalDevice_ == VK_NULL_HANDLE) {
        throw std::runtime_error("no Vulkan 1.1 device presents to this Wayland surface");
    }
}

void SkiaVulkanRenderer::createDevice() {
    vkGetPhysicalDeviceFeatures(physicalDevice_, &deviceFeatures_);
    deviceFeatures_.robustBufferAccess = VK_FALSE;

    const float queuePriority = 1.0F;
    const VkDeviceQueueCreateInfo queueCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueFamilyIndex = queueFamilyIndex_,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };
    const VkDeviceCreateInfo deviceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueCreateInfo,
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = static_cast<uint32_t>(kDeviceExtensions.size()),
        .ppEnabledExtensionNames = kDeviceExtensions.data(),
        .pEnabledFeatures = &deviceFeatures_,
    };

    checkVulkanResult(vkCreateDevice(physicalDevice_, &deviceCreateInfo, nullptr, &device_), "vkCreateDevice");
    vkGetDeviceQueue(device_, queueFamilyIndex_, 0, &queue_);
}

void SkiaVulkanRenderer::createDirectContext() {
    extensions_.init(resolveVulkanProc, instance_, physicalDevice_, static_cast<uint32_t>(kInstanceExtensions.size()),
                     kInstanceExtensions.data(), static_cast<uint32_t>(kDeviceExtensions.size()),
                     kDeviceExtensions.data());

    skgpu::VulkanBackendContext backendContext;
    backendContext.fInstance = instance_;
    backendContext.fPhysicalDevice = physicalDevice_;
    backendContext.fDevice = device_;
    backendContext.fQueue = queue_;
    backendContext.fGraphicsQueueIndex = queueFamilyIndex_;
    backendContext.fMaxAPIVersion = VK_API_VERSION_1_1;
    backendContext.fVkExtensions = &extensions_;
    backendContext.fDeviceFeatures = &deviceFeatures_;
    backendContext.fGetProc = resolveVulkanProc;

    const sk_sp<skgpu::VulkanMemoryAllocator> memoryAllocator =
        skgpu::VulkanMemoryAllocators::Make(backendContext, skgpu::ThreadSafe::kNo);

    if (memoryAllocator == nullptr) {
        throw std::runtime_error("VulkanMemoryAllocators::Make returned no allocator");
    }

    backendContext.fMemoryAllocator = memoryAllocator;

    directContext_ = GrDirectContexts::MakeVulkan(backendContext);

    if (directContext_ == nullptr) {
        throw std::runtime_error("GrDirectContexts::MakeVulkan returned no context");
    }
}

void SkiaVulkanRenderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities{};
    checkVulkanResult(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice_, vulkanSurface_, &capabilities),
                      "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    VkExtent2D extent = capabilities.currentExtent;

    if (extent.width == kUndefinedExtent || extent.height == kUndefinedExtent) {
        extent.width =
            std::clamp(requestedSize_.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height =
            std::clamp(requestedSize_.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    if (extent.width == 0 || extent.height == 0) {
        return;
    }

    uint32_t surfaceFormatCount = 0;
    checkVulkanResult(
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, vulkanSurface_, &surfaceFormatCount, nullptr),
        "vkGetPhysicalDeviceSurfaceFormatsKHR");

    std::vector<VkSurfaceFormatKHR> surfaceFormats(surfaceFormatCount);
    checkVulkanResult(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice_, vulkanSurface_, &surfaceFormatCount,
                                                           surfaceFormats.data()),
                      "vkGetPhysicalDeviceSurfaceFormatsKHR");

    VkSurfaceFormatKHR selectedFormat{VK_FORMAT_UNDEFINED, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};

    for (const VkSurfaceFormatKHR& candidate : surfaceFormats) {
        if (candidate.colorSpace != VK_COLOR_SPACE_SRGB_NONLINEAR_KHR ||
            colorTypeForFormat(candidate.format) == kUnknown_SkColorType) {
            continue;
        }

        selectedFormat = candidate;

        if (candidate.format == VK_FORMAT_B8G8R8A8_UNORM) {
            break;
        }
    }

    if (selectedFormat.format == VK_FORMAT_UNDEFINED) {
        throw std::runtime_error("no 8-bit sRGB swapchain format is available on this surface");
    }

    VkImageUsageFlags imageUsage =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imageUsage &= capabilities.supportedUsageFlags;

    if ((imageUsage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) == 0) {
        throw std::runtime_error("swapchain images cannot be used as colour attachments");
    }

    uint32_t imageCount = capabilities.minImageCount + kSwapchainImageMargin;

    if (capabilities.maxImageCount > 0) {
        imageCount = std::min(imageCount, capabilities.maxImageCount);
    }

    const VkCompositeAlphaFlagBitsKHR compositeAlpha =
        (capabilities.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0
            ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
            : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;

    VkSwapchainKHR previousSwapchain = swapchain_;
    const VkSwapchainCreateInfoKHR swapchainCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = vulkanSurface_,
        .minImageCount = imageCount,
        .imageFormat = selectedFormat.format,
        .imageColorSpace = selectedFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = imageUsage,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = compositeAlpha,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = previousSwapchain,
    };

    checkVulkanResult(vkCreateSwapchainKHR(device_, &swapchainCreateInfo, nullptr, &swapchain_),
                      "vkCreateSwapchainKHR");

    if (previousSwapchain != VK_NULL_HANDLE) {
        destroyBackbuffers();
        vkDestroySwapchainKHR(device_, previousSwapchain, nullptr);
    }

    swapchainSize_ = WindowSize{extent.width, extent.height};
    createBackbuffers(selectedFormat.format, imageUsage);
}

void SkiaVulkanRenderer::createBackbuffers(VkFormat imageFormat, VkImageUsageFlags imageUsage) {
    uint32_t imageCount = 0;
    checkVulkanResult(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr), "vkGetSwapchainImagesKHR");

    std::vector<VkImage> images(imageCount);
    checkVulkanResult(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, images.data()),
                      "vkGetSwapchainImagesKHR");

    imageSurfaces_.reserve(imageCount);

    for (VkImage image : images) {
        GrVkImageInfo imageInfo;
        imageInfo.fImage = image;
        imageInfo.fImageTiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.fImageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.fFormat = imageFormat;
        imageInfo.fImageUsageFlags = imageUsage;
        imageInfo.fSampleCount = 1;
        imageInfo.fLevelCount = 1;
        imageInfo.fCurrentQueueFamily = queueFamilyIndex_;
        imageInfo.fSharingMode = VK_SHARING_MODE_EXCLUSIVE;

        const GrBackendRenderTarget renderTarget = GrBackendRenderTargets::MakeVk(
            static_cast<int>(swapchainSize_.width), static_cast<int>(swapchainSize_.height), imageInfo);
        sk_sp<SkSurface> imageSurface =
            SkSurfaces::WrapBackendRenderTarget(directContext_.get(), renderTarget, kTopLeft_GrSurfaceOrigin,
                                                colorTypeForFormat(imageFormat), nullptr, nullptr);

        if (imageSurface == nullptr) {
            throw std::runtime_error("SkSurfaces::WrapBackendRenderTarget returned no surface");
        }

        imageSurfaces_.push_back(std::move(imageSurface));
    }

    // Skia's own VulkanWindowContext keeps one more backbuffer than there are swapchain images so a command buffer
    // has a chance to retire before its render semaphore is reused.
    backbuffers_.resize(imageCount + 1);

    for (Backbuffer& backbuffer : backbuffers_) {
        const VkSemaphoreCreateInfo semaphoreCreateInfo{
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
        checkVulkanResult(vkCreateSemaphore(device_, &semaphoreCreateInfo, nullptr, &backbuffer.renderSemaphore),
                          "vkCreateSemaphore");
    }

    currentBackbufferIndex_ = backbuffers_.size() - 1;
}

void SkiaVulkanRenderer::destroyBackbuffers() noexcept {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }

    vkDeviceWaitIdle(device_);
    imageSurfaces_.clear();

    for (Backbuffer& backbuffer : backbuffers_) {
        if (backbuffer.renderSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, backbuffer.renderSemaphore, nullptr);
        }
    }

    backbuffers_.clear();
}

} // namespace react_native_linux
