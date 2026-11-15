#include "ExpoDeviceModule.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <array>
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

/**
 * An os-release(5) value as the shell that sources the file reads it, without running one: inside double quotes the
 * escapes `\"`, `\\`, `\$` and `` \` `` give their character and any other backslash stays, inside single quotes
 * everything is literal, and an unquoted value is taken as written.
 */
std::string osReleaseValue(std::string_view written) {
    const bool quoted =
        written.size() >= 2 && written.front() == written.back() && (written.front() == '"' || written.front() == '\'');

    if (!quoted) {
        return std::string(written);
    }

    const std::string_view inner = written.substr(1, written.size() - 2);

    if (written.front() == '\'') {
        return std::string(inner);
    }

    constexpr std::string_view escapable = "\"\\$`";
    std::string value;

    for (std::size_t position = 0; position < inner.size(); ++position) {
        if (inner[position] == '\\' && position + 1 < inner.size() &&
            escapable.find(inner[position + 1]) != std::string_view::npos) {
            ++position;
        }

        value += inner[position];
    }

    return value;
}

/** One `KEY=value` of os-release(5), decoded, from the first of `roots` that has one; an empty value is `null`. */
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

        const std::string value = osReleaseValue(std::string_view(line).substr(prefix.size()));

        return value.empty() ? std::nullopt : std::optional(value);
    }

    return std::nullopt;
}

/** What `getConstants` answers, read from the machine once, when the module is made. */
struct MachineFacts {
    std::optional<std::string> manufacturer;
    std::optional<std::string> modelName;
    double totalMemory;
    std::string osName;
    std::optional<std::string> osVersion;
    std::optional<std::string> osBuildId;
    std::string osInternalBuildId;
    std::string deviceName;
    std::string cpuArchitecture;
};

MachineFacts readMachineFacts(const std::filesystem::path& dmiRoot,
                              const std::vector<std::filesystem::path>& osReleaseRoots) {
    std::array<char, HOST_NAME_MAX + 1> hostName{};
    utsname system{};

    // A failed call leaves the zeroed buffer, an empty string; the size leaves room for the terminator.
    static_cast<void>(gethostname(hostName.data(), hostName.size() - 1));
    static_cast<void>(uname(&system));

    return MachineFacts{
        .manufacturer = firstLine(dmiRoot / "sys_vendor"),
        .modelName = firstLine(dmiRoot / "product_name"),
        .totalMemory = static_cast<double>(sysconf(_SC_PHYS_PAGES)) * static_cast<double>(sysconf(_SC_PAGESIZE)),
        .osName = osReleaseField(osReleaseRoots, "NAME").value_or(system.sysname),
        .osVersion = osReleaseField(osReleaseRoots, "VERSION_ID"),
        .osBuildId = osReleaseField(osReleaseRoots, "BUILD_ID"),
        .osInternalBuildId = system.release,
        .deviceName = hostName.data(),
        .cpuArchitecture = system.machine,
    };
}

facebook::jsi::Value machineConstants(facebook::jsi::Runtime& runtime, const MachineFacts& facts) {
    facebook::jsi::Object constants(runtime);

    constants.setProperty(runtime, "isDevice", true);
    constants.setProperty(runtime, "brand", facebook::jsi::Value::null());
    constants.setProperty(runtime, "manufacturer", stringOrNull(runtime, facts.manufacturer));
    constants.setProperty(runtime, "modelId", facebook::jsi::Value::null());
    constants.setProperty(runtime, "modelName", stringOrNull(runtime, facts.modelName));
    constants.setProperty(runtime, "deviceYearClass", facebook::jsi::Value::null());
    constants.setProperty(runtime, "totalMemory", facts.totalMemory);
    constants.setProperty(runtime, "osName", facebook::jsi::String::createFromUtf8(runtime, facts.osName));
    constants.setProperty(runtime, "osVersion", stringOrNull(runtime, facts.osVersion));
    constants.setProperty(runtime, "osBuildId", stringOrNull(runtime, facts.osBuildId));
    constants.setProperty(runtime, "osInternalBuildId",
                          facebook::jsi::String::createFromUtf8(runtime, facts.osInternalBuildId));
    constants.setProperty(runtime, "deviceName", facebook::jsi::String::createFromUtf8(runtime, facts.deviceName));
    constants.setProperty(runtime, "deviceType", kDesktopDeviceType);
    constants.setProperty(runtime, "supportedCpuArchitectures",
                          facebook::jsi::Array::createWithElements(
                              runtime, facebook::jsi::String::createFromUtf8(runtime, facts.cpuArchitecture)));

    return constants;
}

class ExpoDeviceModule final : public facebook::react::TurboModule {
public:
    ExpoDeviceModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, MachineFacts facts)
        : TurboModule("ExpoDevice", std::move(jsInvoker)), facts_(std::move(facts)) {
        methodMap_["getConstants"] = {0, [](facebook::jsi::Runtime& runtime, TurboModule& turboModule,
                                            const facebook::jsi::Value* /*arguments*/, size_t /*count*/) {
                                          return machineConstants(runtime,
                                                                  static_cast<ExpoDeviceModule&>(turboModule).facts_);
                                      }};
    }

private:
    MachineFacts facts_;
};

} // namespace

std::shared_ptr<facebook::react::TurboModule>
makeExpoDeviceModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
    static const MachineFacts facts = readMachineFacts("/sys/class/dmi/id", {"/etc", "/usr/lib"});

    return std::make_shared<ExpoDeviceModule>(std::move(jsInvoker), facts);
}

std::shared_ptr<facebook::react::TurboModule>
makeExpoDeviceModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, const std::filesystem::path& dmiRoot,
                     const std::vector<std::filesystem::path>& osReleaseRoots) {
    return std::make_shared<ExpoDeviceModule>(std::move(jsInvoker), readMachineFacts(dmiRoot, osReleaseRoots));
}

} // namespace react_native_linux
