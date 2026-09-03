#include "SkiaVulkanRenderer.h"

#include "include/core/SkAlphaType.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkStream.h"
#include "include/encode/SkPngEncoder.h"
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
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
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
constexpr uint64_t kAcquireTimeoutNanoseconds = 200'000'000;
constexpr size_t kBytesPerPixel = 4;
constexpr VkMemoryPropertyFlags kHostReadableMemory =
    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
constexpr VkImageSubresourceRange kColorSubresourceRange{
    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    .baseMipLevel = 0,
    .levelCount = 1,
    .baseArrayLayer = 0,
    .layerCount = 1,
};

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

void SkiaVulkanRenderer::captureNextFrame(std::string outputPath) { pendingCapturePath_ = std::move(outputPath); }

bool SkiaVulkanRenderer::hasPendingCapture() const noexcept { return !pendingCapturePath_.empty(); }

bool SkiaVulkanRenderer::drawFrame(WaylandWindow& window, const SceneDamage& frameDamage,
                                   const std::function<void(SkCanvas&, WindowSize, const SceneDamage&)>& paint) {
    if (backbuffers_.empty()) {
        return false;
    }

    // Before any early return: this frame's damage belongs to every image that is not about to be drawn, and the
    // paths that bail out rebuild the swapchain, which re-seeds every list with the full surface anyway.
    for (SceneDamage& pendingDamage : imageDamage_) {
        mergeDamage(pendingDamage, frameDamage);
    }

    currentBackbufferIndex_ = (currentBackbufferIndex_ + 1) % backbuffers_.size();
    Backbuffer& backbuffer = backbuffers_[currentBackbufferIndex_];

    // An idle frame owns its acquire semaphore instead of handing it to Skia, and a semaphore with a pending wait
    // cannot be destroyed. Coming back round to this backbuffer is the same retirement guarantee that lets its
    // render semaphore be reused, so it is also when the previous acquire semaphore can go.
    if (backbuffer.idleAcquireSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device_, backbuffer.idleAcquireSemaphore, nullptr);
        backbuffer.idleAcquireSemaphore = VK_NULL_HANDLE;
    }

    const VkSemaphoreCreateInfo semaphoreCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = nullptr, .flags = 0};
    VkSemaphore acquireSemaphore = VK_NULL_HANDLE;
    checkVulkanResult(vkCreateSemaphore(device_, &semaphoreCreateInfo, nullptr, &acquireSemaphore),
                      "vkCreateSemaphore");

    const VkResult acquireResult = vkAcquireNextImageKHR(device_, swapchain_, kAcquireTimeoutNanoseconds, acquireSemaphore,
                                                         VK_NULL_HANDLE, &backbuffer.imageIndex);

    if (acquireResult == VK_TIMEOUT || acquireResult == VK_NOT_READY) {
        vkDestroySemaphore(device_, acquireSemaphore, nullptr);

        return false;
    }

    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        vkDestroySemaphore(device_, acquireSemaphore, nullptr);
        createSwapchain();

        return false;
    }

    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        vkDestroySemaphore(device_, acquireSemaphore, nullptr);
        checkVulkanResult(acquireResult, "vkAcquireNextImageKHR");
    }

    SceneDamage& imageDamage = imageDamage_[backbuffer.imageIndex];

    // An empty list means this image already holds the current scene, so the frame costs one present and no
    // drawing at all. A pending capture is the one reason to draw anyway: it makes the screenshot come from a
    // full repaint through the ordinary path, which is also what orders the readback after real submitted work.
    const bool isIdleFrame = imageDamage.empty() && pendingCapturePath_.empty();

    if (isIdleFrame) {
        submitIdleFrame(acquireSemaphore, backbuffer);
    } else {
        SkSurface* imageSurface = imageSurfaces_[backbuffer.imageIndex].get();
        GrBackendSemaphore acquiredSemaphore = GrBackendSemaphores::MakeVk(acquireSemaphore);

        imageSurface->wait(1, &acquiredSemaphore);
        paint(*imageSurface->getCanvas(), swapchainSize_, imageDamage);
        imageDamage.clear();

        GrBackendSemaphore renderedSemaphore = GrBackendSemaphores::MakeVk(backbuffer.renderSemaphore);
        GrFlushInfo flushInfo;
        flushInfo.fNumSemaphores = 1;
        flushInfo.fSignalSemaphores = &renderedSemaphore;

        const skgpu::MutableTextureState presentState =
            skgpu::MutableTextureStates::MakeVulkan(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, queueFamilyIndex_);

        directContext_->flush(imageSurface, flushInfo, &presentState);
        directContext_->submit();
    }

    // The image is in VK_IMAGE_LAYOUT_PRESENT_SRC_KHR and still owned by this process until the present request
    // below hands it to the compositor, which makes this the only correct point to read it back.
    if (!pendingCapturePath_.empty()) {
        copyImageToPng(backbuffer.imageIndex, pendingCapturePath_);
        pendingCapturePath_.clear();
    }

    // vkQueuePresentKHR is what commits the wl_surface, and wl_surface.frame applies to the next commit on the
    // connection, so the callback for the next frame has to be requested before the present request is issued.
    window.requestFrameCallback();

    // wp_presentation.feedback attaches to the same pending content update, so it has to be requested on the same
    // side of the present. See *Frame timing* in docs/cpp-toolchain.md.
    window.requestPresentationFeedback();

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

    return true;
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

    images_.resize(imageCount);
    checkVulkanResult(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, images_.data()),
                      "vkGetSwapchainImagesKHR");

    swapchainFormat_ = imageFormat;
    swapchainImageUsage_ = imageUsage;
    imageSurfaces_.reserve(imageCount);

    for (VkImage image : images_) {
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

    // Every image of a fresh swapchain holds undefined pixels, so each one owes a full repaint before any partial
    // one is meaningful. This is also what makes a resize a full redraw without a special case for it.
    const facebook::react::Rect fullSurface{
        .origin = {},
        .size = {.width = static_cast<facebook::react::Float>(swapchainSize_.width),
                 .height = static_cast<facebook::react::Float>(swapchainSize_.height)}};

    imageDamage_.assign(imageCount, SceneDamage{fullSurface});

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
    imageDamage_.clear();
    images_.clear();

    for (Backbuffer& backbuffer : backbuffers_) {
        if (backbuffer.renderSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, backbuffer.renderSemaphore, nullptr);
        }

        if (backbuffer.idleAcquireSemaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, backbuffer.idleAcquireSemaphore, nullptr);
        }
    }

    backbuffers_.clear();
}

