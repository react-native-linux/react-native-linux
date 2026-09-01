#include "RetainedScene.h"
#include "SkiaVulkanRenderer.h"
#include "WaylandWindow.h"
#include "WindowSession.h"
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
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace {

constexpr uint32_t kInitialWidth = 800;
constexpr uint32_t kInitialHeight = 600;
constexpr std::chrono::milliseconds kFrameCallbackFallback{50};
constexpr SkColor kBackgroundColor = SkColorSetRGB(0x14, 0x16, 0x1A);
constexpr SkColor kCardColor = SkColorSetRGB(0x33, 0x66, 0xCC);
constexpr SkScalar kCardInset = 64.0F;
constexpr SkScalar kCardCornerRadius = 24.0F;
constexpr std::string_view kFabricFlag = "--fabric";

void paintPlaceholderFrame(SkCanvas& canvas, react_native_linux::WindowSize size) {
    canvas.clear(kBackgroundColor);

    const SkRect cardBounds = SkRect::MakeLTRB(kCardInset, kCardInset, static_cast<SkScalar>(size.width) - kCardInset,
                                               static_cast<SkScalar>(size.height) - kCardInset);

    SkPaint cardPaint;
    cardPaint.setColor(kCardColor);
    cardPaint.setAntiAlias(true);

    canvas.drawRRect(SkRRect::MakeRectXY(cardBounds, kCardCornerRadius, kCardCornerRadius), cardPaint);
}

void paintSceneFrame(SkCanvas& canvas, const react_native_linux::SceneSnapshot& scene) {
    canvas.clear(kBackgroundColor);

    SkPaint fillPaint;
    fillPaint.setAntiAlias(true);

    for (const react_native_linux::SceneRectangle& rectangle : scene) {
        fillPaint.setColor(rectangle.colorArgb);
        canvas.drawRect(SkRect::MakeXYWH(rectangle.frame.origin.x, rectangle.frame.origin.y, rectangle.frame.size.width,
                                         rectangle.frame.size.height),
                        fillPaint);
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> arguments(argv, static_cast<size_t>(argc));
    const bool isFabricRequested = arguments.size() > 1 && kFabricFlag == arguments[1];

    if (isFabricRequested && arguments.size() < 3) {
        std::cerr << "[rnl-window] " << kFabricFlag << " requires a bundle path" << std::endl;

        return 1;
    }

    try {
        react_native_linux::WaylandWindow window("react-native-linux",
                                                 react_native_linux::WindowSize{kInitialWidth, kInitialHeight});
        react_native_linux::SkiaVulkanRenderer renderer(window.display(), window.surface(), window.size());
        std::optional<react_native_linux::WindowSession> session;

        renderer.drawFrame(window, paintPlaceholderFrame);

        if (isFabricRequested) {
            session.emplace(std::string(arguments[2]), window.size());
        }

        while (!window.isClosed()) {
            if (window.takePendingResize()) {
                renderer.resize(window.size());

                if (session.has_value()) {
                    session->resize(window.size());
                }
            }

            if (session.has_value()) {
                const react_native_linux::SceneSnapshot scene = session->snapshotScene();

                renderer.drawFrame(window, [&scene](SkCanvas& canvas, react_native_linux::WindowSize /*size*/) {
                    paintSceneFrame(canvas, scene);
                });
            } else {
                renderer.drawFrame(window, paintPlaceholderFrame);
            }

            if (!window.waitForRedraw(kFrameCallbackFallback)) {
                break;
            }
        }

        return session.has_value() && session->hasReportedFatalError() ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "[rnl-window] " << error.what() << std::endl;

        return 1;
    }
}
