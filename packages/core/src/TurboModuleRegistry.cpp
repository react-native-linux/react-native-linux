#include "TurboModuleRegistry.h"

#include "PlatformColor.h"

#include <FBReactNativeSpec/FBReactNativeSpecJSI.h>
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

/**
 * `NativeAppearance` answered from this host's `AppearanceModel`, which is where the portal signal and the
 * `setColorScheme` override meet (#260). The module owns no policy of its own: `getColorScheme` is the model's
 * resolved answer, `setColorScheme` hands the model a decoded override — `auto` and `unspecified` from
 * `ColorSchemeOverride` are "no override", which is what clearing means — and `appearanceChanged` is emitted from
 * the model's change listener, so a portal signal and an override change reach JavaScript by one path.
 *
 * `addListener` and `removeListeners` are the `RCTEventEmitter` bookkeeping every event-emitting module's spec
 * carries, and are empty here for the same reason they are in upstream's C++ modules: the emit goes through
 * `emitDeviceEvent`, which needs no subscription count to reach `RCTDeviceEventEmitter`.
 */
class LinuxAppearanceModule final : public facebook::react::NativeAppearanceCxxSpec<LinuxAppearanceModule> {
public:
    LinuxAppearanceModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                          std::shared_ptr<AppearanceModel> appearanceModel)
        : NativeAppearanceCxxSpec(std::move(jsInvoker)), appearanceModel_(std::move(appearanceModel)) {}

    std::string getColorScheme(facebook::jsi::Runtime& /*runtime*/) {
        return std::string(nameOfColorScheme(appearanceModel_->colorScheme()));
    }

    void setColorScheme(facebook::jsi::Runtime& /*runtime*/, const std::string& colorSchemeOverride) {
        appearanceModel_->setColorScheme(colorSchemeFromName(colorSchemeOverride));
    }

    void addListener(facebook::jsi::Runtime& /*runtime*/, const std::string& /*eventName*/) {}

    void removeListeners(facebook::jsi::Runtime& /*runtime*/, double /*count*/) {}

    void emitAppearanceChange(ColorScheme colorScheme) {
        emitDeviceEvent("appearanceChanged",
                        [colorSchemeName = std::string(nameOfColorScheme(colorScheme))](
                            facebook::jsi::Runtime& runtime, std::vector<facebook::jsi::Value>& arguments) {
                            facebook::jsi::Object preferences(runtime);

                            preferences.setProperty(runtime, "colorScheme",
                                                    facebook::jsi::String::createFromUtf8(runtime, colorSchemeName));
                            arguments.emplace_back(std::move(preferences));
                        });
    }

private:
    std::shared_ptr<AppearanceModel> appearanceModel_;
};

/**
 * `PlatformColor('name')`, as the one host function a JavaScript `PlatformColorValueTypes.linux.js` needs.
 *
 * It is a global rather than a module method because `PlatformColor` has no TurboModule spec on any platform:
 * iOS and macOS resolve a semantic name inside their colour parser, and a colour has to be resolvable
 * synchronously from `processColor`, which is not a call site an asynchronous module method can serve. An
 * unrecognised name answers `undefined` rather than a colour, so the JavaScript side throws a named error instead
 * of putting an undefined colour into a prop, which is rn-macos#413.
 *
 * The scheme is read on every call, so there is no resolved value anywhere that could go stale: the re-render an
 * `appearanceChanged` triggers resolves the same names again and gets the new scheme's colours.
 */
void installPlatformColorBinding(facebook::jsi::Runtime& runtime, std::shared_ptr<AppearanceModel> appearanceModel) {
    constexpr unsigned int kPlatformColorArgumentCount = 1;

    runtime.global().setProperty(
        runtime, "__rnlPlatformColor",
        facebook::jsi::Function::createFromHostFunction(
            runtime, facebook::jsi::PropNameID::forAscii(runtime, "__rnlPlatformColor"),
            kPlatformColorArgumentCount,
            [appearanceModel = std::move(appearanceModel)](
                facebook::jsi::Runtime& hostRuntime, const facebook::jsi::Value& /*thisValue*/,
                const facebook::jsi::Value* arguments, size_t count) -> facebook::jsi::Value {
                if (count < kPlatformColorArgumentCount || !arguments[0].isString()) {
                    return facebook::jsi::Value::undefined();
                }

                const std::optional<int32_t> resolved = platformColor(
                    arguments[0].getString(hostRuntime).utf8(hostRuntime), appearanceModel->colorScheme());

                if (!resolved.has_value()) {
                    return facebook::jsi::Value::undefined();
                }

                return facebook::jsi::Value(resolved.value());
            }));
}

TurboModuleRegistry::TurboModuleRegistry(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
    std::shared_ptr<facebook::react::NativeAnimatedNodesManagerProvider> animatedNodesManagerProvider)
    : dimensionsSource_(std::make_shared<DimensionsSource>()),
      deviceInfoModule_(std::make_shared<LinuxDeviceInfoModule>(jsInvoker, dimensionsSource_)),
      appearanceModel_(std::make_shared<AppearanceModel>(kFallbackColorScheme)),
      appearanceModule_(std::make_shared<LinuxAppearanceModule>(jsInvoker, appearanceModel_)) {
    appearanceModel_->setChangeListener([appearanceModule = appearanceModule_.get()](ColorScheme colorScheme) {
        appearanceModule->emitAppearanceChange(colorScheme);
    });
    moduleFactories_.emplace(LinuxDeviceInfoModule::kModuleName,
                             [deviceInfoModule = deviceInfoModule_]() { return deviceInfoModule; });
    moduleFactories_.emplace(LinuxAppearanceModule::kModuleName,
                             [appearanceModule = appearanceModule_]() { return appearanceModule; });
    moduleFactories_.emplace(
        facebook::react::AnimatedModule::kModuleName,
        [jsInvoker = std::move(jsInvoker),
         animatedNodesManagerProvider = std::move(animatedNodesManagerProvider)]() {
            return std::make_shared<facebook::react::AnimatedModule>(jsInvoker, animatedNodesManagerProvider);
        });
}

DimensionsSource& TurboModuleRegistry::dimensions() noexcept { return *dimensionsSource_; }

AppearanceModel& TurboModuleRegistry::appearance() noexcept { return *appearanceModel_; }

void TurboModuleRegistry::install(facebook::jsi::Runtime& runtime) {
    installPlatformColorBinding(runtime, appearanceModel_);
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
