#include "RetainedScene.h"

#include <react/renderer/components/root/RootShadowNode.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/graphics/Transform.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {

/**
 * What a parent contributes to its children during the snapshot walk: the absolute origin their frames are
 * relative to, the transform they inherit, the opacity they inherit, and the clips they are cut by.
 */
struct ScenePaintState {
    facebook::react::Point origin{};
    SceneMatrix matrix{};
    float opacity{1.0F};
    std::vector<SceneClip> clips{};
};

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
constexpr uint32_t kAlphaShift = 24U;
constexpr uint32_t kRedShift = 16U;
constexpr uint32_t kGreenShift = 8U;

std::string formatFrame(const facebook::react::Rect& frame) {
    std::array<char, kFrameBufferSize> buffer{};

    std::snprintf(buffer.data(), buffer.size(), "(%.2f, %.2f, %.2f, %.2f)", static_cast<double>(frame.origin.x),
                  static_cast<double>(frame.origin.y), static_cast<double>(frame.size.width),
                  static_cast<double>(frame.size.height));

    return buffer.data();
}

/**
 * Composes two affine transforms so that `inner` is applied first and `outer` second.
 */
SceneMatrix composeMatrices(const SceneMatrix& outer, const SceneMatrix& inner) {
    return SceneMatrix{.scaleX = (outer.scaleX * inner.scaleX) + (outer.skewX * inner.skewY),
                       .skewX = (outer.scaleX * inner.skewX) + (outer.skewX * inner.scaleY),
                       .translateX = (outer.scaleX * inner.translateX) + (outer.skewX * inner.translateY) +
                                     outer.translateX,
                       .skewY = (outer.skewY * inner.scaleX) + (outer.scaleY * inner.skewY),
                       .scaleY = (outer.skewY * inner.skewX) + (outer.scaleY * inner.scaleY),
                       .translateY = (outer.skewY * inner.translateX) + (outer.scaleY * inner.translateY) +
                                     outer.translateY};
}

SceneMatrix translationMatrix(float translateX, float translateY) {
    return SceneMatrix{.translateX = translateX, .translateY = translateY};
}

/**
 * React Native applies a view's transform about the centre of its frame, which is what `Transform::applyWithCenter`
 * encodes upstream. A custom `transformOrigin` is already folded into the matrix by `resolveTransform`.
 */
SceneMatrix matrixAboutCenter(const SceneMatrix& transform, facebook::react::Point center) {
    return composeMatrices(translationMatrix(center.x, center.y),
                           composeMatrices(transform, translationMatrix(-center.x, -center.y)));
}

/**
 * Reduces Fabric's resolved 4x4 transform to the 2D affine part. React Native stores the matrix so that a row
 * vector is multiplied from the left, so column 0 and column 1 of rows 0, 1 and 3 carry the whole affine.
 */
SceneMatrix toSceneMatrix(const facebook::react::Transform& transform) {
    return SceneMatrix{.scaleX = static_cast<float>(transform.matrix[0]),
                       .skewX = static_cast<float>(transform.matrix[4]),
                       .translateX = static_cast<float>(transform.matrix[12]),
                       .skewY = static_cast<float>(transform.matrix[1]),
                       .scaleY = static_cast<float>(transform.matrix[5]),
                       .translateY = static_cast<float>(transform.matrix[13])};
}

uint32_t toArgb(facebook::react::SharedColor color, float opacity) {
    if (!facebook::react::isColorMeaningful(color)) {
        return 0;
    }

    const float scaledAlpha = static_cast<float>(facebook::react::alphaFromColor(color)) * opacity;
    const uint32_t alpha = static_cast<uint32_t>(std::lround(scaledAlpha));
    const uint32_t red = facebook::react::redFromColor(color);
    const uint32_t green = facebook::react::greenFromColor(color);
    const uint32_t blue = facebook::react::blueFromColor(color);

    return (alpha << kAlphaShift) | (red << kRedShift) | (green << kGreenShift) | blue;
}

