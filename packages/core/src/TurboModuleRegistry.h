#pragma once

#include "DimensionsSource.h"

#include <jsi/jsi.h>

#include <memory>

namespace facebook::react {

class CallInvoker;

} // namespace facebook::react

namespace react_native_linux {

class LinuxDeviceInfoModule;

/**
 * The TurboModules this platform registers, and the single `TurboModuleBinding` that exposes them to JavaScript
 * (#50). `DeviceInfo` is the first and, for now, the only one.
 *
 * Upstream's `ReactCxxPlatform` does this with `ReactCxxTurboModuleProvider`: a chain of provider callbacks over
 * every core module it ships, installed from its own `ReactHost`. This is the same shape with one entry, because
 * a provider chain with nothing to chain is a chain. Its `DeviceInfoModule` is not reusable here — it answers
 * `getConstants` with a hardcoded 1280x720 and has no way to be told a surface size — so the module below is
 * ours; the generated `NativeDeviceInfoCxxSpec`, the payload structs and their `Bridging` specialisations are
 * upstream's, reached through `<react/coremodules/DeviceInfoModule.h>` and `packages/core/generated`.
 *
 * The module is constructed eagerly rather than per lookup, because the frame thread needs a handle to it to emit
 * `didUpdateDimensions` whether or not JavaScript has ever asked for the module. `TurboModuleBinding` keeps its
 * own reference for as long as the runtime lives, so the order this object is destroyed in does not matter.
 *
 * Threading contract: `install` runs on the JavaScript thread, inside the `initializeRuntime` bindings installer.
 * `dimensions` and `publishPendingDimensions` run on the platform frame thread; both reach JavaScript only
 * through `DimensionsSource`'s mutex and the module's `CallInvoker`.
 */
class TurboModuleRegistry final {
public:
    explicit TurboModuleRegistry(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

    DimensionsSource& dimensions() noexcept;
    void install(facebook::jsi::Runtime& runtime);

    /**
     * Emits at most one `didUpdateDimensions` for everything that has configured the dimensions since the last
     * call. Called once per frame, which is what keeps a burst of `xdg_toplevel.configure` events from becoming a
     * burst of `useWindowDimensions` re-renders.
     */
    void publishPendingDimensions();

private:
    std::shared_ptr<DimensionsSource> dimensionsSource_;
    std::shared_ptr<LinuxDeviceInfoModule> deviceInfoModule_;
};

} // namespace react_native_linux
