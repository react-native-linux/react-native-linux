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
#include <span>
#include <string>
#include <string_view>
#include <system_error>

namespace {

constexpr std::string_view kFabricFlag = "--fabric";
constexpr std::string_view kGoldenFlag = "--golden";
constexpr std::string_view kDamageGoldenFlag = "--damage-golden";
constexpr std::string_view kInjectPointerFlag = "--inject-pointer";
constexpr size_t kInjectPointerArgumentCount = 5;

std::optional<int> parsePositiveDimension(std::string_view text) {
    int value = 0;
    const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);

    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value <= 0) {
        return std::nullopt;
    }

    return value;
}

int runInjectPointerCommand(std::span<char*> arguments) {
    if (arguments.size() != kInjectPointerArgumentCount) {
        std::cerr << "[hello_react] " << kInjectPointerFlag << " requires <bundle> <x> <y>" << std::endl;

        return 1;
    }

    const std::optional<int> parsedX = parsePositiveDimension(arguments[3]);
    const std::optional<int> parsedY = parsePositiveDimension(arguments[4]);

    if (!parsedX.has_value() || !parsedY.has_value()) {
        std::cerr << "[hello_react] " << kInjectPointerFlag << " x and y must be positive integers" << std::endl;

        return 1;
    }

    const facebook::react::Point surfacePoint{.x = static_cast<facebook::react::Float>(parsedX.value()),
                                              .y = static_cast<facebook::react::Float>(parsedY.value())};

    return react_native_linux::runInjectedClick(std::string(arguments[2]), surfacePoint);
}

#ifdef RNL_ENABLE_GOLDEN

constexpr size_t kGoldenDefaultArgumentCount = 4;
constexpr size_t kGoldenSizedArgumentCount = 6;
constexpr int kGoldenDefaultWidth = 800;
constexpr int kGoldenDefaultHeight = 600;

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

int runGoldenCommand(std::span<char*> /*arguments*/, bool /*isDamageRequested*/) {
    std::cerr << "[hello_react] " << kGoldenFlag << " and " << kDamageGoldenFlag
              << " need Skia, which this build was configured without; run node scripts/vendor-skia.ts and "
                 "reconfigure"
              << std::endl;

    return 1;
}

#endif

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> arguments(argv, static_cast<size_t>(argc));
    const bool isGoldenRequested = arguments.size() > 1 && kGoldenFlag == arguments[1];
    const bool isDamageGoldenRequested = arguments.size() > 1 && kDamageGoldenFlag == arguments[1];
    const bool isFabricRequested = arguments.size() > 1 && kFabricFlag == arguments[1];
    const bool isInjectPointerRequested = arguments.size() > 1 && kInjectPointerFlag == arguments[1];

    if (isFabricRequested && arguments.size() < 3) {
        std::cerr << "[hello_react] " << kFabricFlag << " requires a bundle path" << std::endl;

        return 1;
    }

    try {
        if (isInjectPointerRequested) {
            return runInjectPointerCommand(arguments);
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
