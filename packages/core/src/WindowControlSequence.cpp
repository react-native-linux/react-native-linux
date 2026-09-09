#include "WindowControlSequence.h"

#include <charconv>
#include <cstddef>
#include <optional>
#include <system_error>

namespace react_native_linux {
namespace {

constexpr std::string_view kDragPrefix = "Drag:";
constexpr char kTokenOpen = '{';
constexpr char kTokenClose = '}';
constexpr char kExtentSeparator = 'x';
constexpr char kArgumentSeparator = ':';
constexpr uint32_t kMinimumDragConfigures = 2;

struct Extent {
    uint32_t width{0};
    uint32_t height{0};
};

std::optional<uint32_t> parsePositiveNumber(std::string_view text) {
    uint32_t value = 0;
    const std::from_chars_result parsed = std::from_chars(text.data(), text.data() + text.size(), value);

    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value == 0) {
        return std::nullopt;
    }

    return value;
}

std::optional<Extent> parseExtent(std::string_view text) {
    const size_t separator = text.find(kExtentSeparator);

    if (separator == std::string_view::npos) {
        return std::nullopt;
    }

    const std::optional<uint32_t> width = parsePositiveNumber(text.substr(0, separator));
    const std::optional<uint32_t> height = parsePositiveNumber(text.substr(separator + 1));

    if (!width.has_value() || !height.has_value()) {
        return std::nullopt;
    }

    return Extent{.width = width.value(), .height = height.value()};
}

bool applyStateName(std::string_view name, ToplevelState& state) {
    if (name == "Maximized" || name == "Unmaximized") {
        state.maximized = name == "Maximized";

        return true;
    }

    if (name == "Fullscreen" || name == "Unfullscreen") {
        state.fullscreen = name == "Fullscreen";

        return true;
    }

    if (name != "Tiled" && name != "Untiled") {
        return false;
    }

    const bool tiled = name == "Tiled";

    state.tiledLeft = tiled;
    state.tiledRight = tiled;
    state.tiledTop = tiled;
    state.tiledBottom = tiled;

    return true;
}

uint32_t interpolate(uint32_t from, uint32_t to, uint32_t index, uint32_t lastIndex) {
    const int64_t span = static_cast<int64_t>(to) - static_cast<int64_t>(from);

    return static_cast<uint32_t>(static_cast<int64_t>(from) + (span * index) / lastIndex);
}

bool appendDrag(std::string_view arguments, Extent& extent, ToplevelState& state,
                std::vector<WindowControlStep>& steps) {
    const size_t firstSeparator = arguments.find(kArgumentSeparator);

    if (firstSeparator == std::string_view::npos) {
        return false;
    }

    const size_t secondSeparator = arguments.find(kArgumentSeparator, firstSeparator + 1);

    if (secondSeparator == std::string_view::npos) {
        return false;
    }

    const std::optional<Extent> from = parseExtent(arguments.substr(0, firstSeparator));
    const std::optional<Extent> to =
        parseExtent(arguments.substr(firstSeparator + 1, secondSeparator - firstSeparator - 1));
    const std::optional<uint32_t> configures = parsePositiveNumber(arguments.substr(secondSeparator + 1));

    if (!from.has_value() || !to.has_value() || !configures.has_value() ||
        configures.value() < kMinimumDragConfigures) {
        return false;
    }

    const uint32_t lastIndex = configures.value() - 1;

    for (uint32_t index = 0; index <= lastIndex; ++index) {
        extent.width = interpolate(from.value().width, to.value().width, index, lastIndex);
        extent.height = interpolate(from.value().height, to.value().height, index, lastIndex);
        state.resizing = index != lastIndex;
        steps.push_back(WindowControlStep{.width = extent.width, .height = extent.height, .state = state});
    }

    return true;
}

bool appendToken(std::string_view token, Extent& extent, ToplevelState& state, std::vector<WindowControlStep>& steps) {
    if (token.starts_with(kDragPrefix)) {
        return appendDrag(token.substr(kDragPrefix.size()), extent, state, steps);
    }

    state.resizing = false;

    if (const std::optional<Extent> resized = parseExtent(token); resized.has_value()) {
        extent = resized.value();
        steps.push_back(WindowControlStep{.width = extent.width, .height = extent.height, .state = state});

        return true;
    }

    const size_t separator = token.find(kArgumentSeparator);

    if (!applyStateName(token.substr(0, separator), state)) {
        return false;
    }

    if (separator != std::string_view::npos) {
        const std::optional<Extent> resized = parseExtent(token.substr(separator + 1));

        if (!resized.has_value()) {
            return false;
        }

        extent = resized.value();
    }

    steps.push_back(WindowControlStep{.width = extent.width, .height = extent.height, .state = state});

    return true;
}

} // namespace

WindowControlSequence parseWindowControlSequence(std::string_view specification) {
    WindowControlSequence parsed;

    if (specification.empty()) {
        parsed.error = "the window control sequence is empty";

        return parsed;
    }

    Extent extent;
    ToplevelState state;
    size_t position = 0;

    while (position < specification.size()) {
        const size_t close = specification.find(kTokenClose, position);

        if (specification[position] != kTokenOpen || close == std::string_view::npos) {
            parsed.error = "the window control sequence must be brace-delimited tokens";
            parsed.steps.clear();

            return parsed;
        }

        const std::string_view token = specification.substr(position + 1, close - position - 1);

        position = close + 1;

        if (!appendToken(token, extent, state, parsed.steps)) {
            parsed.error = "unknown window control token {" + std::string(token) + "}";
            parsed.steps.clear();

            return parsed;
        }
    }

    return parsed;
}

std::string describeWindowControlStep(const WindowControlStep& step) {
    return "[rnl-geometry] configure " + std::to_string(step.width) + "x" + std::to_string(step.height) +
           " maximized=" + std::to_string(static_cast<int>(step.state.maximized)) +
           " fullscreen=" + std::to_string(static_cast<int>(step.state.fullscreen)) +
           " tiled=" + std::to_string(static_cast<int>(isEffectivelyTiled(step.state))) +
           " resizing=" + std::to_string(static_cast<int>(step.state.resizing));
}

} // namespace react_native_linux
