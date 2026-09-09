#include "PinnedFontFamilies.h"

#include <sstream>

namespace react_native_linux {

std::optional<std::string> pinnedFontFamiliesFatalMessage(const std::vector<PinnedFontFamilyResolution>& resolutions) {
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

bool resolvedStyleMatchesExactly(int weight, int width, int slant, int expectedWeight, int expectedWidth,
                                 int expectedSlant) {
    return weight == expectedWeight && width == expectedWidth && slant == expectedSlant;
}

} // namespace react_native_linux
