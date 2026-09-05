#include "BundleRunner.h"

#ifdef RNL_ENABLE_GOLDEN
#include "GoldenRenderer.h"
#endif

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <optional>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/graphics/Float.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Size.h>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr std::string_view kFabricFlag = "--fabric";
constexpr std::string_view kGoldenFlag = "--golden";
constexpr std::string_view kDamageGoldenFlag = "--damage-golden";
constexpr std::string_view kHitPaintGoldenFlag = "--hit-paint-golden";
constexpr std::string_view kTextFitGoldenFlag = "--text-fit-golden";
constexpr std::string_view kFirstFrameGoldenFlag = "--first-frame-golden";
constexpr std::string_view kInjectPointerFlag = "--inject-pointer";
constexpr std::string_view kResizeFlag = "--resize";
constexpr std::string_view kScrollToFlag = "--scroll-to";
constexpr std::string_view kAnimatedScrollFlag = "--animated-scroll";
constexpr std::string_view kFocusTabFlag = "--focus-tab";
constexpr std::string_view kFocusClickFlag = "--focus-click";
constexpr std::string_view kFocusCommandGoldenFlag = "--focus-command-golden";
constexpr std::string_view kAnimatedImageFlag = "--animated-image";
constexpr std::string_view kTypeFlag = "--type";
/**
 * Which proof `--golden`, `--damage-golden` and `--hit-paint-golden` run. All three take the same arguments and
 * write the same kind of PNG; the last two also assert something about the scene they painted.
 */
enum class GoldenKind : uint8_t { Scene, Damage, HitPaint, TextFit, FirstFrame };

GoldenKind toGoldenKind(bool isDamageRequested, bool isHitPaintRequested, bool isTextFitRequested,
                        bool isFirstFrameRequested) {
    if (isDamageRequested) {
        return GoldenKind::Damage;
    }

    if (isHitPaintRequested) {
        return GoldenKind::HitPaint;
    }

    if (isTextFitRequested) {
        return GoldenKind::TextFit;
    }

    return isFirstFrameRequested ? GoldenKind::FirstFrame : GoldenKind::Scene;
}

constexpr size_t kInjectPointerArgumentCount = 5;
constexpr size_t kResizeArgumentCount = 5;
constexpr size_t kScrollToArgumentCount = 7;
constexpr size_t kAnimatedScrollArgumentCount = 6;
constexpr size_t kFocusTabArgumentCount = 5;
constexpr size_t kFocusClickArgumentCount = 6;
constexpr size_t kFocusCommandGoldenArgumentCount = 5;
constexpr size_t kAnimatedImageArgumentCount = 5;
constexpr size_t kTypeArgumentCount = 5;

std::optional<int> parsePositiveDimension(std::string_view text) {
    int value = 0;
    const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);

    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value <= 0) {
        return std::nullopt;
    }

    return value;
}

/**
 * A surface coordinate parsed off two arguments, or nothing when either of them is not a positive integer.
 */
std::optional<facebook::react::Point> parseSurfacePoint(std::string_view x, std::string_view y) {
    const std::optional<int> parsedX = parsePositiveDimension(x);
    const std::optional<int> parsedY = parsePositiveDimension(y);

    if (!parsedX.has_value() || !parsedY.has_value()) {
        return std::nullopt;
    }

    return facebook::react::Point{.x = static_cast<facebook::react::Float>(parsedX.value()),
                                  .y = static_cast<facebook::react::Float>(parsedY.value())};
}

int runInjectPointerCommand(std::span<char*> arguments) {
    if (arguments.size() != kInjectPointerArgumentCount) {
        std::cerr << "[hello_react] " << kInjectPointerFlag << " requires <bundle> <x> <y>" << std::endl;

        return 1;
    }

    const std::optional<facebook::react::Point> surfacePoint = parseSurfacePoint(arguments[3], arguments[4]);

    if (!surfacePoint.has_value()) {
        std::cerr << "[hello_react] " << kInjectPointerFlag << " x and y must be positive integers" << std::endl;

        return 1;
    }

    return react_native_linux::runInjectedClick(std::string(arguments[2]), surfacePoint.value());
}

int runAnimatedScrollCommand(std::span<char*> arguments) {
    if (arguments.size() != kAnimatedScrollArgumentCount) {
        std::cerr << "[hello_react] " << kAnimatedScrollFlag << " requires <bundle> <x> <y> <notches>" << std::endl;

        return 1;
    }

    const std::optional<facebook::react::Point> surfacePoint = parseSurfacePoint(arguments[3], arguments[4]);
    const std::optional<int> notches = parsePositiveDimension(arguments[5]);

    if (!surfacePoint.has_value() || !notches.has_value()) {
        std::cerr << "[hello_react] " << kAnimatedScrollFlag << " x, y and notches must be positive integers"
                  << std::endl;

        return 1;
    }

    return react_native_linux::runAnimatedScroll(std::string(arguments[2]), surfacePoint.value(), notches.value());
}

