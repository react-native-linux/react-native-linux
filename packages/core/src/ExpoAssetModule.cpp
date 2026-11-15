#include "ExpoAssetModule.h"

#include "CurlHttpClient.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <ReactCommon/TurboModuleUtils.h>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <functional>
#include <jsi/jsi.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace react_native_linux {
namespace {

constexpr std::string_view kModuleName = "ExpoAsset";
constexpr std::string_view kFileScheme = "file://";
constexpr size_t kDownloadArgumentCount = 3;

/** `$XDG_CACHE_HOME`, or `$HOME/.cache` when it is unset or not absolute, as the XDG Base Directory spec asks. */
std::filesystem::path assetCacheDirectory() {
    const char* cacheHome = std::getenv("XDG_CACHE_HOME");
    const char* home = std::getenv("HOME");
    const std::filesystem::path base = cacheHome != nullptr && cacheHome[0] == '/'
                                           ? std::filesystem::path(cacheHome)
                                           : std::filesystem::path(home == nullptr ? "" : home) / ".cache";

    return base / "react-native-linux" / "expo-asset";
}

/** Written beside the target and renamed over it, so a cached file is never a partial download. */
void saveAsset(const std::filesystem::path& localPath, const std::string& contents) {
    const std::filesystem::path temporaryPath = localPath.string() + ".download";
    std::error_code ignored;

    std::filesystem::create_directories(localPath.parent_path(), ignored);

    std::ofstream stream(temporaryPath, std::ios::binary);

    stream << contents;
    stream.close();

    if (stream.fail()) {
        throw std::runtime_error("Unable to save asset to directory: '" + std::string(kFileScheme) +
                                 localPath.parent_path().string() + "'");
    }

    std::filesystem::rename(temporaryPath, localPath);
}

/**
 * Upstream reuses a cached file named after the asset's MD5 hash, or after the URL's when the asset has none. Without
 * an MD5 here, an unhashed asset is named after a hash of its URL, which keys the same reuse.
 */
std::string downloadAsset(const std::string& url, const std::optional<std::string>& md5Hash, const std::string& type) {
    if (url.starts_with(kFileScheme)) {
        return url;
    }

    const std::string cacheFileId = md5Hash.value_or(std::to_string(std::hash<std::string>{}(url)));
    const std::filesystem::path localPath = assetCacheDirectory() / ("ExponentAsset-" + cacheFileId + "." + type);

    if (!std::filesystem::exists(localPath)) {
        std::string contents;

        try {
            contents = fetchBundle(url);
        } catch (const std::exception&) {
            throw std::runtime_error("Unable to download asset from url: '" + url + "'");
        }

        saveAsset(localPath, contents);
    }

    return std::string(kFileScheme) + localPath.string();
}

class ExpoAssetModule final : public facebook::react::TurboModule {
public:
    explicit ExpoAssetModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
        : TurboModule(std::string(kModuleName), std::move(jsInvoker)) {
        methodMap_["downloadAsync"] = {kDownloadArgumentCount, &downloadAsync};
    }

private:
    static facebook::jsi::Value downloadAsync(facebook::jsi::Runtime& runtime, TurboModule& /*turboModule*/,
                                              const facebook::jsi::Value* arguments, size_t count) {
        if (count < kDownloadArgumentCount) {
            throw facebook::jsi::JSError(runtime, "ExpoAsset.downloadAsync takes a url, an md5Hash and a type");
        }

        const std::string url = arguments[0].asString(runtime).utf8(runtime);
        const std::optional<std::string> md5Hash =
            arguments[1].isString() ? std::optional(arguments[1].getString(runtime).utf8(runtime)) : std::nullopt;
        const std::string type = arguments[2].asString(runtime).utf8(runtime);

        return facebook::react::createPromiseAsJSIValue(
            runtime,
            [&](facebook::jsi::Runtime& promiseRuntime, const std::shared_ptr<facebook::react::Promise>& promise) {
                try {
                    promise->resolve(
                        facebook::jsi::String::createFromUtf8(promiseRuntime, downloadAsset(url, md5Hash, type)));
                } catch (const std::exception& error) {
                    promise->reject(error.what());
                }
            });
    }
};

} // namespace

std::shared_ptr<facebook::react::TurboModule>
makeExpoAssetModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
    return std::make_shared<ExpoAssetModule>(std::move(jsInvoker));
}

} // namespace react_native_linux
