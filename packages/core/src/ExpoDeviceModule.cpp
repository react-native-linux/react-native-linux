#include "ExpoDeviceModule.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <array>
#include <chrono>
#include <climits>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <jsi/jsi.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <sys/utsname.h>
#include <unistd.h>
#include <utility>
#include <vector>

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

std::optional<std::string> firstLine(const std::filesystem::path& path) {
    std::ifstream stream(path);
    std::string line;

    if (!std::getline(stream, line) || line.empty()) {
        return std::nullopt;
    }

    return line;
}

/** One `KEY=value` of os-release(5), unquoted, from the first of `roots` that has one; an empty value is `null`. */
std::optional<std::string> osReleaseField(const std::vector<std::filesystem::path>& roots, std::string_view key) {
    std::ifstream stream;

    for (const std::filesystem::path& root : roots) {
        stream.open(root / "os-release");

        if (stream.is_open()) {
            break;
        }
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

        return value.empty() ? std::nullopt : std::optional(value);
    }

    return std::nullopt;
}

facebook::jsi::Value machineConstants(facebook::jsi::Runtime& runtime, const std::filesystem::path& dmiRoot,
                                      const std::vector<std::filesystem::path>& osReleaseRoots) {
    std::array<char, HOST_NAME_MAX + 1> hostName{};
    utsname system{};
    facebook::jsi::Object constants(runtime);

    // A failed call leaves the zeroed buffer, an empty string; the size leaves room for the terminator.
    static_cast<void>(gethostname(hostName.data(), hostName.size() - 1));
    static_cast<void>(uname(&system));
    constants.setProperty(runtime, "isDevice", true);
    constants.setProperty(runtime, "brand", facebook::jsi::Value::null());
    constants.setProperty(runtime, "manufacturer", stringOrNull(runtime, firstLine(dmiRoot / "sys_vendor")));
    constants.setProperty(runtime, "modelId", facebook::jsi::Value::null());
    constants.setProperty(runtime, "modelName", stringOrNull(runtime, firstLine(dmiRoot / "product_name")));
    constants.setProperty(runtime, "deviceYearClass", facebook::jsi::Value::null());
    constants.setProperty(runtime, "totalMemory",
                          static_cast<double>(sysconf(_SC_PHYS_PAGES)) * static_cast<double>(sysconf(_SC_PAGESIZE)));
    constants.setProperty(runtime, "osName",
                          facebook::jsi::String::createFromUtf8(
                              runtime, osReleaseField(osReleaseRoots, "NAME").value_or(system.sysname)));
    constants.setProperty(runtime, "osVersion", stringOrNull(runtime, osReleaseField(osReleaseRoots, "VERSION_ID")));
    constants.setProperty(runtime, "osBuildId", stringOrNull(runtime, osReleaseField(osReleaseRoots, "BUILD_ID")));
    constants.setProperty(runtime, "osInternalBuildId", facebook::jsi::String::createFromUtf8(runtime, system.release));
    constants.setProperty(runtime, "deviceName", facebook::jsi::String::createFromUtf8(runtime, hostName.data()));
    constants.setProperty(runtime, "deviceType", kDesktopDeviceType);
    constants.setProperty(runtime, "supportedCpuArchitectures",
                          facebook::jsi::Array::createWithElements(
                              runtime, facebook::jsi::String::createFromUtf8(runtime, system.machine)));

    return constants;
}

facebook::jsi::Value resolvedWith(facebook::jsi::Runtime& runtime, const facebook::jsi::Value& value) {
    const facebook::jsi::Object promise = runtime.global().getPropertyAsObject(runtime, "Promise");

    return promise.getPropertyAsFunction(runtime, "resolve").callWithThis(runtime, promise, value);
}

class ExpoDeviceModule final : public facebook::react::TurboModule {
public:
    ExpoDeviceModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, std::filesystem::path dmiRoot,
                     std::vector<std::filesystem::path> osReleaseRoots)
        : TurboModule("ExpoDevice", std::move(jsInvoker)), dmiRoot_(std::move(dmiRoot)),
          osReleaseRoots_(std::move(osReleaseRoots)) {
        methodMap_["getConstants"] = {
            0, [](facebook::jsi::Runtime& runtime, TurboModule& turboModule, const facebook::jsi::Value* /*arguments*/,
                  size_t /*count*/) { return static_cast<ExpoDeviceModule&>(turboModule).constants(runtime); }};
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

private:
    facebook::jsi::Value constants(facebook::jsi::Runtime& runtime) const {
        return machineConstants(runtime, dmiRoot_, osReleaseRoots_);
    }

    std::filesystem::path dmiRoot_;
    std::vector<std::filesystem::path> osReleaseRoots_;
};

} // namespace

std::shared_ptr<facebook::react::TurboModule>
makeExpoDeviceModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, std::filesystem::path dmiRoot,
                     std::vector<std::filesystem::path> osReleaseRoots) {
    return std::make_shared<ExpoDeviceModule>(std::move(jsInvoker), std::move(dmiRoot), std::move(osReleaseRoots));
}

} // namespace react_native_linux
