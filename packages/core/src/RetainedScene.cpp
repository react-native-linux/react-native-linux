#include "RetainedScene.h"

#include <react/renderer/components/root/RootShadowNode.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/Rect.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace react_native_linux {

namespace {

template <typename Nodes>
void eraseChildTag(Nodes& nodes, facebook::react::Tag parentTag, facebook::react::Tag childTag) {
    const auto parent = nodes.find(parentTag);

    if (parent == nodes.end()) {
        return;
    }

    std::vector<facebook::react::Tag>& childTags = parent->second.childTags;
    childTags.erase(std::remove(childTags.begin(), childTags.end(), childTag), childTags.end());
}

constexpr size_t kFrameBufferSize = 128;
constexpr size_t kIndentWidth = 2;

std::string formatFrame(const facebook::react::Rect& frame) {
    std::array<char, kFrameBufferSize> buffer{};

    std::snprintf(buffer.data(), buffer.size(), "(%.2f, %.2f, %.2f, %.2f)", static_cast<double>(frame.origin.x),
                  static_cast<double>(frame.origin.y), static_cast<double>(frame.size.width),
                  static_cast<double>(frame.size.height));

    return buffer.data();
}

std::optional<facebook::react::SharedColor> readBackgroundColor(const facebook::react::ShadowView& shadowView) {
    const std::shared_ptr<const facebook::react::ViewProps> viewProps =
        std::dynamic_pointer_cast<const facebook::react::ViewProps>(shadowView.props);

    if (viewProps == nullptr || !facebook::react::isColorMeaningful(viewProps->backgroundColor)) {
        return std::nullopt;
    }

    return viewProps->backgroundColor;
}

uint32_t toArgb(facebook::react::SharedColor color) {
    const uint32_t alpha = facebook::react::alphaFromColor(color);
    const uint32_t red = facebook::react::redFromColor(color);
    const uint32_t green = facebook::react::greenFromColor(color);
    const uint32_t blue = facebook::react::blueFromColor(color);

    return (alpha << 24U) | (red << 16U) | (green << 8U) | blue;
}

std::string readComponentName(const facebook::react::ShadowView& shadowView) {
    if (shadowView.componentName == nullptr) {
        return {};
    }

    return shadowView.componentName;
}

} // namespace

void RetainedScene::createSurfaceRoot(facebook::react::SurfaceId surfaceId, facebook::react::Size size) {
    SceneNode& node = nodes_[surfaceId];

    node.tag = surfaceId;
    node.componentName = facebook::react::RootComponentName;
    node.layoutMetrics.frame.size = size;
}

void RetainedScene::createNode(const facebook::react::ShadowView& shadowView) {
    writeNode(shadowView);
}

void RetainedScene::deleteNode(facebook::react::Tag tag) {
    nodes_.erase(tag);
}

void RetainedScene::insertChild(facebook::react::Tag parentTag, const facebook::react::ShadowView& childShadowView,
                                int index) {
    SceneNode& child = writeNode(childShadowView);
    child.parentTag = parentTag;

    const auto parent = nodes_.find(parentTag);

    if (parent == nodes_.end()) {
        return;
    }

    std::vector<facebook::react::Tag>& childTags = parent->second.childTags;
    const size_t position = std::min(static_cast<size_t>(std::max(index, 0)), childTags.size());

    childTags.insert(childTags.begin() + static_cast<std::ptrdiff_t>(position), childShadowView.tag);
}

void RetainedScene::removeChild(facebook::react::Tag parentTag, const facebook::react::ShadowView& childShadowView) {
    const auto child = nodes_.find(childShadowView.tag);
    facebook::react::Tag formerParentTag = parentTag;

    if (child != nodes_.end()) {
        formerParentTag = child->second.parentTag;
        child->second.parentTag = 0;
    }

    eraseChildTag(nodes_, parentTag, childShadowView.tag);

    if (formerParentTag != parentTag) {
        eraseChildTag(nodes_, formerParentTag, childShadowView.tag);
    }
}

void RetainedScene::updateNode(const facebook::react::ShadowView& shadowView) {
    writeNode(shadowView);
}

SceneSnapshot RetainedScene::snapshot() const {
    SceneSnapshot rectangles;

    for (facebook::react::Tag tag : sortedRootTags()) {
        appendRectangles(rectangles, tag, {});
    }

    return rectangles;
}

std::string RetainedScene::dump() const {
    std::string output;

    for (facebook::react::Tag tag : sortedRootTags()) {
        appendNode(output, tag, 0);
    }

    return output;
}

SceneNode& RetainedScene::writeNode(const facebook::react::ShadowView& shadowView) {
    SceneNode& node = nodes_[shadowView.tag];

    node.tag = shadowView.tag;
    node.componentName = readComponentName(shadowView);
    node.layoutMetrics = shadowView.layoutMetrics;
    node.backgroundColor = readBackgroundColor(shadowView);

    return node;
}

std::vector<facebook::react::Tag> RetainedScene::sortedRootTags() const {
    std::vector<facebook::react::Tag> rootTags;

    for (const auto& [tag, node] : nodes_) {
        if (node.parentTag == 0) {
            rootTags.push_back(tag);
        }
    }

    std::sort(rootTags.begin(), rootTags.end());

    return rootTags;
}

void RetainedScene::appendRectangles(SceneSnapshot& rectangles, facebook::react::Tag tag,
                                     facebook::react::Point parentOrigin) const {
    const auto entry = nodes_.find(tag);

    if (entry == nodes_.end()) {
        return;
    }

    const SceneNode& node = entry->second;
    const facebook::react::Point origin = parentOrigin + node.layoutMetrics.frame.origin;

    if (node.backgroundColor.has_value()) {
        rectangles.push_back(
            SceneRectangle{.frame = facebook::react::Rect{.origin = origin, .size = node.layoutMetrics.frame.size},
                           .colorArgb = toArgb(node.backgroundColor.value())});
    }

    for (facebook::react::Tag childTag : node.childTags) {
        appendRectangles(rectangles, childTag, origin);
    }
}

void RetainedScene::appendNode(std::string& output, facebook::react::Tag tag, size_t depth) const {
    const auto entry = nodes_.find(tag);

    if (entry == nodes_.end()) {
        return;
    }

    const SceneNode& node = entry->second;

    output.append(depth * kIndentWidth, ' ');
    output += node.componentName;
    output += " #";
    output += std::to_string(node.tag);
    output += " frame=";
    output += formatFrame(node.layoutMetrics.frame);

    if (node.backgroundColor.has_value()) {
        output += " backgroundColor=";
        output += node.backgroundColor.value().toString();
    }

    output += '\n';

    for (facebook::react::Tag childTag : node.childTags) {
        appendNode(output, childTag, depth + 1);
    }
}

} // namespace react_native_linux
