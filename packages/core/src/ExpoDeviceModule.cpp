#include "ExpoDeviceModule.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <ReactCommon/TurboModuleUtils.h>
#include <array>
#include <chrono>
#include <climits>
#include <cstddef>
#include <fstream>
#include <jsi/jsi.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <sys/utsname.h>
#include <unistd.h>
#include <utility>

namespace react_native_linux {
namespace {

/** Upstream's `DeviceType.DESKTOP`. */
constexpr int kDesktopDeviceType = 3;

facebook::jsi::Value stringOrNull(facebook::jsi::Runtime& runtime, const std::optional<std::string>& value) {
    if (!value.has_value()) {
        return facebook::jsi::Value::null();
    }

    return facebook::jsi::String::createFromUtf8(runtime, value.value());
}

std::optional<std::string> firstLine(const char* path) {
    std::ifstream stream(path);
    std::string line;

    if (!std::getline(stream, line) || line.empty()) {
        return std::nullopt;
    }

    return line;
}

/** One `KEY=value` of os-release(5), unquoted: `/etc/os-release`, or `/usr/lib/os-release` when that is missing. */
std::optional<std::string> osReleaseField(std::string_view key) {
    std::ifstream stream("/etc/os-release");

    if (!stream.is_open()) {
        stream.open("/usr/lib/os-release");
    }

    const std::string prefix = std::string(key) + "=";

    for (std::string line; std::getline(stream, line);) {
        if (!line.starts_with(prefix)) {
            continue;
        }

        std::string value = line.substr(prefix.size());

        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') && value.back() == value.front()) {
            value = value.substr(1, value.size() - 2);
        }

        return value;
    }

    return std::nullopt;
}

facebook::jsi::Value resolvedWith(facebook::jsi::Runtime& runtime, facebook::jsi::Value value) {
    return facebook::react::createPromiseAsJSIValue(
        runtime, [&value](facebook::jsi::Runtime& /*promiseRuntime*/,
                          const std::shared_ptr<facebook::react::Promise>& promise) { promise->resolve(value); });
}

facebook::jsi::Value constants(facebook::jsi::Runtime& runtime) {
    std::array<char, HOST_NAME_MAX + 1> hostName{};
    utsname system{};
    facebook::jsi::Object constants(runtime);

    // A failed call leaves the zeroed buffer, an empty string; the size leaves room for the terminator.
    static_cast<void>(gethostname(hostName.data(), hostName.size() - 1));
    static_cast<void>(uname(&system));
    constants.setProperty(runtime, "isDevice", true);
    constants.setProperty(runtime, "brand", facebook::jsi::Value::null());
    constants.setProperty(runtime, "manufacturer", stringOrNull(runtime, firstLine("/sys/class/dmi/id/sys_vendor")));
    constants.setProperty(runtime, "modelId", facebook::jsi::Value::null());
    constants.setProperty(runtime, "modelName", stringOrNull(runtime, firstLine("/sys/class/dmi/id/product_name")));
    constants.setProperty(runtime, "deviceYearClass", facebook::jsi::Value::null());
    constants.setProperty(runtime, "totalMemory",
                          static_cast<double>(sysconf(_SC_PHYS_PAGES)) * static_cast<double>(sysconf(_SC_PAGESIZE)));
    constants.setProperty(
        runtime, "osName",
        facebook::jsi::String::createFromUtf8(runtime, osReleaseField("NAME").value_or(system.sysname)));
    constants.setProperty(runtime, "osVersion", stringOrNull(runtime, osReleaseField("VERSION_ID")));
    constants.setProperty(runtime, "osBuildId", stringOrNull(runtime, osReleaseField("BUILD_ID")));
    constants.setProperty(runtime, "osInternalBuildId", facebook::jsi::String::createFromUtf8(runtime, system.release));
    constants.setProperty(runtime, "deviceName", facebook::jsi::String::createFromUtf8(runtime, hostName.data()));
    constants.setProperty(runtime, "deviceType", kDesktopDeviceType);
    constants.setProperty(runtime, "supportedCpuArchitectures",
                          facebook::jsi::Array::createWithElements(
                              runtime, facebook::jsi::String::createFromUtf8(runtime, system.machine)));

    return constants;
}

class ExpoDeviceModule final : public facebook::react::TurboModule {
public:
    explicit ExpoDeviceModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
        : TurboModule("ExpoDevice", std::move(jsInvoker)) {
        methodMap_["getConstants"] = {0, [](facebook::jsi::Runtime& runtime, TurboModule& /*turboModule*/,
                                            const facebook::jsi::Value* /*arguments*/,
                                            size_t /*count*/) { return constants(runtime); }};
        methodMap_["getDeviceTypeAsync"] = {0, [](facebook::jsi::Runtime& runtime, TurboModule& /*turboModule*/,
                                                  const facebook::jsi::Value* /*arguments*/, size_t /*count*/) {
                                                return resolvedWith(runtime, kDesktopDeviceType);
                                            }};
        methodMap_["getUptimeAsync"] = {0, [](facebook::jsi::Runtime& runtime, TurboModule& /*turboModule*/,
                                              const facebook::jsi::Value* /*arguments*/, size_t /*count*/) {
                                            const auto uptime = std::chrono::duration<double, std::milli>(
                                                std::chrono::steady_clock::now().time_since_epoch());

                                            return resolvedWith(runtime, uptime.count());
                                        }};
        methodMap_["isRootedExperimentalAsync"] = {0, [](facebook::jsi::Runtime& runtime, TurboModule& /*turboModule*/,
                                                         const facebook::jsi::Value* /*arguments*/, size_t /*count*/) {
                                                       return resolvedWith(runtime, geteuid() == 0);
                                                   }};
    }
};

} // namespace

std::shared_ptr<facebook::react::TurboModule>
makeExpoDeviceModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
    return std::make_shared<ExpoDeviceModule>(std::move(jsInvoker));
}

} // namespace react_native_linux
