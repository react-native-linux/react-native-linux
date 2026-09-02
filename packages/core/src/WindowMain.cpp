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

#include <charconv>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr uint32_t kInitialWidth = 800;
constexpr uint32_t kInitialHeight = 600;
constexpr uint32_t kDefaultScreenshotFrames = 60;
constexpr std::chrono::milliseconds kFrameCallbackFallback{50};
constexpr SkColor kCardColor = SkColorSetRGB(0x33, 0x66, 0xCC);
constexpr SkScalar kCardInset = 64.0F;
constexpr SkScalar kCardCornerRadius = 24.0F;
constexpr std::string_view kFabricFlag = "--fabric";
constexpr std::string_view kScreenshotFlag = "--screenshot";
constexpr std::string_view kFramesFlag = "--frames";

/**
 * `--screenshot <path>` runs the ordinary loop and reads the last presented swapchain image back into a PNG, so
 * the picture it writes came through the real Vulkan and Wayland path rather than an offscreen surface.
 * `--frames` is how long the bundle is given to mount and settle before that frame is captured.
 */
struct WindowArguments {
    std::optional<std::string> bundlePath;
    std::optional<std::string> screenshotPath;
    uint32_t frameCount{kDefaultScreenshotFrames};
    std::string error;
};

std::string describeMissingValue(std::string_view flag) {
    if (flag == kFabricFlag) {
        return "--fabric requires a bundle path";
    }

    if (flag == kScreenshotFlag) {
        return "--screenshot requires an output path";
    }

    return "--frames requires a positive frame count";
}

std::optional<uint32_t> parseFrameCount(std::string_view value) {
    uint32_t frameCount = 0;
    const std::from_chars_result parsed = std::from_chars(value.data(), value.data() + value.size(), frameCount);

    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || frameCount == 0) {
        return std::nullopt;
    }

    return frameCount;
}

WindowArguments parseArguments(std::span<char*> arguments) {
    WindowArguments parsed;

    for (size_t index = 1; index < arguments.size(); ++index) {
        const std::string_view flag = arguments[index];

        if (flag != kFabricFlag && flag != kScreenshotFlag && flag != kFramesFlag) {
            parsed.error = "unknown argument " + std::string(flag);

            return parsed;
        }

        if (index + 1 >= arguments.size()) {
            parsed.error = describeMissingValue(flag);

            return parsed;
        }

        const std::string_view value = arguments[index + 1];
        ++index;

        if (flag == kFabricFlag) {
            parsed.bundlePath = std::string(value);
        } else if (flag == kScreenshotFlag) {
            parsed.screenshotPath = std::string(value);
        } else {
            const std::optional<uint32_t> frameCount = parseFrameCount(value);

            if (!frameCount.has_value()) {
                parsed.error = describeMissingValue(flag);

                return parsed;
            }

            parsed.frameCount = frameCount.value();
        }
    }

    return parsed;
}

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
    const WindowArguments parsedArguments = parseArguments(arguments);

    if (!parsedArguments.error.empty()) {
        std::cerr << "[rnl-window] " << parsedArguments.error << std::endl;

        return 1;
    }

    try {
        react_native_linux::WaylandWindow window("react-native-linux",
                                                 react_native_linux::WindowSize{kInitialWidth, kInitialHeight});
        react_native_linux::SkiaVulkanRenderer renderer(window.display(), window.surface(), window.size());
        std::optional<react_native_linux::WindowSession> session;

        renderer.drawFrame(window, {}, paintPlaceholderFrame);

        if (parsedArguments.bundlePath.has_value()) {
            session.emplace(parsedArguments.bundlePath.value(), window.size());
        }

        uint32_t presentedFrames = 0;
        bool hasCaptured = false;

        while (!window.isClosed() && !hasCaptured) {
            if (window.takePendingResize()) {
                renderer.resize(window.size());

                if (session.has_value()) {
                    session->resize(window.size());
                }
            }

            // The capture is armed before the frame that carries it, because the readback happens inside
            // drawFrame while the image is still owned by this process. A frame that rebuilds the swapchain
            // instead of painting leaves the request pending, so the next one takes it.
            const bool isCaptureFrame =
                parsedArguments.screenshotPath.has_value() && presentedFrames + 1 >= parsedArguments.frameCount;

            if (isCaptureFrame) {
                renderer.captureNextFrame(parsedArguments.screenshotPath.value());
            }

            bool presented = false;

            if (session.has_value()) {
                // Input first, and unconditionally: the event beat is induced inside this call, and it is what
                // releases everything Fabric has queued since the last frame onto the JavaScript thread.
                session->deliverInput(window.takeInputEvents());

                // The scene and the damage that describes it have to come out of the mounting manager together,
                // under one lock: a transaction landing between them would leave damage this scene cannot satisfy.
                const react_native_linux::SceneFrame frame = session->takeFrame();

                presented = renderer.drawFrame(window, frame.damage,
                                               [&frame](SkCanvas& canvas, react_native_linux::WindowSize /*size*/,
                                                        const react_native_linux::SceneDamage& imageDamage) {
                                                   react_native_linux::paintScene(canvas, frame.scene, imageDamage);
                                               });
            } else {
                presented = renderer.drawFrame(window, {}, paintPlaceholderFrame);
            }

            if (presented) {
                ++presentedFrames;
            }
            hasCaptured = isCaptureFrame && !renderer.hasPendingCapture();

            if (!hasCaptured && !window.waitForRedraw(kFrameCallbackFallback)) {
                break;
            }
        }

        if (parsedArguments.screenshotPath.has_value() && !hasCaptured) {
            std::cerr << "[rnl-window] the window closed before frame " << parsedArguments.frameCount
                      << " could be captured" << std::endl;

            return 1;
        }

        return session.has_value() && session->hasReportedFatalError() ? 1 : 0;
    } catch (const std::exception& error) {
        std::cerr << "[rnl-window] " << error.what() << std::endl;

        return 1;
    }
}
