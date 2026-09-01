#include "BundleRunner.h"

#ifdef RNL_ENABLE_GOLDEN
#include "GoldenRenderer.h"

#include <charconv>
#include <system_error>
#endif

#include <cstddef>
#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kFabricFlag = "--fabric";
constexpr std::string_view kGoldenFlag = "--golden";

#ifdef RNL_ENABLE_GOLDEN

constexpr size_t kGoldenDefaultArgumentCount = 4;
constexpr size_t kGoldenSizedArgumentCount = 6;
constexpr int kGoldenDefaultWidth = 800;
constexpr int kGoldenDefaultHeight = 600;

std::optional<int> parsePositiveDimension(std::string_view text) {
    int value = 0;
    const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);

    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value <= 0) {
        return std::nullopt;
    }

    return value;
}

int runGoldenCommand(std::span<char*> arguments) {
    if (arguments.size() != kGoldenDefaultArgumentCount && arguments.size() != kGoldenSizedArgumentCount) {
        std::cerr << "[hello_react] " << kGoldenFlag << " requires <bundle> <output.png> [width height]" << std::endl;

        return 1;
    }

    int width = kGoldenDefaultWidth;
    int height = kGoldenDefaultHeight;

    if (arguments.size() == kGoldenSizedArgumentCount) {
        const std::optional<int> parsedWidth = parsePositiveDimension(arguments[4]);
        const std::optional<int> parsedHeight = parsePositiveDimension(arguments[5]);

        if (!parsedWidth.has_value() || !parsedHeight.has_value()) {
            std::cerr << "[hello_react] " << kGoldenFlag << " width and height must be positive integers" << std::endl;

            return 1;
        }

        width = parsedWidth.value();
        height = parsedHeight.value();
    }

    return react_native_linux::renderGolden(std::string(arguments[2]), std::string(arguments[3]), width, height);
}

#else

int runGoldenCommand(std::span<char*> /*arguments*/) {
    std::cerr << "[hello_react] " << kGoldenFlag
              << " needs Skia, which this build was configured without; run node scripts/vendor-skia.ts and "
                 "reconfigure"
              << std::endl;

    return 1;
}

#endif

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> arguments(argv, static_cast<size_t>(argc));
    const bool isGoldenRequested = arguments.size() > 1 && kGoldenFlag == arguments[1];
    const bool isFabricRequested = arguments.size() > 1 && kFabricFlag == arguments[1];

    if (isFabricRequested && arguments.size() < 3) {
        std::cerr << "[hello_react] " << kFabricFlag << " requires a bundle path" << std::endl;

        return 1;
    }

    try {
        if (isGoldenRequested) {
            return runGoldenCommand(arguments);
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
