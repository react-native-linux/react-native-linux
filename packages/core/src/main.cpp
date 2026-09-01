#include "BundleRunner.h"

#include <exception>
#include <iostream>
#include <optional>
#include <span>
#include <string>

int main(int argc, char** argv) {
    const std::span<char*> arguments(argv, static_cast<size_t>(argc));
    std::optional<std::string> bundlePath;

    if (arguments.size() > 1) {
        bundlePath = std::string(arguments[1]);
    }

    try {
        return react_native_linux::runBundle(bundlePath);
    } catch (const std::exception& error) {
        std::cerr << "[bundle-runner] " << error.what() << std::endl;

        return 1;
    }
}
