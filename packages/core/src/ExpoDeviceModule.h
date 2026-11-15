#pragma once

#include <filesystem>
#include <memory>
#include <vector>

namespace facebook::react {

class CallInvoker;
class TurboModule;

} // namespace facebook::react

namespace react_native_linux {

/**
 * `ExpoDevice` (ADR-0004, #277), which upstream's unmodified `expo-device` requires as it is imported, answered for
 * the machine the app runs on: `manufacturer` and `modelName` from the DMI root (`null` where the firmware has
 * no DMI table), `osName`, `osVersion` and `osBuildId` from the `os-release` of the first os-release root that has
 * one (decoded as the shell reads it; an empty value is `null`), `osInternalBuildId` and `supportedCpuArchitectures`
 * from `uname`, `totalMemory` from `sysconf`, the host name as `deviceName`, and `DeviceType.DESKTOP`. `brand`,
 * `modelId` and `deviceYearClass` have no desktop source and answer `null`. Only `getConstants` is answered: T3 Code
 * calls no query, so upstream rejects `getDeviceTypeAsync`, `getUptimeAsync`, `isRootedExperimentalAsync` and the
 * Android-only ones with its own `UnavailabilityError`.
 *
 * The machine is read when the module is made and `getConstants` only serializes what was read. It stays synchronous
 * because upstream reads every constant through `getConstants` as `expo-device` is imported. This overload reads
 * `/sys/class/dmi/id`, `/etc` and `/usr/lib` once per process, so every module it makes shares one reading.
 *
 * Threading contract: the JavaScript thread only, like every TurboModule method here; the one process-wide reading
 * is a function-local static, so its first construction is thread-safe.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoDeviceModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

/** The same module over `dmiRoot` and `osReleaseRoots`, read once as it is made, for fixture machines. */
std::shared_ptr<facebook::react::TurboModule>
makeExpoDeviceModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, const std::filesystem::path& dmiRoot,
                     const std::vector<std::filesystem::path>& osReleaseRoots);

} // namespace react_native_linux
