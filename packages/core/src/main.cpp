#include "BundleRunner.h"

#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view kFabricFlag = "--fabric";

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> arguments(argv, static_cast<size_t>(argc));
    std::optional<std::string> bundlePath;
    react_native_linux::BundleMode bundleMode = react_native_linux::BundleMode::Script;

    if (arguments.size() > 1 && kFabricFlag == arguments[1]) {
        if (arguments.size() < 3) {
            std::cerr << "[hello_react] " << kFabricFlag << " requires a bundle path" << std::endl;

            return 1;
        }

        bundleMode = react_native_linux::BundleMode::Fabric;
        bundlePath = std::string(arguments[2]);
    } else if (arguments.size() > 1) {
        bundlePath = std::string(arguments[1]);
    }

    try {
        return react_native_linux::runBundle(bundlePath, bundleMode);
    } catch (const std::exception& error) {
        std::cerr << "[bundle-runner] " << error.what() << std::endl;

        return 1;
    }
}
