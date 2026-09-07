#pragma once

#include "RetainedScene.h"
#include "WaylandWindow.h"
#include "WindowRenderer.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSurface.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

struct wl_buffer;
struct wl_shm;
struct wl_shm_pool;

namespace react_native_linux {

/**
 * The bottom rung of `RendererLadder.h`: Skia's CPU backend painting into a `wl_shm` buffer.
 *
 * It exists to be the rung that always works. There is no `VkInstance`, no driver and no GPU on this path, so a
 * machine whose every Vulkan rung crashes — the state electron#32317's reporters are left in, where a GPU fault
 * turns into an installation that never shows a window again — still gets pixels. It is the last resort and is
 * written as one: correctness over throughput, no partial redraw, no buffer-age bookkeeping.
 *
 * Two buffers in one `wl_shm_pool` alternate, because a client may not draw into a buffer the compositor still
 * holds. `wl_buffer.release` is what says a buffer is free again, and a frame that finds neither free presents
 * nothing and returns false rather than painting over pixels the compositor is reading. The pool is rebuilt on
 * every resize, which is also what makes each buffer's contents undefined afterwards and therefore why every
 * frame repaints the whole surface: with two buffers, a frame's damage would have to be replayed into the one it
 * did not touch, and the partial-redraw bookkeeping that would need is not worth carrying on the rung whose only
 * job is to work at all.
 *
 * Threading contract: as `WindowRenderer`'s.
 */
class SharedMemoryRasterRenderer final : public WindowRenderer {
public:
    SharedMemoryRasterRenderer(wl_shm* sharedMemory, wl_surface* waylandSurface, WindowSize initialSize);
    SharedMemoryRasterRenderer(const SharedMemoryRasterRenderer&) = delete;
    SharedMemoryRasterRenderer(SharedMemoryRasterRenderer&&) = delete;
    SharedMemoryRasterRenderer& operator=(const SharedMemoryRasterRenderer&) = delete;
    SharedMemoryRasterRenderer& operator=(SharedMemoryRasterRenderer&&) = delete;
    ~SharedMemoryRasterRenderer() noexcept override;

    bool drawFrame(WaylandWindow& window, const SceneDamage& frameDamage,
                   const std::function<void(SkCanvas&, WindowSize, const SceneDamage&)>& paint) override;
    void resize(WindowSize size) override;
    [[nodiscard]] bool hasPendingCapture() const noexcept override;
    void captureNextFrame(std::string outputPath) override;

private:
    struct Buffer {
        wl_buffer* waylandBuffer{nullptr};
        sk_sp<SkSurface> surface;
        std::byte* pixels{nullptr};
        bool isHeldByCompositor{false};
    };

    void createPool(WindowSize size);
    void destroyPool() noexcept;
    void writeCapture(const Buffer& buffer);

    wl_shm* sharedMemory_{nullptr};
    wl_surface* waylandSurface_{nullptr};
    wl_shm_pool* pool_{nullptr};
    void* poolPixels_{nullptr};
    size_t poolSize_{0};
    int poolFileDescriptor_{-1};
    WindowSize size_;
    std::array<Buffer, 2> buffers_;
    std::string pendingCapturePath_;
};

} // namespace react_native_linux