facebook::react::RectangleEdges<uint32_t> toArgbEdges(const facebook::react::BorderColors& colors, float opacity) {
    return facebook::react::RectangleEdges<uint32_t>{.left = toArgb(colors.left, opacity),
                                                     .top = toArgb(colors.top, opacity),
                                                     .right = toArgb(colors.right, opacity),
                                                     .bottom = toArgb(colors.bottom, opacity)};
}

bool isEdgeVisible(facebook::react::Float width, uint32_t colorArgb) {
    return width > 0 && (colorArgb >> kAlphaShift) != 0U;
}

bool isPrimitiveVisible(const ScenePrimitive& primitive) {
    return (primitive.backgroundColorArgb >> kAlphaShift) != 0U ||
           isEdgeVisible(primitive.borderWidths.left, primitive.borderColorsArgb.left) ||
           isEdgeVisible(primitive.borderWidths.top, primitive.borderColorsArgb.top) ||
           isEdgeVisible(primitive.borderWidths.right, primitive.borderColorsArgb.right) ||
           isEdgeVisible(primitive.borderWidths.bottom, primitive.borderColorsArgb.bottom);
}

void readPaintProps(SceneNode& node, const facebook::react::ShadowView& shadowView) {
    const std::shared_ptr<const facebook::react::ViewProps> viewProps =
        std::dynamic_pointer_cast<const facebook::react::ViewProps>(shadowView.props);

    if (viewProps == nullptr) {
        node.backgroundColor = std::nullopt;
        node.borderMetrics = {};
        node.transform = {};
        node.opacity = 1.0F;
        node.clipsChildren = false;

        return;
    }

    if (facebook::react::isColorMeaningful(viewProps->backgroundColor)) {
        node.backgroundColor = viewProps->backgroundColor;
    } else {
        node.backgroundColor = std::nullopt;
    }

    // resolveBorderMetrics is what iOS and Android call too: it cascades the per-edge and per-corner props,
    // converts percentage radii to points, and applies the CSS corner-overlap clamp.
    node.borderMetrics = viewProps->resolveBorderMetrics(shadowView.layoutMetrics);
    node.transform = toSceneMatrix(viewProps->resolveTransform(shadowView.layoutMetrics));
    node.opacity = std::clamp(static_cast<float>(viewProps->opacity), 0.0F, 1.0F);
    node.clipsChildren = viewProps->getClipsContentToBounds();
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
    SceneSnapshot primitives;
    const ScenePaintState rootState{};

    for (facebook::react::Tag tag : sortedRootTags()) {
        appendPrimitives(primitives, tag, rootState);
    }

    return primitives;
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
    readPaintProps(node, shadowView);

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

void RetainedScene::appendPrimitives(SceneSnapshot& primitives, facebook::react::Tag tag,
                                     const ScenePaintState& state) const {
    const auto entry = nodes_.find(tag);

    if (entry == nodes_.end()) {
        return;
    }

    const SceneNode& node = entry->second;
    const facebook::react::Rect frame{.origin = state.origin + node.layoutMetrics.frame.origin,
                                      .size = node.layoutMetrics.frame.size};
    const SceneMatrix matrix = composeMatrices(state.matrix, matrixAboutCenter(node.transform, frame.getCenter()));
    const float opacity = state.opacity * node.opacity;
    ScenePrimitive primitive{.frame = frame,
                             .matrix = matrix,
                             .clips = state.clips,
                             .borderRadii = node.borderMetrics.borderRadii,
                             .borderWidths = node.borderMetrics.borderWidths,
                             .borderColorsArgb = toArgbEdges(node.borderMetrics.borderColors, opacity),
                             .backgroundColorArgb =
                                 node.backgroundColor.has_value() ? toArgb(node.backgroundColor.value(), opacity) : 0};

    if (isPrimitiveVisible(primitive)) {
        primitives.push_back(std::move(primitive));
    }

    ScenePaintState childState{.origin = frame.origin, .matrix = matrix, .opacity = opacity, .clips = state.clips};

    if (node.clipsChildren) {
        childState.clips.push_back(
            SceneClip{.frame = frame, .borderRadii = node.borderMetrics.borderRadii, .matrix = matrix});
    }

    for (facebook::react::Tag childTag : node.childTags) {
        appendPrimitives(primitives, childTag, childState);
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
