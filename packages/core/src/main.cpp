#include "BundleRunner.h"

#ifdef RNL_ENABLE_GOLDEN
#include "GoldenRenderer.h"
#endif

#include <charconv>
#include <cstddef>
#include <exception>
#include <iostream>
#include <optional>
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
constexpr std::string_view kInjectPointerFlag = "--inject-pointer";
constexpr std::string_view kResizeFlag = "--resize";
constexpr std::string_view kScrollToFlag = "--scroll-to";
constexpr std::string_view kFocusTabFlag = "--focus-tab";
constexpr std::string_view kTypeFlag = "--type";
constexpr size_t kInjectPointerArgumentCount = 5;
constexpr size_t kResizeArgumentCount = 5;
constexpr size_t kScrollToArgumentCount = 7;
constexpr size_t kFocusTabArgumentCount = 5;
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

int runTypeCommand(std::span<char*> arguments) {
    return react_native_linux::renderTypedGolden(std::string(arguments[2]), std::string(arguments[3]),
                                                 std::string(arguments[4]), kGoldenDefaultWidth,
                                                 kGoldenDefaultHeight);
}

int runGoldenCommand(std::span<char*> arguments, bool isDamageRequested) {
    const std::string_view flag = isDamageRequested ? kDamageGoldenFlag : kGoldenFlag;

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

    if (isDamageRequested) {
        return react_native_linux::renderDamageGolden(bundlePath, outputPath, width, height);
    }

    return react_native_linux::renderGolden(bundlePath, outputPath, width, height);
}

#else

int reportMissingSkia() {
    std::cerr << "[hello_react] " << kGoldenFlag << ", " << kDamageGoldenFlag << ", " << kScrollToFlag << ", "
              << kFocusTabFlag << " and " << kTypeFlag
              << " need Skia, which this build was configured without; run node scripts/vendor-skia.ts and "
                 "reconfigure"
              << std::endl;

    return 1;
}

int runGoldenCommand(std::span<char*> /*arguments*/, bool /*isDamageRequested*/) { return reportMissingSkia(); }

int runScrollToCommand(std::span<char*> /*arguments*/) { return reportMissingSkia(); }

int runFocusTabCommand(std::span<char*> /*arguments*/) { return reportMissingSkia(); }

int runTypeCommand(std::span<char*> /*arguments*/) { return reportMissingSkia(); }

#endif

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> arguments(argv, static_cast<size_t>(argc));
    const bool isGoldenRequested = arguments.size() > 1 && kGoldenFlag == arguments[1];
    const bool isDamageGoldenRequested = arguments.size() > 1 && kDamageGoldenFlag == arguments[1];
    const bool isFabricRequested = arguments.size() > 1 && kFabricFlag == arguments[1];
    const bool isInjectPointerRequested = arguments.size() > 1 && kInjectPointerFlag == arguments[1];
    const bool isResizeRequested = arguments.size() > 1 && kResizeFlag == arguments[1];
    const bool isScrollToRequested = arguments.size() > 1 && kScrollToFlag == arguments[1];
    const bool isFocusTabRequested = arguments.size() > 1 && kFocusTabFlag == arguments[1];
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

        if (isScrollToRequested) {
            return runScrollToCommand(arguments);
        }

        if (isFocusTabRequested) {
            return runFocusTabCommand(arguments);
        }

        if (isTypeRequested) {
            return runTypeCommand(arguments);
        }

        if (isGoldenRequested || isDamageGoldenRequested) {
            return runGoldenCommand(arguments, isDamageGoldenRequested);
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
