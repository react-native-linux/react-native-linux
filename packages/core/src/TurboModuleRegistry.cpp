#include "TurboModuleRegistry.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <ReactCommon/TurboModuleBinding.h>
#include <react/coremodules/DeviceInfoModule.h>
#include <react/renderer/animated/AnimatedModule.h>
#include <react/renderer/animated/NativeAnimatedNodesManagerProvider.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {

/**
 * `NativeDeviceInfo` answered from this surface's `DimensionsSource` instead of from a platform-wide lookup, which
 * is the bug this module exists not to have (rn-macos#2296).
 *
 * `windowPhysicalPixels` and `screenPhysicalPixels` stay absent: they are the Android half of the payload, and the
 * generated bridging omits an empty `std::optional` rather than sending a null JavaScript would have to defend
 * against.
 */
class LinuxDeviceInfoModule final : public facebook::react::NativeDeviceInfoCxxSpec<LinuxDeviceInfoModule> {
public:
    LinuxDeviceInfoModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                          std::shared_ptr<DimensionsSource> dimensionsSource)
        : NativeDeviceInfoCxxSpec(std::move(jsInvoker)), dimensionsSource_(std::move(dimensionsSource)) {}

    facebook::react::DeviceInfoConstants getConstants(facebook::jsi::Runtime& /*runtime*/) {
        return facebook::react::DeviceInfoConstants{.Dimensions = toPayload(dimensionsSource_->metrics())};
    }

    void emitDimensionsChange(const DisplayMetrics& metrics) {
        emitDeviceEvent("didUpdateDimensions",
                        [payload = toPayload(metrics), jsInvoker = jsInvoker_](
                            facebook::jsi::Runtime& runtime, std::vector<facebook::jsi::Value>& arguments) {
                            arguments.emplace_back(facebook::react::bridging::toJs(runtime, payload, jsInvoker));
                        });
    }

private:
    static facebook::react::DimensionsPayload toPayload(const DisplayMetrics& metrics) {
        const facebook::react::DisplayMetrics displayMetrics{metrics.width, metrics.height, metrics.scale,
                                                             metrics.fontScale};

        return facebook::react::DimensionsPayload{.window = displayMetrics, .screen = displayMetrics};
    }

    std::shared_ptr<DimensionsSource> dimensionsSource_;
};

TurboModuleRegistry::TurboModuleRegistry(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
    std::shared_ptr<facebook::react::NativeAnimatedNodesManagerProvider> animatedNodesManagerProvider)
    : dimensionsSource_(std::make_shared<DimensionsSource>()),
      deviceInfoModule_(std::make_shared<LinuxDeviceInfoModule>(jsInvoker, dimensionsSource_)) {
    moduleFactories_.emplace(LinuxDeviceInfoModule::kModuleName,
                             [deviceInfoModule = deviceInfoModule_]() { return deviceInfoModule; });
    moduleFactories_.emplace(
        facebook::react::AnimatedModule::kModuleName,
        [jsInvoker = std::move(jsInvoker),
         animatedNodesManagerProvider = std::move(animatedNodesManagerProvider)]() {
            return std::make_shared<facebook::react::AnimatedModule>(jsInvoker, animatedNodesManagerProvider);
        });
}

DimensionsSource& TurboModuleRegistry::dimensions() noexcept { return *dimensionsSource_; }

void TurboModuleRegistry::install(facebook::jsi::Runtime& runtime) {
    facebook::react::TurboModuleBinding::install(
        runtime,
        [moduleFactories = moduleFactories_](facebook::jsi::Runtime& /*runtime*/, const std::string& name)
            -> std::shared_ptr<facebook::react::TurboModule> {
            const auto moduleFactory = moduleFactories.find(name);

            if (moduleFactory == moduleFactories.end()) {
                return nullptr;
            }

            return moduleFactory->second();
        });
}

void TurboModuleRegistry::publishPendingDimensions() {
    const std::optional<DisplayMetrics> change = dimensionsSource_->takeChangeIfAny();

    if (change.has_value()) {
        deviceInfoModule_->emitDimensionsChange(change.value());
    }
}

} // namespace react_native_linux
