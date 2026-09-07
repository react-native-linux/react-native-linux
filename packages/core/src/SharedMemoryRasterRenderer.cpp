#include "SharedMemoryRasterRenderer.h"

#include "TextRasterizationPolicySkia.h"
#include "include/core/SkAlphaType.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorType.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/core/SkSurfaceProps.h"
#include "include/encode/SkPngEncoder.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <unistd.h>
#include <utility>
#include <vector>
#include <wayland-client-protocol.h>

namespace react_native_linux {

namespace {

constexpr size_t kBytesPerPixel = 4;

// wl_shm's WL_SHM_FORMAT_XRGB8888 is a 32-bit little-endian word, which is the byte order Skia calls BGRA.
constexpr SkColorType kBufferColorType = kBGRA_8888_SkColorType;

void markBufferReleased(void* data, wl_buffer* /*waylandBuffer*/) { *static_cast<bool*>(data) = false; }

const wl_buffer_listener kBufferListener{.release = markBufferReleased};

size_t rowBytesFor(WindowSize size) { return static_cast<size_t>(size.width) * kBytesPerPixel; }

SkImageInfo imageInfoFor(WindowSize size) {
    return SkImageInfo::Make(static_cast<int>(size.width), static_cast<int>(size.height), kBufferColorType,
                             kOpaque_SkAlphaType);
}

SceneDamage fullSurfaceDamage(WindowSize size) {
    return SceneDamage{facebook::react::Rect{.origin = {},
                                             .size = {.width = static_cast<facebook::react::Float>(size.width),
                                                      .height = static_cast<facebook::react::Float>(size.height)}}};
}

} // namespace

SharedMemoryRasterRenderer::SharedMemoryRasterRenderer(wl_shm* sharedMemory, wl_surface* waylandSurface,
                                                       WindowSize initialSize)
    : sharedMemory_(sharedMemory), waylandSurface_(waylandSurface), size_(initialSize) {
    if (sharedMemory_ == nullptr) {
        throw std::runtime_error("compositor does not advertise wl_shm");
    }

    createPool(initialSize);
}

SharedMemoryRasterRenderer::~SharedMemoryRasterRenderer() noexcept { destroyPool(); }

void SharedMemoryRasterRenderer::resize(WindowSize size) {
    if (size.width == size_.width && size.height == size_.height) {
        return;
    }

    destroyPool();
    size_ = size;
    createPool(size);
}

void SharedMemoryRasterRenderer::captureNextFrame(std::string outputPath) {
    pendingCapturePath_ = std::move(outputPath);
}

bool SharedMemoryRasterRenderer::hasPendingCapture() const noexcept { return !pendingCapturePath_.empty(); }

bool SharedMemoryRasterRenderer::drawFrame(
    WaylandWindow& window, const SceneDamage& /*frameDamage*/,
    const std::function<void(SkCanvas&, WindowSize, const SceneDamage&)>& paint) {
    // Cleared unconditionally so a discard is never left pending behind a frame that bailed out, exactly as
    // SkiaVulkanRenderer::drawFrame does with the same flag. Every frame here is a full repaint, so a discarded
    // content update needs no recovery beyond the next frame.
    window.takeContentUpdateDiscarded();

    if (!window.isConfigureAcknowledged()) {
        return false;
    }

    Buffer* free = nullptr;

    for (Buffer& buffer : buffers_) {
        if (!buffer.isHeldByCompositor) {
            free = &buffer;

            break;
        }
    }

    if (free == nullptr) {
        // Both buffers are still held by the compositor. Re-arming the frame callback resets
        // frameCallbackFired_, so WaylandWindow::waitForRedraw actually dispatches the connection instead of
        // returning immediately on a callback that already fired for a frame whose buffer has not been released
        // yet — otherwise the pending wl_buffer.release event is never read and this call spins forever.
        window.requestFrameCallback();

        return false;
    }

    // Every frame repaints the whole surface: with two alternating buffers, the one this frame does not touch is
    // an unknown number of frames stale, and the pixels it holds cannot be reasoned about without buffer-age
    // bookkeeping this rung deliberately does not carry.
    paint(*free->surface->getCanvas(), size_, fullSurfaceDamage(size_));

    if (!pendingCapturePath_.empty()) {
        writeCapture(*free);
        pendingCapturePath_.clear();
    }

    // wl_surface.frame applies to the next commit on the connection, so both requests have to be issued before
    // the commit that carries this frame's content, the same ordering the Vulkan path keeps around the present.
    window.requestFrameCallback();
    window.requestPresentationFeedback();

    wl_surface_attach(waylandSurface_, free->waylandBuffer, 0, 0);
    wl_surface_damage(waylandSurface_, 0, 0, static_cast<int32_t>(size_.width), static_cast<int32_t>(size_.height));
    wl_surface_commit(waylandSurface_);

    free->isHeldByCompositor = true;

    return true;
}

void SharedMemoryRasterRenderer::createPool(WindowSize size) {
    const size_t bufferSize = rowBytesFor(size) * size.height;

    poolSize_ = bufferSize * buffers_.size();
    poolFileDescriptor_ = memfd_create("rnl-raster", MFD_CLOEXEC);

    if (poolFileDescriptor_ < 0) {
        throw std::runtime_error("memfd_create for the wl_shm pool failed");
    }

    if (ftruncate(poolFileDescriptor_, static_cast<off_t>(poolSize_)) != 0) {
        close(poolFileDescriptor_);
        poolFileDescriptor_ = -1;

        throw std::runtime_error("ftruncate for the wl_shm pool failed");
    }

    poolPixels_ = mmap(nullptr, poolSize_, PROT_READ | PROT_WRITE, MAP_SHARED, poolFileDescriptor_, 0);

    if (poolPixels_ == MAP_FAILED) {
        poolPixels_ = nullptr;
        close(poolFileDescriptor_);
        poolFileDescriptor_ = -1;

        throw std::runtime_error("mmap for the wl_shm pool failed");
    }

    pool_ = wl_shm_create_pool(sharedMemory_, poolFileDescriptor_, static_cast<int32_t>(poolSize_));

    for (size_t index = 0; index < buffers_.size(); ++index) {
        Buffer& buffer = buffers_[index];

        buffer.pixels = static_cast<std::byte*>(poolPixels_) + (index * bufferSize);
        buffer.isHeldByCompositor = false;
        buffer.waylandBuffer = wl_shm_pool_create_buffer(
            pool_, static_cast<int32_t>(index * bufferSize), static_cast<int32_t>(size.width),
            static_cast<int32_t>(size.height), static_cast<int32_t>(rowBytesFor(size)), WL_SHM_FORMAT_XRGB8888);
        const SkSurfaceProps surfaceProps = skSurfacePropsFor(textRasterizationPolicy());

        buffer.surface = SkSurfaces::WrapPixels(imageInfoFor(size), buffer.pixels, rowBytesFor(size), &surfaceProps);

        wl_buffer_add_listener(buffer.waylandBuffer, &kBufferListener, &buffer.isHeldByCompositor);
    }
}

void SharedMemoryRasterRenderer::destroyPool() noexcept {
    for (Buffer& buffer : buffers_) {
        buffer.surface.reset();

        if (buffer.waylandBuffer != nullptr) {
            wl_buffer_destroy(buffer.waylandBuffer);
            buffer.waylandBuffer = nullptr;
        }

        buffer.pixels = nullptr;
        buffer.isHeldByCompositor = false;
    }

    if (pool_ != nullptr) {
        wl_shm_pool_destroy(pool_);
        pool_ = nullptr;
    }

    if (poolPixels_ != nullptr) {
        munmap(poolPixels_, poolSize_);
        poolPixels_ = nullptr;
    }

    if (poolFileDescriptor_ >= 0) {
        close(poolFileDescriptor_);
        poolFileDescriptor_ = -1;
    }

    poolSize_ = 0;
}

void SharedMemoryRasterRenderer::writeCapture(const Buffer& buffer) {
    const SkImageInfo bufferInfo = imageInfoFor(size_);
    const SkPixmap bufferPixels(bufferInfo, buffer.pixels, rowBytesFor(size_));
    std::vector<uint8_t> encodedBytes(rowBytesFor(size_) * size_.height);
    const SkPixmap encodedPixels(bufferInfo.makeColorType(kRGBA_8888_SkColorType), encodedBytes.data(),
                                 rowBytesFor(size_));

    SkFILEWStream pngFile(pendingCapturePath_.c_str());

    if (!bufferPixels.readPixels(encodedPixels) || !pngFile.isValid() ||
        !SkPngEncoder::Encode(&pngFile, encodedPixels, SkPngEncoder::Options{})) {
        throw std::runtime_error("the raster frame could not be written to " + pendingCapturePath_);
    }
}

} // namespace react_native_linux