int runResizeCommand(std::span<char*> arguments) {
    if (arguments.size() != kResizeArgumentCount) {
        std::cerr << "[hello_react] " << kResizeFlag << " requires <bundle> <width> <height>" << std::endl;

        return 1;
    }

    const std::optional<int> width = parsePositiveDimension(arguments[3]);
    const std::optional<int> height = parsePositiveDimension(arguments[4]);

    if (!width.has_value() || !height.has_value()) {
        std::cerr << "[hello_react] " << kResizeFlag << " width and height must be positive integers" << std::endl;

        return 1;
    }

    return react_native_linux::runResizedFabricBundle(
        std::string(arguments[2]),
        facebook::react::Size{.width = static_cast<facebook::react::Float>(width.value()),
                              .height = static_cast<facebook::react::Float>(height.value())});
}

#ifdef RNL_ENABLE_GOLDEN

constexpr size_t kGoldenDefaultArgumentCount = 4;
constexpr size_t kGoldenSizedArgumentCount = 6;
constexpr int kGoldenDefaultWidth = 800;
constexpr int kGoldenDefaultHeight = 600;

int runScrollToCommand(std::span<char*> arguments) {
    const std::optional<facebook::react::Point> surfacePoint = parseSurfacePoint(arguments[4], arguments[5]);
    const std::optional<int> parsedNotches = parsePositiveDimension(arguments[6]);

    if (!surfacePoint.has_value() || !parsedNotches.has_value()) {
        std::cerr << "[hello_react] " << kScrollToFlag << " x, y and notches must be positive integers" << std::endl;

        return 1;
    }

    return react_native_linux::renderScrollGolden(std::string(arguments[2]), std::string(arguments[3]),
                                                  surfacePoint.value(), parsedNotches.value(), kGoldenDefaultWidth,
                                                  kGoldenDefaultHeight);
}

int runFocusTabCommand(std::span<char*> arguments) {
    const std::optional<int> parsedPresses = parsePositiveDimension(arguments[4]);

    if (!parsedPresses.has_value()) {
        std::cerr << "[hello_react] " << kFocusTabFlag << " presses must be a positive integer" << std::endl;

        return 1;
    }

    return react_native_linux::renderFocusGolden(std::string(arguments[2]), std::string(arguments[3]),
                                                 parsedPresses.value(), kGoldenDefaultWidth, kGoldenDefaultHeight);
}

int runFocusClickCommand(std::span<char*> arguments) {
    const std::optional<facebook::react::Point> surfacePoint = parseSurfacePoint(arguments[4], arguments[5]);

    if (!surfacePoint.has_value()) {
        std::cerr << "[hello_react] " << kFocusClickFlag << " x and y must be positive integers" << std::endl;

        return 1;
    }

    return react_native_linux::renderFocusClickGolden(std::string(arguments[2]), std::string(arguments[3]),
                                                      surfacePoint.value(), kGoldenDefaultWidth,
                                                      kGoldenDefaultHeight);
}

int runFocusCommandGoldenCommand(std::span<char*> arguments) {
    const std::optional<int> focusedTag = parsePositiveDimension(arguments[4]);

    if (!focusedTag.has_value()) {
        std::cerr << "[hello_react] " << kFocusCommandGoldenFlag << " tag must be a positive integer" << std::endl;

        return 1;
    }

    return react_native_linux::renderFocusCommandGolden(
        std::string(arguments[2]), std::string(arguments[3]),
        static_cast<facebook::react::Tag>(focusedTag.value()), kGoldenDefaultWidth, kGoldenDefaultHeight);
}

int runAnimatedImageCommand(std::span<char*> arguments) {
    const std::optional<int> parsedFrames = parsePositiveDimension(arguments[4]);

    if (!parsedFrames.has_value()) {
        std::cerr << "[hello_react] " << kAnimatedImageFlag << " frames must be a positive integer" << std::endl;

        return 1;
    }

    return react_native_linux::renderAnimatedImageGolden(std::string(arguments[2]), std::string(arguments[3]),
                                                         parsedFrames.value(), kGoldenDefaultWidth,
                                                         kGoldenDefaultHeight);
}

