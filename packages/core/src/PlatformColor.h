#pragma once

#include "Appearance.h"

#include <cstdint>
#include <optional>
#include <string_view>

namespace react_native_linux {

/**
 * `PlatformColor('name')` for Linux: the semantic colour names this platform answers, resolved against a colour
 * scheme.
 *
 * Issue #52 carried `needs:decision` for the token set, and the decision is the small one. macOS and iOS expose
 * the whole `NSColor`/`UIColor` catalogue and then spend issues on the parts of it Fabric never wired up
 * (rn-macos#2736, rn-macos#1846); Linux has no system catalogue to mirror — the XDG portal publishes a colour
 * *scheme* and an accent colour, not a palette — so inventing a hundred names to answer would be inventing a
 * hundred colours. These six are the ones a desktop app cannot draw a window without, and an unrecognised name
 * resolves to nothing so the caller can throw a named error rather than let an undefined colour reach the scene,
 * which is rn-macos#413.
 *
 * The return is packed ARGB in the `HostPlatformColor` representation this platform uses for every other colour,
 * so a resolved value goes into a prop unchanged.
 *
 * This is a pure table on purpose: a scheme change resolves the same names again against the new scheme, and
 * nothing is cached anywhere it could go stale. A cache that cannot be invalidated is the whole of the bug
 * cluster the issue was filed for.
 */
std::optional<int32_t> platformColor(std::string_view platformColorName, ColorScheme colorScheme);

} // namespace react_native_linux
