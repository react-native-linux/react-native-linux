#pragma once

#include "WorkletsModule.h"

#include <ReactCommon/TurboModule.h>
#include <jsi/jsi.h>
#include <memory>
#include <string_view>

namespace reanimated {

class ReanimatedModuleProxy;

} // namespace reanimated

namespace react_native_linux {

/**
 * The `ContextContainer` key `FabricHost` publishes its `IMountingManager` under, as a
 * `std::shared_ptr<facebook::react::IMountingManager>`, for Reanimated's platform contract.
 */
inline constexpr char kMountingManagerContextKey[] = "react_native_linux::IMountingManager";

/**
 * react-native-reanimated's `ReanimatedModule` (#140): the host `apple/reanimated/apple/ReanimatedModule.mm` is on
 * Apple, as a C++ TurboModule. Its one method, `installTurboModule`, constructs the `ReanimatedModuleProxy` over
 * worklets' UI runtime and UI scheduler with `makePlatformDepMethodsHolder`, attaches it to Fabric, which registers
 * Reanimated's commit and mount hooks with the `UIManager`, and installs `globalThis.__reanimatedModuleProxy`. It
 * answers `false`, as Apple does without a scheduler, when no Fabric surface exists, and Reanimated's JavaScript
 * then installs its dummy proxy.
 *
 * Reanimated is compiled with `USE_ANIMATION_BACKEND` (asserted in ReanimatedModule.cpp), so its whole frame loop
 * is a callback on React Native's shared animation backend: `FabricHost::tickAnimations` runs it once per frame
 * with the frame's timestamp, and the backend applies the props it returns — synchronously for a non-layout prop,
 * through a commit for a layout one. That callback is where the frame's updates are flushed, so this host never
 * calls `performOperations`, which upstream forbids on that path. Reanimated's commit hook passes every commit
 * through unchanged on that path, and React Native's `AnimationBackendCommitHook` re-applies the animated props to
 * a React commit, so the two hooks never both write one tree. Reduced motion reads false until the platform
 * source of #143 exists.
 *
 * Threading contract: `installTurboModule` and `invalidate` run on the JavaScript thread. The proxy's frame work,
 * and every member of the platform contract, run on the frame thread, which is worklets' UI thread (ADR-0003).
 * `ReactHost` calls `invalidate` before it invalidates worklets, while the frame thread runs no frame, so the proxy
 * releases its UI-runtime values while that runtime is alive.
 */
class LinuxReanimatedModule final : public facebook::react::TurboModule {
public:
    static constexpr std::string_view kModuleName = "ReanimatedModule";

    LinuxReanimatedModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                          std::shared_ptr<LinuxWorkletsModule> worklets);

    void invalidate();

private:
    bool installProxy(facebook::jsi::Runtime& runtime);

    std::shared_ptr<LinuxWorkletsModule> worklets_;
    std::shared_ptr<reanimated::ReanimatedModuleProxy> proxy_;
};

} // namespace react_native_linux
