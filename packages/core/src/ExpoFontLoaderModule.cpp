#include "ExpoFontLoaderModule.h"

#include "TextGeometry.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <ReactCommon/TurboModuleUtils.h>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <jsi/jsi.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace react_native_linux {
namespace {

constexpr std::string_view kFileScheme = "file://";
constexpr size_t kLoadArgumentCount = 2;

/**
 * Upstream's iOS messages, so an app's error handling reads the same text on Linux. Every face is decoded before any
 * registers, so a family that fails partway lists nothing and upstream's `isLoaded` lets a corrected retry through.
 */
void registerFontFiles(const std::string& fontFamily, const std::vector<std::string>& localUris) {
    std::vector<std::string> fontPaths;

    for (const std::string& localUri : localUris) {
        std::string fontPath = localUri.starts_with(kFileScheme) ? localUri.substr(kFileScheme.size()) : localUri;

        if (!std::filesystem::is_regular_file(fontPath)) {
            throw std::runtime_error("Font file '" + localUri + "' doesn't exist");
        }

        fontPaths.push_back(std::move(fontPath));
    }

    if (!registerApplicationFont(fontFamily, fontPaths)) {
        throw std::runtime_error("Could not create font from loaded data for '" + fontFamily + "'");
    }
}

std::vector<std::string> faceUris(facebook::jsi::Runtime& runtime, const std::string& fontFamily,
                                  const facebook::jsi::Array& faces) {
    std::vector<std::string> uris;

    if (faces.size(runtime) == 0) {
        throw std::runtime_error("Could not load font family '" + fontFamily +
                                 "' because no faces were provided. Pass at least one font face with a 'localUri' to "
                                 "'loadFontFamilyAsync'");
    }

    for (size_t index = 0; index < faces.size(runtime); ++index) {
        const facebook::jsi::Value localUri =
            faces.getValueAtIndex(runtime, index).asObject(runtime).getProperty(runtime, "localUri");

        if (!localUri.isString()) {
            throw std::runtime_error("Could not load font family '" + fontFamily +
                                     "' because one of its faces has no 'localUri'. Make sure every face passed to "
                                     "'loadFontFamilyAsync' includes a font file URI");
        }

        uris.push_back(localUri.getString(runtime).utf8(runtime));
    }

    return uris;
}

/** Settles as upstream's `AsyncFunction` does: a thrown error rejects with its message. */
template <typename Registration>
facebook::jsi::Value settle(facebook::jsi::Runtime& runtime, Registration registration) {
    return facebook::react::createPromiseAsJSIValue(
        runtime, [&registration](facebook::jsi::Runtime& /*promiseRuntime*/,
                                 const std::shared_ptr<facebook::react::Promise>& promise) {
            try {
                registration();
                promise->resolve(facebook::jsi::Value::undefined());
            } catch (const std::exception& error) {
                promise->reject(error.what());
            }
        });
}

class ExpoFontLoaderModule final : public facebook::react::TurboModule {
public:
    explicit ExpoFontLoaderModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
        : TurboModule("ExpoFontLoader", std::move(jsInvoker)) {
        methodMap_["getLoadedFonts"] = {0, &getLoadedFonts};
        methodMap_["loadAsync"] = {kLoadArgumentCount, &loadAsync};
        methodMap_["loadFontFamilyAsync"] = {kLoadArgumentCount, &loadFontFamilyAsync};
    }

private:
    static facebook::jsi::Value getLoadedFonts(facebook::jsi::Runtime& runtime, TurboModule& /*turboModule*/,
                                               const facebook::jsi::Value* /*arguments*/, size_t /*count*/) {
        const std::vector<std::string> families = applicationFontFamilies();
        facebook::jsi::Array loadedFonts(runtime, families.size());

        for (size_t index = 0; index < families.size(); ++index) {
            loadedFonts.setValueAtIndex(runtime, index,
                                        facebook::jsi::String::createFromUtf8(runtime, families[index]));
        }

        return loadedFonts;
    }

    static facebook::jsi::Value loadAsync(facebook::jsi::Runtime& runtime, TurboModule& /*turboModule*/,
                                          const facebook::jsi::Value* arguments, size_t count) {
        if (count < kLoadArgumentCount) {
            throw facebook::jsi::JSError(runtime, "ExpoFontLoader.loadAsync takes a fontFamily and a localUri");
        }

        const std::string fontFamily = arguments[0].asString(runtime).utf8(runtime);
        const std::string localUri = arguments[1].asString(runtime).utf8(runtime);

        return settle(runtime, [&]() { registerFontFiles(fontFamily, {localUri}); });
    }

    static facebook::jsi::Value loadFontFamilyAsync(facebook::jsi::Runtime& runtime, TurboModule& /*turboModule*/,
                                                    const facebook::jsi::Value* arguments, size_t count) {
        if (count < kLoadArgumentCount) {
            throw facebook::jsi::JSError(runtime, "ExpoFontLoader.loadFontFamilyAsync takes a fontFamily and faces");
        }

        const std::string fontFamily = arguments[0].asString(runtime).utf8(runtime);
        const facebook::jsi::Array faces = arguments[1].asObject(runtime).asArray(runtime);

        return settle(runtime, [&]() { registerFontFiles(fontFamily, faceUris(runtime, fontFamily, faces)); });
    }
};

} // namespace

std::shared_ptr<facebook::react::TurboModule>
makeExpoFontLoaderModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
    return std::make_shared<ExpoFontLoaderModule>(std::move(jsInvoker));
}

} // namespace react_native_linux
