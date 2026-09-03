#pragma once

#include "DimensionsSource.h"

#include <jsi/jsi.h>

#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace facebook::react {

class CallInvoker;
class NativeAnimatedNodesManagerProvider;
class TurboModule;

} // namespace facebook::react

namespace react_native_linux {

class LinuxDeviceInfoModule;

/**
 * The TurboModules this platform registers, and the single `TurboModuleBinding` that exposes them to JavaScript
 * (#50). `DeviceInfo` came first; `NativeAnimatedModule` is the second (#127).
 *
 * Upstream's `ReactCxxPlatform` does this with `ReactCxxTurboModuleProvider`: a chain of provider callbacks over
 * every core module it ships, installed from its own `ReactHost`. This is the same shape reduced to what two
 * modules need — one name-to-factory map, one lookup, `nullptr` for everything else. Its `DeviceInfoModule` is not
 * reusable here — it answers `getConstants` with a hardcoded 1280x720 and has no way to be told a surface size —
 * so that module is ours; `AnimatedModule` is upstream's, built exactly as `ReactCxxTurboModuleProvider` builds
 * it, from the shared `NativeAnimatedNodesManagerProvider` the host owns. The generated
 * `NativeDeviceInfoCxxSpec` and `NativeAnimatedModuleCxxSpec` come from `packages/core/generated`.
 *
 * `DeviceInfo` is constructed eagerly rather than per lookup, because the frame thread needs a handle to it to
 * emit `didUpdateDimensions` whether or not JavaScript has ever asked for the module. `AnimatedModule` is built
 * per lookup, which is what upstream does, and is what defers `NativeAnimatedNodesManagerProvider::getOrCreate` —
 * and therefore the `UIManagerBinding` lookup inside it — until JavaScript actually reaches for the module.
 *
 * Threading contract: `install` runs on the JavaScript thread, inside the `initializeRuntime` bindings installer,
 * and so does every factory in the map, because `TurboModuleBinding` only calls the provider from a JavaScript
 * property access. `dimensions` and `publishPendingDimensions` run on the platform frame thread; both reach
 * JavaScript only through `DimensionsSource`'s mutex and the module's `CallInvoker`.
 */
class TurboModuleRegistry final {
public:
    TurboModuleRegistry(
        std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
        std::shared_ptr<facebook::react::NativeAnimatedNodesManagerProvider> animatedNodesManagerProvider);

    DimensionsSource& dimensions() noexcept;
    void install(facebook::jsi::Runtime& runtime);

    /**
     * Emits at most one `didUpdateDimensions` for everything that has configured the dimensions since the last
     * call. Called once per frame, which is what keeps a burst of `xdg_toplevel.configure` events from becoming a
     * burst of `useWindowDimensions` re-renders.
     */
    void publishPendingDimensions();

private:
    using ModuleFactory = std::function<std::shared_ptr<facebook::react::TurboModule>()>;

    std::shared_ptr<DimensionsSource> dimensionsSource_;
    std::shared_ptr<LinuxDeviceInfoModule> deviceInfoModule_;
    std::unordered_map<std::string_view, ModuleFactory> moduleFactories_;
};

} // namespace react_native_linux
