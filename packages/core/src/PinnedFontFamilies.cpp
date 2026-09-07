#include "PinnedFontFamilies.h"

#include <sstream>

namespace react_native_linux {

namespace {

// Mirrors `SkFontStyle`'s own default encoding (weight, width, slant), documented on
// `resolvedStyleIsPinnedDefault` in the header: kept as plain ints here rather than including Skia so this
// file — and the wrong-face regression test it makes possible — stay Skia-free.
constexpr int kPinnedDefaultWeight = 400;
constexpr int kPinnedDefaultWidth = 5;
constexpr int kPinnedDefaultSlant = 0;

}  // namespace

std::optional<std::string> pinnedFontFamiliesFatalMessage(
    const std::vector<PinnedFontFamilyResolution>& resolutions) {
    std::vector<std::string> missingFamilies;

    for (const PinnedFontFamilyResolution& resolution : resolutions) {
        if (!resolution.resolvedFromAssetManager) {
            missingFamilies.push_back(resolution.family);
        }
    }

    if (missingFamilies.empty()) {
        return std::nullopt;
    }

    std::ostringstream message;

    message << "[text] fatal: packages/core/fonts is missing the face(s) scripts/fonts.lock.json pins for ";

    for (std::size_t index = 0; index < missingFamilies.size(); ++index) {
        if (index > 0) {
            message << ", ";
        }

        message << "\"" << missingFamilies[index] << "\"";
    }

    message << "; run \"pnpm --filter @react-native-linux/core vendor:fonts\" and retry";

    return message.str();
}

bool resolvedStyleIsPinnedDefault(int weight, int width, int slant) {
    return weight == kPinnedDefaultWeight && width == kPinnedDefaultWidth && slant == kPinnedDefaultSlant;
}

}  // namespace react_native_linux