int runTypeCommand(std::span<char*> arguments) {
    return react_native_linux::renderTypedGolden(std::string(arguments[2]), std::string(arguments[3]),
                                                 std::string(arguments[4]), kGoldenDefaultWidth,
                                                 kGoldenDefaultHeight);
}

std::string_view toGoldenFlag(GoldenKind goldenKind) {
    if (goldenKind == GoldenKind::Damage) {
        return kDamageGoldenFlag;
    }

    if (goldenKind == GoldenKind::HitPaint) {
        return kHitPaintGoldenFlag;
    }

    if (goldenKind == GoldenKind::TextFit) {
        return kTextFitGoldenFlag;
    }

    if (goldenKind == GoldenKind::FirstFrame) {
        return kFirstFrameGoldenFlag;
    }

    return kGoldenFlag;
}

int runGoldenCommand(std::span<char*> arguments, GoldenKind goldenKind) {
    const std::string_view flag = toGoldenFlag(goldenKind);

    if (arguments.size() != kGoldenDefaultArgumentCount && arguments.size() != kGoldenSizedArgumentCount) {
        std::cerr << "[hello_react] " << flag << " requires <bundle> <output.png> [width height]" << std::endl;

        return 1;
    }

    int width = kGoldenDefaultWidth;
    int height = kGoldenDefaultHeight;

    if (arguments.size() == kGoldenSizedArgumentCount) {
        const std::optional<int> parsedWidth = parsePositiveDimension(arguments[4]);
        const std::optional<int> parsedHeight = parsePositiveDimension(arguments[5]);

        if (!parsedWidth.has_value() || !parsedHeight.has_value()) {
            std::cerr << "[hello_react] " << flag << " width and height must be positive integers" << std::endl;

            return 1;
        }

        width = parsedWidth.value();
        height = parsedHeight.value();
    }

    const std::string bundlePath(arguments[2]);
    const std::string outputPath(arguments[3]);

    if (goldenKind == GoldenKind::Damage) {
        return react_native_linux::renderDamageGolden(bundlePath, outputPath, width, height);
    }

    if (goldenKind == GoldenKind::HitPaint) {
        return react_native_linux::renderHitPaintGolden(bundlePath, outputPath, width, height);
    }

    if (goldenKind == GoldenKind::TextFit) {
        return react_native_linux::renderTextFitGolden(bundlePath, outputPath, width, height);
    }

    if (goldenKind == GoldenKind::FirstFrame) {
        return react_native_linux::renderFirstFrameGolden(bundlePath, outputPath, width, height);
    }

    return react_native_linux::renderGolden(bundlePath, outputPath, width, height);
}

#else

int reportMissingSkia() {
    std::cerr << "[hello_react] " << kGoldenFlag << ", " << kDamageGoldenFlag << ", " << kHitPaintGoldenFlag
              << ", " << kTextFitGoldenFlag << ", " << kFirstFrameGoldenFlag << ", " << kScrollToFlag << ", "
              << kFocusTabFlag << ", " << kFocusClickFlag << ", " << kFocusCommandGoldenFlag
              << ", " << kAnimatedImageFlag << " and " << kTypeFlag
              << " need Skia, which this build was configured without; run node scripts/vendor-skia.ts and "
                 "reconfigure"
              << std::endl;

    return 1;
}

int runGoldenCommand(std::span<char*> /*arguments*/, GoldenKind /*goldenKind*/) { return reportMissingSkia(); }

int runScrollToCommand(std::span<char*> /*arguments*/) { return reportMissingSkia(); }

int runFocusTabCommand(std::span<char*> /*arguments*/) { return reportMissingSkia(); }

int runFocusClickCommand(std::span<char*> /*arguments*/) { return reportMissingSkia(); }

int runFocusCommandGoldenCommand(std::span<char*> /*arguments*/) { return reportMissingSkia(); }

int runAnimatedImageCommand(std::span<char*> /*arguments*/) { return reportMissingSkia(); }

int runTypeCommand(std::span<char*> /*arguments*/) { return reportMissingSkia(); }

