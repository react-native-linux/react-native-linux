#include "SkiaVulkanRenderer.h"
#include "WaylandWindow.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkScalar.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>

namespace {

constexpr uint32_t kInitialWidth = 800;
constexpr uint32_t kInitialHeight = 600;
constexpr std::chrono::milliseconds kFrameCallbackFallback{50};
constexpr SkColor kBackgroundColor = SkColorSetRGB(0x14, 0x16, 0x1A);
constexpr SkColor kCardColor = SkColorSetRGB(0x33, 0x66, 0xCC);
constexpr SkScalar kCardInset = 64.0F;
constexpr SkScalar kCardCornerRadius = 24.0F;

void paintFrame(SkCanvas& canvas, react_native_linux::WindowSize size) {
    canvas.clear(kBackgroundColor);

    const SkRect cardBounds = SkRect::MakeLTRB(kCardInset, kCardInset, static_cast<SkScalar>(size.width) - kCardInset,
                                               static_cast<SkScalar>(size.height) - kCardInset);

    SkPaint cardPaint;
    cardPaint.setColor(kCardColor);
    cardPaint.setAntiAlias(true);

    canvas.drawRRect(SkRRect::MakeRectXY(cardBounds, kCardCornerRadius, kCardCornerRadius), cardPaint);
}

} // namespace

int main() {
    try {
        react_native_linux::WaylandWindow window("react-native-linux",
                                                 react_native_linux::WindowSize{kInitialWidth, kInitialHeight});
        react_native_linux::SkiaVulkanRenderer renderer(window.display(), window.surface(), window.size());

        while (!window.isClosed()) {
            if (window.takePendingResize()) {
                renderer.resize(window.size());
            }

            renderer.drawFrame(window, paintFrame);

            if (!window.waitForRedraw(kFrameCallbackFallback)) {
                break;
            }
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "[rnl-window] " << error.what() << std::endl;

        return 1;
    }
}
