#pragma once

#include <memory>

namespace facebook::react {

class CallInvoker;
class TurboModule;

} // namespace facebook::react

namespace react_native_linux {

/**
 * `ExpoDevice` (ADR-0004, #277), which upstream's unmodified `expo-device` requires as it is imported, answered for
 * the machine the app runs on: `manufacturer` and `modelName` from `/sys/class/dmi/id` (`null` where the firmware has
 * no DMI table), `osName`, `osVersion` and `osBuildId` from `os-release`, `osInternalBuildId` and
 * `supportedCpuArchitectures` from `uname`, `totalMemory` from `sysconf`, the host name as `deviceName`, and
 * `DeviceType.DESKTOP`. `brand`, `modelId` and `deviceYearClass` have no desktop source and answer `null`.
 * `getUptimeAsync` is the monotonic clock, which like iOS's and Android's excludes suspend, and
 * `isRootedExperimentalAsync` answers whether the app runs as root. The Android-only queries are not answered, so
 * upstream rejects each with its own `UnavailabilityError`.
 *
 * Threading contract: the JavaScript thread only, like every TurboModule method here.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoDeviceModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

} // namespace react_native_linux
