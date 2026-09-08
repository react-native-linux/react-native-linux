#include "MountTreeText.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <string>
#include <vector>

namespace react_native_linux {

namespace {

constexpr size_t kIndentSpacesPerLevel = 2;

// The shortest round-trip form of a `float` is at most fifteen characters, sign and exponent included, so
// `std::to_chars` below cannot run out of room and its result never has to be checked.
constexpr size_t kCoordinateCapacity = 32;

/**
 * A coordinate in the shortest form that reads back as the same number: `100` renders as `100` rather than as
 * `100.000000`, and two coordinates that differ render differently. `std::to_string` cannot do the second — it
 * prints six fractional digits and nothing beyond them, so it collapses `0.1234567` and `0.1234568` onto one
 * string and a moved node would assert as unmoved.
 */
std::string formatCoordinate(facebook::react::Float coordinate) {
    std::array<char, kCoordinateCapacity> buffer{};
    const std::to_chars_result formatted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), coordinate);

    return std::string(buffer.data(), formatted.ptr);
}

/**
 * The three characters an attribute value cannot carry literally. A `testID` is authored in JavaScript and is
 * whatever the bundle put there, so an unescaped one could close the attribute and make the rendered tree parse
 * as a different tree than the one that mounted.
 */
std::string escapeAttributeValue(const std::string& value) {
    std::string escaped;

    for (char character : value) {
        switch (character) {
        case '&':
            escaped += "&amp;";
            break;
        case '<':
            escaped += "&lt;";
            break;
        case '"':
            escaped += "&quot;";
            break;
        default:
            escaped.push_back(character);
            break;
        }
    }

    return escaped;
}

std::string formatFrame(const facebook::react::Rect& frame) {
    return "{x:" + formatCoordinate(frame.origin.x) + ",y:" + formatCoordinate(frame.origin.y) +
           ",width:" + formatCoordinate(frame.size.width) + ",height:" + formatCoordinate(frame.size.height) + "}";
}

std::string elementName(const SceneNode& node) {
    std::string name = "rn-";

    for (char character : node.componentName) {
        name.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }

    return name;
}

void appendNode(const SceneNodes& nodes, facebook::react::Tag tag, size_t depth, std::string& rendered) {
    const auto entry = nodes.find(tag);

    if (entry == nodes.end()) {
        return;
    }

    const SceneNode& node = entry->second;
    const std::string indent(depth * kIndentSpacesPerLevel, ' ');
    const std::string name = elementName(node);

    rendered += indent + "<" + name + " layoutMetrics-frame=\"" + formatFrame(node.layoutMetrics.frame) + "\"";

    if (!node.testId.empty()) {
        rendered += " testID=\"" + escapeAttributeValue(node.testId) + "\"";
    }

    if (node.childTags.empty()) {
        rendered += " />\n";

        return;
    }

    rendered += ">\n";

    for (facebook::react::Tag childTag : node.childTags) {
        appendNode(nodes, childTag, depth + 1, rendered);
    }

    rendered += indent + "</" + name + ">\n";
}

} // namespace

std::string renderMountTree(const SceneNodes& nodes) {
    std::vector<facebook::react::Tag> rootTags;

    for (const auto& [tag, node] : nodes) {
        if (node.parentTag == 0) {
            rootTags.insert(std::ranges::upper_bound(rootTags, tag), tag);
        }
    }

    std::string rendered;

    for (facebook::react::Tag rootTag : rootTags) {
        appendNode(nodes, rootTag, 0, rendered);
    }

    return rendered;
}

} // namespace react_native_linux
