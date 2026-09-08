#pragma once

#include "Activation.h"
#include "Appearance.h"
#include "DimensionsSource.h"

#include <functional>
#include <jsi/jsi.h>
#include <memory>
#include <string_view>
#include <unordered_map>

namespace facebook::react {

class CallInvoker;
class NativeAnimatedNodesManagerProvider;
class TurboModule;

} // namespace facebook::react

namespace react_native_linux {

class LinuxAppearanceModule;
class LinuxDeviceInfoModule;
class LinuxLinkingModule;

/**
 * The TurboModules this platform registers, and the single `TurboModuleBinding` that exposes them to JavaScript
 * (#50). `DeviceInfo` came first, `NativeAnimatedModule` is the second (#127) and `Appearance` the third (#52),
 * over the `AppearanceModel` precedence engine of #260.
 *
 * Upstream's `ReactCxxPlatform` does this with `ReactCxxTurboModuleProvider`: a chain of provider callbacks over
 * every core module it ships, installed from its own `ReactHost`. This is the same shape reduced to what two
 * modules need — one name-to-factory map, one lookup, `nullptr` for everything else. Its `DeviceInfoModule` is
 * not reusable here — it answers `getConstants` with a hardcoded 1280x720 and has no way to be told a surface size —
 * so that module is ours; `AnimatedModule` is upstream's, built exactly as `ReactCxxTurboModuleProvider` builds
 * it, from the shared `NativeAnimatedNodesManagerProvider` the host owns. The generated
 * `NativeDeviceInfoCxxSpec`, `NativeAnimatedModuleCxxSpec` and `NativeAppearanceCxxSpec` come from
 * `packages/core/generated`.
 *
 * `DeviceInfo` and `Appearance` are constructed eagerly rather than per lookup, because the frame thread needs a
 * handle to each to emit `didUpdateDimensions` and `appearanceChanged` whether or not JavaScript has ever asked
 * for the module. `AnimatedModule` is built per lookup, which is what upstream does, and is what defers
 * `NativeAnimatedNodesManagerProvider::getOrCreate` — and therefore the `UIManagerBinding` lookup inside it — until
 * JavaScript actually reaches for the module.
 *
 * Threading contract: `install` runs on the JavaScript thread, inside the `initializeRuntime` bindings installer,
 * and so does every factory in the map, because `TurboModuleBinding` only calls the provider from a JavaScript
 * property access. `dimensions`, `publishPendingDimensions` and `appearance` run on the platform frame thread;
 * all three reach JavaScript only through `DimensionsSource`'s mutex and the modules' `CallInvoker`.
 */
class TurboModuleRegistry final {
public:
    TurboModuleRegistry(
        std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
        std::shared_ptr<facebook::react::NativeAnimatedNodesManagerProvider> animatedNodesManagerProvider);

    DimensionsSource& dimensions() noexcept;

    /**
     * The colour-scheme state `Appearance` answers from. Whoever owns the portal connection writes it:
     * `WindowSession` through `AppearancePortal`, the headless golden runner from its `--appearance-golden`
     * argument. See *Appearance and PlatformColor* in docs/cpp-toolchain.md.
     */
    AppearanceModel& appearance() noexcept;

    /**
     * The state behind `Linking.getInitialURL()` and the `url` device event (#363). Whoever decides an activation
     * URL writes it: `WindowSession` seeds it from this process's own launch `argv`, and later
     * `SingleInstanceCoordinator` feeds it a later instance's forwarded `argv`.
     */
    ActivationModel& activation() noexcept;

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
    std::shared_ptr<AppearanceModel> appearanceModel_;
    std::shared_ptr<LinuxAppearanceModule> appearanceModule_;
    std::shared_ptr<ActivationModel> activationModel_;
    std::shared_ptr<LinuxLinkingModule> linkingModule_;
    std::unordered_map<std::string_view, ModuleFactory> moduleFactories_;
};

} // namespace react_native_linux
