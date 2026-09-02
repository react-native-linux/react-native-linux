#pragma once

#include "LinuxMountingManager.h"
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

/**
 * What a headless run of a bundle that commits twice leaves behind: the scene as it stood after the first commit,
 * the scene after the second, and the damage the second commit accumulated. Everything a partial redraw needs, and
 * everything an equivalence proof needs to compare it against a full one.
 *
 * `failure` is empty when the run produced all three. A bundle that never commits, or that commits only once,
 * fills it instead of leaving a caller to guess why the damage is empty.
 */
struct FabricDamageRunResult {
    SceneSnapshot firstScene;
    SceneSnapshot secondScene;
    SceneDamage damage;
    std::string failure;
    bool hasReportedFatalError{};
};

int runBundle(const std::optional<std::string>& bundlePath, BundleMode bundleMode);
FabricRunResult runFabricBundle(const std::optional<std::string>& bundlePath, facebook::react::Size surfaceSize);
FabricDamageRunResult runFabricBundleAcrossCommits(const std::string& bundlePath, facebook::react::Size surfaceSize);

} // namespace react_native_linux