#endif

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> arguments(argv, static_cast<size_t>(argc));
    const bool isGoldenRequested = arguments.size() > 1 && kGoldenFlag == arguments[1];
    const bool isDamageGoldenRequested = arguments.size() > 1 && kDamageGoldenFlag == arguments[1];
    const bool isHitPaintGoldenRequested = arguments.size() > 1 && kHitPaintGoldenFlag == arguments[1];
    const bool isTextFitGoldenRequested = arguments.size() > 1 && kTextFitGoldenFlag == arguments[1];
    const bool isFirstFrameGoldenRequested = arguments.size() > 1 && kFirstFrameGoldenFlag == arguments[1];
    const bool isFabricRequested = arguments.size() > 1 && kFabricFlag == arguments[1];
    const bool isInjectPointerRequested = arguments.size() > 1 && kInjectPointerFlag == arguments[1];
    const bool isResizeRequested = arguments.size() > 1 && kResizeFlag == arguments[1];
    const bool isScrollToRequested = arguments.size() > 1 && kScrollToFlag == arguments[1];
    const bool isAnimatedScrollRequested = arguments.size() > 1 && kAnimatedScrollFlag == arguments[1];
    const bool isFocusTabRequested = arguments.size() > 1 && kFocusTabFlag == arguments[1];
    const bool isFocusClickRequested = arguments.size() > 1 && kFocusClickFlag == arguments[1];
    const bool isFocusCommandGoldenRequested = arguments.size() > 1 && kFocusCommandGoldenFlag == arguments[1];
    const bool isAnimatedImageRequested = arguments.size() > 1 && kAnimatedImageFlag == arguments[1];
    const bool isTypeRequested = arguments.size() > 1 && kTypeFlag == arguments[1];

    if (isFabricRequested && arguments.size() < 3) {
        std::cerr << "[hello_react] " << kFabricFlag << " requires a bundle path" << std::endl;

        return 1;
    }

    if (isScrollToRequested && arguments.size() != kScrollToArgumentCount) {
        std::cerr << "[hello_react] " << kScrollToFlag << " requires <bundle> <output.png> <x> <y> <notches>"
                  << std::endl;

        return 1;
    }

    if (isFocusTabRequested && arguments.size() != kFocusTabArgumentCount) {
        std::cerr << "[hello_react] " << kFocusTabFlag << " requires <bundle> <output.png> <presses>" << std::endl;

        return 1;
    }

    if (isFocusClickRequested && arguments.size() != kFocusClickArgumentCount) {
        std::cerr << "[hello_react] " << kFocusClickFlag << " requires <bundle> <output.png> <x> <y>" << std::endl;

        return 1;
    }

    if (isFocusCommandGoldenRequested && arguments.size() != kFocusCommandGoldenArgumentCount) {
        std::cerr << "[hello_react] " << kFocusCommandGoldenFlag << " requires <bundle> <output.png> <tag>"
                  << std::endl;

        return 1;
    }

    if (isAnimatedImageRequested && arguments.size() != kAnimatedImageArgumentCount) {
        std::cerr << "[hello_react] " << kAnimatedImageFlag << " requires <bundle> <output.png> <frames>"
                  << std::endl;

        return 1;
    }

    if (isTypeRequested && arguments.size() != kTypeArgumentCount) {
        std::cerr << "[hello_react] " << kTypeFlag << " requires <bundle> <output.png> \"<sequence>\"" << std::endl;

        return 1;
    }

    try {
        if (isInjectPointerRequested) {
            return runInjectPointerCommand(arguments);
        }

        if (isResizeRequested) {
            return runResizeCommand(arguments);
        }

        if (isAnimatedScrollRequested) {
            return runAnimatedScrollCommand(arguments);
        }

        if (isScrollToRequested) {
            return runScrollToCommand(arguments);
        }

        if (isFocusTabRequested) {
            return runFocusTabCommand(arguments);
        }

        if (isFocusClickRequested) {
            return runFocusClickCommand(arguments);
        }

        if (isFocusCommandGoldenRequested) {
            return runFocusCommandGoldenCommand(arguments);
        }

        if (isAnimatedImageRequested) {
            return runAnimatedImageCommand(arguments);
        }

        if (isTypeRequested) {
            return runTypeCommand(arguments);
        }

        if (isGoldenRequested || isDamageGoldenRequested || isHitPaintGoldenRequested || isTextFitGoldenRequested ||
            isFirstFrameGoldenRequested) {
            return runGoldenCommand(arguments, toGoldenKind(isDamageGoldenRequested, isHitPaintGoldenRequested,
                                                           isTextFitGoldenRequested, isFirstFrameGoldenRequested));
        }

        std::optional<std::string> bundlePath;
        react_native_linux::BundleMode bundleMode = react_native_linux::BundleMode::Script;

        if (isFabricRequested) {
            bundleMode = react_native_linux::BundleMode::Fabric;
            bundlePath = std::string(arguments[2]);
        } else if (arguments.size() > 1) {
            bundlePath = std::string(arguments[1]);
        }

        return react_native_linux::runBundle(bundlePath, bundleMode);
    } catch (const std::exception& error) {
        std::cerr << "[bundle-runner] " << error.what() << std::endl;

        return 1;
    }
}
