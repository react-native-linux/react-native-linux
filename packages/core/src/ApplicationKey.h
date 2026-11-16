#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace react_native_linux {

/** The key Expo's `registerRootComponent` registers and leaves its host to run. */
inline constexpr std::string_view kDefaultApplicationKey = "main";

/**
 * The `AppRegistry` key a window runs once its bundle has loaded (#777), the name Android's `getMainComponentName`
 * and react-native-windows' `ReactViewOptions.ComponentName` give their hosts. `requestedKey` is `--module`'s, run
 * whether or not the bundle registered it, so a misspelt key fails in `AppRegistry.runApplication` instead of
 * leaving the window blank. Without one it is `kDefaultApplicationKey` once the bundle registered it, and otherwise
 * none: the empty surface a bundle commits into by hand.
 *
 * Pure, and free of JSI, so it sits inside the `rnl_core_tests` coverage gate.
 */
std::optional<std::string> selectApplicationKey(const std::optional<std::string>& requestedKey,
                                                const std::vector<std::string>& registeredKeys);

} // namespace react_native_linux
