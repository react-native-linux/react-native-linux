#pragma once

#include <optional>
#include <string>

namespace react_native_linux {

enum class BundleMode {
    Script,
    Fabric,
};

int runBundle(const std::optional<std::string>& bundlePath, BundleMode bundleMode);

} // namespace react_native_linux
