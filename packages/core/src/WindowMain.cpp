#include "LinuxMountingManager.h"
#include "RetainedScene.h"
#include "ScenePainter.h"
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
constexpr SkColor kCardColor = SkColorSetRGB(0x33, 0x66, 0xCC);
constexpr SkScalar kCardInset = 64.0F;
constexpr SkScalar kCardCornerRadius = 24.0F;
constexpr std::string_view kFabricFlag = "--fabric";

void paintPlaceholderFrame(SkCanvas& canvas, react_native_linux::WindowSize size,
                           const react_native_linux::SceneDamage& /*damage*/) {
    canvas.clear(react_native_linux::kSceneBackgroundColor);

    const SkRect cardBounds = SkRect::MakeLTRB(kCardInset, kCardInset, static_cast<SkScalar>(size.width) - kCardInset,
                                               static_cast<SkScalar>(size.height) - kCardInset);

    SkPaint cardPaint;
    cardPaint.setColor(kCardColor);
    cardPaint.setAntiAlias(true);

    canvas.drawRRect(SkRRect::MakeRectXY(cardBounds, kCardCornerRadius, kCardCornerRadius), cardPaint);
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

        renderer.drawFrame(window, {}, paintPlaceholderFrame);

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
                // Input first, and unconditionally: the event beat is induced inside this call, and it is what
                // releases everything Fabric has queued since the last frame onto the JavaScript thread.
                session->deliverInput(window.takeInputEvents());

                // The scene and the damage that describes it have to come out of the mounting manager together,
                // under one lock: a transaction landing between them would leave damage this scene cannot satisfy.
                const react_native_linux::SceneFrame frame = session->takeFrame();

                renderer.drawFrame(window, frame.damage,
                                   [&frame](SkCanvas& canvas, react_native_linux::WindowSize /*size*/,
                                            const react_native_linux::SceneDamage& imageDamage) {
                                       react_native_linux::paintScene(canvas, frame.scene, imageDamage);
                                   });
            } else {
                renderer.drawFrame(window, {}, paintPlaceholderFrame);
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
