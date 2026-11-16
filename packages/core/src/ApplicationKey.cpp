#include "ApplicationKey.h"

#include <algorithm>

namespace react_native_linux {

std::optional<std::string> selectApplicationKey(const std::optional<std::string>& requestedKey,
                                                const std::vector<std::string>& registeredKeys) {
    if (requestedKey.has_value()) {
        return requestedKey;
    }

    if (std::ranges::find(registeredKeys, kDefaultApplicationKey) != registeredKeys.end()) {
        return std::string(kDefaultApplicationKey);
    }

    return std::nullopt;
}

} // namespace react_native_linux