void SkiaVulkanRenderer::submitIdleFrame(VkSemaphore acquireSemaphore, Backbuffer& backbuffer) {
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    const VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &acquireSemaphore,
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 0,
        .pCommandBuffers = nullptr,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &backbuffer.renderSemaphore,
    };

    checkVulkanResult(vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit");

    backbuffer.idleAcquireSemaphore = acquireSemaphore;
}

uint32_t SkiaVulkanRenderer::findHostVisibleMemoryType(uint32_t acceptedMemoryTypes) const {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);

    for (uint32_t typeIndex = 0; typeIndex < memoryProperties.memoryTypeCount; ++typeIndex) {
        const bool isAccepted = (acceptedMemoryTypes & (1U << typeIndex)) != 0;
        const VkMemoryPropertyFlags typeFlags = memoryProperties.memoryTypes[typeIndex].propertyFlags;

        if (isAccepted && (typeFlags & kHostReadableMemory) == kHostReadableMemory) {
            return typeIndex;
        }
    }

    throw std::runtime_error("no host-visible coherent memory type accepts the screenshot buffer");
}

void SkiaVulkanRenderer::copyImageToPng(uint32_t imageIndex, const std::string& outputPath) {
    if ((swapchainImageUsage_ & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) == 0) {
        throw std::runtime_error("this surface's swapchain images are not transfer sources and cannot be read back");
    }

    const size_t rowBytes = static_cast<size_t>(swapchainSize_.width) * kBytesPerPixel;
    const VkDeviceSize pixelBufferSize = static_cast<VkDeviceSize>(rowBytes) * swapchainSize_.height;
    const VkBufferCreateInfo bufferCreateInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = pixelBufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    VkBuffer pixelBuffer = VK_NULL_HANDLE;
    checkVulkanResult(vkCreateBuffer(device_, &bufferCreateInfo, nullptr, &pixelBuffer), "vkCreateBuffer");

    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(device_, pixelBuffer, &memoryRequirements);

    const VkMemoryAllocateInfo memoryAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = memoryRequirements.size,
        .memoryTypeIndex = findHostVisibleMemoryType(memoryRequirements.memoryTypeBits),
    };
    VkDeviceMemory pixelMemory = VK_NULL_HANDLE;
    checkVulkanResult(vkAllocateMemory(device_, &memoryAllocateInfo, nullptr, &pixelMemory), "vkAllocateMemory");
    checkVulkanResult(vkBindBufferMemory(device_, pixelBuffer, pixelMemory, 0), "vkBindBufferMemory");

    const VkCommandPoolCreateInfo commandPoolCreateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = queueFamilyIndex_,
    };
    VkCommandPool commandPool = VK_NULL_HANDLE;
    checkVulkanResult(vkCreateCommandPool(device_, &commandPoolCreateInfo, nullptr, &commandPool),
                      "vkCreateCommandPool");

    const VkCommandBufferAllocateInfo commandBufferAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = commandPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    checkVulkanResult(vkAllocateCommandBuffers(device_, &commandBufferAllocateInfo, &commandBuffer),
                      "vkAllocateCommandBuffers");

    const VkCommandBufferBeginInfo commandBufferBeginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .pInheritanceInfo = nullptr,
    };
    checkVulkanResult(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo), "vkBeginCommandBuffer");

    // A barrier's first synchronisation scope covers everything already submitted to the same queue, which is what
    // orders this copy after Skia's rendering without adding a second semaphore to the frame.
    VkImageMemoryBarrier layoutBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .pNext = nullptr,
        .srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT,
        .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = images_[imageIndex],
        .subresourceRange = kColorSubresourceRange,
    };
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &layoutBarrier);

    const VkBufferImageCopy copyRegion{
        .bufferOffset = 0,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                             .mipLevel = 0,
                             .baseArrayLayer = 0,
                             .layerCount = 1},
        .imageOffset = {.x = 0, .y = 0, .z = 0},
        .imageExtent = {.width = swapchainSize_.width, .height = swapchainSize_.height, .depth = 1},
    };
    vkCmdCopyImageToBuffer(commandBuffer, images_[imageIndex], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, pixelBuffer, 1,
                           &copyRegion);

    // Restored to exactly the layout Skia recorded, so both the present below and Skia's own tracking stay valid.
    layoutBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    layoutBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    layoutBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    layoutBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0,
                         nullptr, 0, nullptr, 1, &layoutBarrier);

    checkVulkanResult(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");

    const VkFenceCreateInfo fenceCreateInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .pNext = nullptr, .flags = 0};
    VkFence copyFence = VK_NULL_HANDLE;
    checkVulkanResult(vkCreateFence(device_, &fenceCreateInfo, nullptr, &copyFence), "vkCreateFence");

    const VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 1,
        .pCommandBuffers = &commandBuffer,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };
    checkVulkanResult(vkQueueSubmit(queue_, 1, &submitInfo, copyFence), "vkQueueSubmit");
    checkVulkanResult(vkWaitForFences(device_, 1, &copyFence, VK_TRUE, UINT64_MAX), "vkWaitForFences");

    void* mappedPixels = nullptr;
    checkVulkanResult(vkMapMemory(device_, pixelMemory, 0, VK_WHOLE_SIZE, 0, &mappedPixels), "vkMapMemory");

    // The swapchain format decides the channel order in the mapped buffer, and readPixels is what turns a BGRA
    // swapchain into the RGBA a PNG carries, so the golden never depends on which format the surface offered.
    const SkImageInfo swapchainInfo =
        SkImageInfo::Make(static_cast<int>(swapchainSize_.width), static_cast<int>(swapchainSize_.height),
                          colorTypeForFormat(swapchainFormat_), kPremul_SkAlphaType);
    const SkPixmap swapchainPixels(swapchainInfo, mappedPixels, rowBytes);
    std::vector<uint8_t> encodedBytes(static_cast<size_t>(pixelBufferSize));
    const SkPixmap encodedPixels(swapchainInfo.makeColorType(kRGBA_8888_SkColorType), encodedBytes.data(), rowBytes);
    const bool isConverted = swapchainPixels.readPixels(encodedPixels);

    SkFILEWStream pngFile(outputPath.c_str());
    const bool isWritten =
        isConverted && pngFile.isValid() && SkPngEncoder::Encode(&pngFile, encodedPixels, SkPngEncoder::Options{});

    vkUnmapMemory(device_, pixelMemory);
    vkDestroyFence(device_, copyFence, nullptr);
    vkDestroyCommandPool(device_, commandPool, nullptr);
    vkFreeMemory(device_, pixelMemory, nullptr);
    vkDestroyBuffer(device_, pixelBuffer, nullptr);

    if (!isWritten) {
        throw std::runtime_error("could not write the screenshot to " + outputPath);
    }
}

} // namespace react_native_linux
