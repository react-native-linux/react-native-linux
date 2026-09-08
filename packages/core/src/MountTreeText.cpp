#include "MountTreeText.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace react_native_linux {

namespace {

constexpr size_t kIndentSpacesPerLevel = 2;

/**
 * A coordinate with no trailing zeroes, so `100` renders as `100` rather than as `100.000000` and a fractional
 * layout keeps the digits that distinguish it.
 */
std::string formatCoordinate(facebook::react::Float coordinate) {
    std::string formatted = std::to_string(static_cast<double>(coordinate));

    formatted.erase(formatted.find_last_not_of('0') + 1);

    if (formatted.back() == '.') {
        formatted.pop_back();
    }

    return formatted;
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
        rendered += " testID=\"" + node.testId + "\"";
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
