#pragma once

#include "RetainedScene.h"

#include <react/renderer/graphics/Size.h>

#include <optional>
#include <string>

namespace react_native_linux {

enum class BundleMode {
    Script,
    Fabric,
};

/**
 * What a headless Fabric run leaves behind once the JavaScript thread has gone quiet and the surface has been
 * stopped: the scene as a flat list of absolute rectangles, the same scene as the human-readable dump, and whether
 * the run reported a fatal JavaScript error.
 *
 * Both are captured before teardown because stopping the surface commits an empty tree.
 */
struct FabricRunResult {
    SceneSnapshot scene;
    std::string sceneDump;
    bool hasReportedFatalError{};
};

int runBundle(const std::optional<std::string>& bundlePath, BundleMode bundleMode);
FabricRunResult runFabricBundle(const std::optional<std::string>& bundlePath, facebook::react::Size surfaceSize);

} // namespace react_native_linux
