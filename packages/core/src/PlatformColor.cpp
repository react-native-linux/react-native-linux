#include "PlatformColor.h"

#include <array>

namespace react_native_linux {

namespace {

struct PlatformColorEntry {
    std::string_view name;
    int32_t lightArgb;
    int32_t darkArgb;
};

constexpr std::array<PlatformColorEntry, 6> kPlatformColors{{
    {.name = "labelColor",
     .lightArgb = static_cast<int32_t>(0xFF1B1F23),
     .darkArgb = static_cast<int32_t>(0xFFE6EDF3)},
    {.name = "secondaryLabelColor",
     .lightArgb = static_cast<int32_t>(0xFF5C6370),
     .darkArgb = static_cast<int32_t>(0xFF8B949E)},
    {.name = "windowBackgroundColor",
     .lightArgb = static_cast<int32_t>(0xFFF5F6F7),
     .darkArgb = static_cast<int32_t>(0xFF0D1117)},
    {.name = "controlBackgroundColor",
     .lightArgb = static_cast<int32_t>(0xFFFFFFFF),
     .darkArgb = static_cast<int32_t>(0xFF161B22)},
    {.name = "separatorColor",
     .lightArgb = static_cast<int32_t>(0xFFD0D7DE),
     .darkArgb = static_cast<int32_t>(0xFF30363D)},
    {.name = "linkColor",
     .lightArgb = static_cast<int32_t>(0xFF0969DA),
     .darkArgb = static_cast<int32_t>(0xFF58A6FF)},
}};

} // namespace

std::optional<int32_t> platformColor(std::string_view platformColorName, ColorScheme colorScheme) {
    for (const PlatformColorEntry& entry : kPlatformColors) {
        if (entry.name == platformColorName) {
            return colorScheme == ColorScheme::Dark ? entry.darkArgb : entry.lightArgb;
        }
    }

    return std::nullopt;
}

} // namespace react_native_linux
