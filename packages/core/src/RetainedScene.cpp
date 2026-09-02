#include "RetainedScene.h"

#include "TextInputComponent.h"

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/TextAttributes.h>
#include <react/renderer/components/textinput/TextInputState.h>
#include <react/renderer/components/image/ImageProps.h>
#include <react/renderer/components/image/ImageState.h>
#include <react/renderer/components/root/RootShadowNode.h>
#include <react/renderer/components/scrollview/ScrollViewState.h>
#include <react/renderer/components/text/ParagraphState.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/imagemanager/primitives.h>
#include <react/renderer/core/ConcreteState.h>
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
#include <tuple>
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

/**
 * What one node contributes to a walk: the primitive it would paint, and the state its children inherit.
 */
struct SceneVisit {
    ScenePrimitive primitive;
    ScenePaintState childState;
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
// The caret and the selection follow the same fixed accent the focus ring does, for the same reason: there is no
// theme service to ask, and reading the compositor's accent is an org.freedesktop.portal.Settings round trip.
// `cursorColor` and `selectionColor` override both, which is react-native-macos#1096.
constexpr uint32_t kDefaultCaretColorArgb = 0xFF599EFFU;
constexpr uint32_t kDefaultSelectionColorArgb = 0x59599EFFU;

std::string formatFrame(const facebook::react::Rect& frame) {
    std::array<char, kFrameBufferSize> buffer{};

    std::snprintf(buffer.data(), buffer.size(), "(%.2f, %.2f, %.2f, %.2f)", static_cast<double>(frame.origin.x),
                  static_cast<double>(frame.origin.y), static_cast<double>(frame.size.width),
                  static_cast<double>(frame.size.height));

    return buffer.data();
}

std::string formatPoint(facebook::react::Point point) {
    std::array<char, kFrameBufferSize> buffer{};

    std::snprintf(buffer.data(), buffer.size(), "(%.2f, %.2f)", static_cast<double>(point.x),
                  static_cast<double>(point.y));

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

/**
 * The same alpha multiplication `toArgb` performs, kept in React Native's own colour type because text colours
 * travel to the painter inside the `AttributedString` rather than as packed pixels.
 */
facebook::react::SharedColor scaleColorAlpha(facebook::react::SharedColor color, float opacity) {
    if (!facebook::react::isColorMeaningful(color)) {
        return color;
    }

    const float scaledAlpha = static_cast<float>(facebook::react::alphaFromColor(color)) * opacity;

    return facebook::react::colorFromRGBA(facebook::react::redFromColor(color), facebook::react::greenFromColor(color),
                                          facebook::react::blueFromColor(color),
                                          static_cast<uint8_t>(std::lround(scaledAlpha)));
}

/**
 * Resolves a node's text for one snapshot: the absolute content box the paragraph occupies, and the inherited
 * opacity folded into every colour a fragment can paint with.
 *
 * Copying the string first is what makes the colour rewrite safe: `Sealable` clears the seal on copy, so the
 * retained node's own content is never mutated.
 */
SceneTextContent resolveText(const SceneTextContent& text, const facebook::react::EdgeInsets& contentInsets,
                             const facebook::react::Rect& frame, float opacity) {
    SceneTextContent resolved = text;

    resolved.frame = facebook::react::Rect{
        .origin = facebook::react::Point{.x = frame.origin.x + contentInsets.left,
                                         .y = frame.origin.y + contentInsets.top},
        .size = facebook::react::Size{.width = frame.size.width - contentInsets.left - contentInsets.right,
                                      .height = frame.size.height - contentInsets.top - contentInsets.bottom}};

    for (facebook::react::AttributedString::Fragment& fragment : resolved.attributedString.getFragments()) {
        facebook::react::TextAttributes& attributes = fragment.textAttributes;

        attributes.foregroundColor = scaleColorAlpha(attributes.foregroundColor, opacity);
        attributes.backgroundColor = scaleColorAlpha(attributes.backgroundColor, opacity);
        attributes.textDecorationColor = scaleColorAlpha(attributes.textDecorationColor, opacity);
    }

    return resolved;
}

/**
 * The same alpha multiplication `toArgb` performs, applied to an already packed colour. An image's tint is read
 * off the props once and folded down the tree per snapshot, so the retained node keeps the authored colour.
 */
uint32_t scaleArgbAlpha(uint32_t colorArgb, float opacity) {
    const uint32_t alpha = colorArgb >> kAlphaShift;

    if (alpha == 0U) {
        return 0;
    }

    const uint32_t scaledAlpha = static_cast<uint32_t>(std::lround(static_cast<float>(alpha) * opacity));

    return (scaledAlpha << kAlphaShift) | (colorArgb & ~(0xFFU << kAlphaShift));
}

SceneImageContent resolveImage(const SceneImageContent& image, float opacity) {
    return SceneImageContent{.uri = image.uri,
                             .resizeMode = image.resizeMode,
                             .tintColorArgb = scaleArgbAlpha(image.tintColorArgb, opacity),
                             .opacity = opacity};
}

SceneEditorContent resolveEditor(const SceneEditorContent& editor, float opacity) {
    return SceneEditorContent{.state = editor.state,
                              .caretColorArgb = scaleArgbAlpha(editor.caretColorArgb, opacity),
                              .selectionColorArgb = scaleArgbAlpha(editor.selectionColorArgb, opacity),
                              .isPlaceholder = editor.isPlaceholder,
                              .isMultiline = editor.isMultiline};
}

SceneImageResizeMode toSceneImageResizeMode(facebook::react::ImageResizeMode resizeMode) {
    if (resizeMode == facebook::react::ImageResizeMode::Cover) {
        return SceneImageResizeMode::Cover;
    }

    if (resizeMode == facebook::react::ImageResizeMode::Contain) {
        return SceneImageResizeMode::Contain;
    }

    if (resizeMode == facebook::react::ImageResizeMode::Stretch) {
        return SceneImageResizeMode::Stretch;
    }

    if (resizeMode == facebook::react::ImageResizeMode::Repeat) {
        return SceneImageResizeMode::Repeat;
    }

    // `center` and `none` both draw the image at its natural size; upstream distinguishes them only for Android's
    // legacy scale types, which this renderer has no equivalent of.
    return SceneImageResizeMode::Center;
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
    return primitive.focusRing || primitive.text.has_value() || primitive.image.has_value() ||
           !primitive.backgroundImage.empty() || (primitive.backgroundColorArgb >> kAlphaShift) != 0U ||
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
        node.backgroundImage.clear();
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

    // Copied rather than resolved: the gradient stops cannot be turned into a ramp without the CSS gradient line,
    // which needs Skia geometry. See *Gradients* in docs/cpp-toolchain.md.
    node.backgroundImage = viewProps->backgroundImage;

    // resolveBorderMetrics is what iOS and Android call too: it cascades the per-edge and per-corner props,
    // converts percentage radii to points, and applies the CSS corner-overlap clamp.
    node.borderMetrics = viewProps->resolveBorderMetrics(shadowView.layoutMetrics);
    node.transform = toSceneMatrix(viewProps->resolveTransform(shadowView.layoutMetrics));
    node.opacity = std::clamp(static_cast<float>(viewProps->opacity), 0.0F, 1.0F);
    node.clipsChildren = viewProps->getClipsContentToBounds();
}

/**
 * The paragraph inputs a `<Paragraph>` mounts with. React never mounts the nested `<Text>` and `<RawText>` shadow
 * nodes — they are not view-forming — so `ParagraphState`, which `ParagraphShadowNode::layout` fills in with the
 * flattened `AttributedString`, is the only place the text exists at the mounting layer.
 *
 * The state pointer is what identifies a paragraph, not the component name: `<Paragraph>` and
 * `<SelectableParagraph>` both carry this state and neither of the other components carries any.
 */
void readTextContent(SceneNode& node, const facebook::react::ShadowView& shadowView) {
    node.text = std::nullopt;

    const std::shared_ptr<const facebook::react::ConcreteState<facebook::react::ParagraphState>> paragraphState =
        std::dynamic_pointer_cast<const facebook::react::ConcreteState<facebook::react::ParagraphState>>(
            shadowView.state);

    if (paragraphState == nullptr || paragraphState->getData().attributedString.isEmpty()) {
        return;
    }

    node.text = SceneTextContent{.attributedString = paragraphState->getData().attributedString,
                                 .paragraphAttributes = paragraphState->getData().paragraphAttributes};
}

/**
 * The value a `<TextInput>` mounts with, and the colours its caret and selection are drawn in.
 *
 * The string is read off `TextInputState`, not off `props.text`, for the reason the scroll offset is read off
 * `ScrollViewState`: the state is what the platform writes back into when the user types, so it is the one
 * description of the field's contents that React, Yoga's measurement and the picture all share. A `props.text`
 * that has not been reconciled yet is by definition the value React thinks is current, which is not the same
 * thing — and drawing it is react-native-macos#2127, the caret jumping back mid-word.
 *
 * An empty value draws the placeholder instead, in `placeholderTextColor` when there is one. The placeholder is
 * resolved here rather than pushed into the state because upstream's `updateStateIfNeeded` treats a non-empty
 * state string as the field's content: a placeholder in there would become the value on the next commit.
 */
void readEditorContent(SceneNode& node, const facebook::react::ShadowView& shadowView) {
    node.editor = std::nullopt;

    const std::shared_ptr<const TextInputProps> textInputProps =
        std::dynamic_pointer_cast<const TextInputProps>(shadowView.props);
    const std::shared_ptr<const facebook::react::ConcreteState<facebook::react::TextInputState>> textInputState =
        std::dynamic_pointer_cast<const facebook::react::ConcreteState<facebook::react::TextInputState>>(
            shadowView.state);

    if (textInputProps == nullptr || textInputState == nullptr) {
        return;
    }

    const facebook::react::AttributedString& value =
        textInputState->getData().attributedStringBox.getValue();
    const bool isPlaceholder = value.isEmpty();
    facebook::react::AttributedString displayed = value;

    if (isPlaceholder) {
        facebook::react::TextAttributes placeholderAttributes = textInputProps->getEffectiveTextAttributes(1.0F);

        if (facebook::react::isColorMeaningful(textInputProps->placeholderTextColor)) {
            placeholderAttributes.foregroundColor = textInputProps->placeholderTextColor;
        }

        displayed.setBaseTextAttributes(placeholderAttributes);

        if (!textInputProps->placeholder.empty()) {
            displayed.appendFragment(facebook::react::AttributedString::Fragment{
                .string = textInputProps->placeholder, .textAttributes = placeholderAttributes,
                .parentShadowView = {}});
        }
    }

    node.text = SceneTextContent{.attributedString = displayed,
                                 .paragraphAttributes = textInputProps->paragraphAttributes};

    const uint32_t authoredCaretColorArgb = toArgb(textInputProps->cursorColor, 1.0F);
    const uint32_t authoredSelectionColorArgb = toArgb(textInputProps->selectionColor, 1.0F);
    const uint32_t caretColorArgb =
        authoredCaretColorArgb != 0 ? authoredCaretColorArgb : kDefaultCaretColorArgb;
    const uint32_t selectionColorArgb =
        authoredSelectionColorArgb != 0 ? authoredSelectionColorArgb : kDefaultSelectionColorArgb;

    node.editor = SceneEditorContent{.caretColorArgb = textInputProps->caretHidden ? 0 : caretColorArgb,
                                     .selectionColorArgb = selectionColorArgb,
                                     .isPlaceholder = isPlaceholder,
                                     .isMultiline = textInputProps->multiline};
}

/**
 * The source an `<Image>` mounts with, read off `ImageState` rather than off `ImageProps.sources`.
 *
 * `ImageShadowNode` is what chooses between several sources and what stamps the laid-out size and scale onto the
 * one it chose, and the source it chose is the source `ImageManager::requestImage` was given and therefore the
 * source the decoder is filling the cache with. Reading the props instead would be a second answer to the same
 * question. The fit and the tint are only on the props, because neither reaches the state.
 *
 * A node whose state still holds the `Invalid` source `ImageShadowNode::initialStateData` seeds — which is every
 * `<Image>` before its first layout — has an empty uri and paints nothing.
 */
void readImageContent(SceneNode& node, const facebook::react::ShadowView& shadowView) {
    node.image = std::nullopt;

    const std::shared_ptr<const facebook::react::ImageProps> imageProps =
        std::dynamic_pointer_cast<const facebook::react::ImageProps>(shadowView.props);
    const std::shared_ptr<const facebook::react::ConcreteState<facebook::react::ImageState>> imageState =
        std::dynamic_pointer_cast<const facebook::react::ConcreteState<facebook::react::ImageState>>(
            shadowView.state);

    if (imageProps == nullptr || imageState == nullptr) {
        return;
    }

    const facebook::react::ImageSource imageSource = imageState->getData().getImageSource();

    if (imageSource.uri.empty()) {
        return;
    }

    node.image = SceneImageContent{.uri = imageSource.uri,
                                   .resizeMode = toSceneImageResizeMode(imageProps->resizeMode),
                                   .tintColorArgb = toArgb(imageProps->tintColor, 1.0F)};
}

/**
 * The scroll position a `<ScrollView>` mounts with, read off `ScrollViewState` for the same reason the image
 * source is read off `ImageState`: the state is what the platform writes back into when it scrolls, so it is the
 * one description of the offset that React, the hit test and the picture all share.
 *
 * A ScrollView clips unconditionally, which is `UIScrollView.clipsToBounds` and does not depend on `overflow`
 * reaching the props. Setting it here rather than in `readPaintProps` is what makes it survive that function's
 * reset, and the two are called in that order.
 */
void readScrollContent(SceneNode& node, const facebook::react::ShadowView& shadowView) {
    node.scrollContentOffset = std::nullopt;

    const std::shared_ptr<const facebook::react::ConcreteState<facebook::react::ScrollViewState>> scrollState =
        std::dynamic_pointer_cast<const facebook::react::ConcreteState<facebook::react::ScrollViewState>>(
            shadowView.state);

    if (scrollState == nullptr) {
        return;
    }

    node.scrollContentOffset = scrollState->getData().contentOffset;
    node.clipsChildren = true;
}

std::string readComponentName(const facebook::react::ShadowView& shadowView) {
    if (shadowView.componentName == nullptr) {
        return {};
    }

    return shadowView.componentName;
}

/**
 * The absolute origin a node's children are composed from. It is the node's own origin for everything but a
 * `<ScrollView>`, which shifts its children by `-contentOffset` — the whole of scrolling, in one subtraction.
 */
facebook::react::Point contentOrigin(const SceneNode& node, facebook::react::Point origin) {
    if (!node.scrollContentOffset.has_value()) {
        return origin;
    }

    return origin - node.scrollContentOffset.value();
}

SceneVisit visitNode(const SceneNode& node, const ScenePaintState& state) {
    const facebook::react::Rect frame{.origin = state.origin + node.layoutMetrics.frame.origin,
                                      .size = node.layoutMetrics.frame.size};
    const SceneMatrix matrix = composeMatrices(state.matrix, matrixAboutCenter(node.transform, frame.getCenter()));
    const float opacity = state.opacity * node.opacity;
    SceneVisit visit{.primitive = ScenePrimitive{.frame = frame,
                                                 .matrix = matrix,
                                                 .clips = state.clips,
                                                 .borderRadii = node.borderMetrics.borderRadii,
                                                 .borderWidths = node.borderMetrics.borderWidths,
                                                 .borderColorsArgb = toArgbEdges(node.borderMetrics.borderColors,
                                                                                 opacity),
                                                 .backgroundColorArgb =
                                                     node.backgroundColor.has_value()
                                                         ? toArgb(node.backgroundColor.value(), opacity)
                                                         : 0,
                                                 .backgroundImage = node.backgroundImage,
                                                 .backgroundImageOpacity = opacity,
                                                 .text = node.text.has_value()
                                                             ? std::optional<SceneTextContent>{resolveText(
                                                                   node.text.value(),
                                                                   node.layoutMetrics.contentInsets, frame, opacity)}
                                                             : std::nullopt,
                                                 .image = node.image.has_value()
                                                              ? std::optional<SceneImageContent>{resolveImage(
                                                                    node.image.value(), opacity)}
                                                              : std::nullopt,
                                                 .editor = node.editor.has_value()
                                                               ? std::optional<SceneEditorContent>{resolveEditor(
                                                                     node.editor.value(), opacity)}
                                                               : std::nullopt},
                     .childState = ScenePaintState{.origin = contentOrigin(node, frame.origin),
                                                   .matrix = matrix,
                                                   .opacity = opacity,
                                                   .clips = state.clips}};

    if (node.clipsChildren) {
        visit.childState.clips.push_back(
            SceneClip{.frame = frame, .borderRadii = node.borderMetrics.borderRadii, .matrix = matrix});
    }

    return visit;
}

/**
 * The state a node's own walk starts from: every ancestor between it and its root, composed top down. A node whose
 * parent chain is broken — an insert under a tag that was never created — is measured as if it were a root, which
 * damages a region that nothing paints. That is a superset, and a superset is safe.
 */
ScenePaintState paintStateOfAncestors(const SceneNodes& nodes, facebook::react::Tag tag) {
    const auto entry = nodes.find(tag);
    facebook::react::Tag current = entry == nodes.end() ? 0 : entry->second.parentTag;
    std::vector<const SceneNode*> ancestors;

    while (current != 0) {
        const auto ancestor = nodes.find(current);

        if (ancestor == nodes.end()) {
            break;
        }

        ancestors.push_back(&ancestor->second);
        current = ancestor->second.parentTag;
    }

    std::reverse(ancestors.begin(), ancestors.end());

    ScenePaintState state;

    for (const SceneNode* ancestor : ancestors) {
        state = visitNode(*ancestor, state).childState;
    }

    return state;
}

facebook::react::Point mapPoint(const SceneMatrix& matrix, facebook::react::Point point) {
    return facebook::react::Point{.x = (matrix.scaleX * point.x) + (matrix.skewX * point.y) + matrix.translateX,
                                  .y = (matrix.skewY * point.x) + (matrix.scaleY * point.y) + matrix.translateY};
}

/**
 * The axis-aligned box a transformed rectangle occupies: its four corners mapped through the matrix, bounded.
 */
facebook::react::Rect mappedBounds(const facebook::react::Rect& frame, const SceneMatrix& matrix) {
    const facebook::react::Float right = frame.origin.x + frame.size.width;
    const facebook::react::Float bottom = frame.origin.y + frame.size.height;

    return facebook::react::Rect::boundingRect(
        mapPoint(matrix, frame.origin), mapPoint(matrix, facebook::react::Point{.x = right, .y = frame.origin.y}),
        mapPoint(matrix, facebook::react::Point{.x = right, .y = bottom}),
        mapPoint(matrix, facebook::react::Point{.x = frame.origin.x, .y = bottom}));
}

/**
 * What one primitive can dirty: its transformed frame, cut by every clip it inherited. Borders are inside the
 * frame, so the frame is the whole extent.
 */
facebook::react::Rect primitiveDamageBounds(const ScenePrimitive& primitive) {
    facebook::react::Rect bounds = mappedBounds(primitive.frame, primitive.matrix);

    for (const SceneClip& clip : primitive.clips) {
        bounds = facebook::react::Rect::intersect(bounds, mappedBounds(clip.frame, clip.matrix));
    }

    return bounds;
}

bool hasArea(const facebook::react::Rect& rect) {
    return rect.size.width * rect.size.height > 0;
}

constexpr size_t kMaxDamageRects = 8;

void addDamageRect(SceneDamage& damage, const facebook::react::Rect& rect) {
    damage.push_back(rect);

    if (damage.size() <= kMaxDamageRects) {
        return;
    }

    facebook::react::Rect bounds = damage.front();

    for (const facebook::react::Rect& merged : damage) {
        bounds.unionInPlace(merged);
    }

    damage.assign(1, bounds);
}

} // namespace

void mergeDamage(SceneDamage& damage, const SceneDamage& additions) {
    for (const facebook::react::Rect& rect : additions) {
        addDamageRect(damage, rect);
    }
}

void RetainedScene::createSurfaceRoot(facebook::react::SurfaceId surfaceId, facebook::react::Size size) {
    SceneNode& node = nodes_[surfaceId];

    node.tag = surfaceId;
    node.componentName = facebook::react::RootComponentName;
    node.layoutMetrics.frame.size = size;

    // The root paints nothing, so its subtree extent would be empty on a fresh surface. A new or resized surface
    // is a full repaint regardless: the pixels behind it belong to whatever was there before.
    addDamageRect(damage_, node.layoutMetrics.frame);
}

void RetainedScene::createNode(const facebook::react::ShadowView& shadowView) {
    writeNode(shadowView);
    damageSubtree(shadowView.tag);
}

void RetainedScene::deleteNode(facebook::react::Tag tag) {
    damageSubtree(tag);
    nodes_.erase(tag);
    editorStates_.erase(tag);
}

void RetainedScene::insertChild(facebook::react::Tag parentTag, const facebook::react::ShadowView& childShadowView,
                                int index) {
    damageSubtree(childShadowView.tag);

    SceneNode& child = writeNode(childShadowView);
    child.parentTag = parentTag;

    const auto parent = nodes_.find(parentTag);

    if (parent != nodes_.end()) {
        std::vector<facebook::react::Tag>& childTags = parent->second.childTags;
        const size_t position = std::min(static_cast<size_t>(std::max(index, 0)), childTags.size());

        childTags.insert(childTags.begin() + static_cast<std::ptrdiff_t>(position), childShadowView.tag);
    }

    damageSubtree(childShadowView.tag);
}

void RetainedScene::removeChild(facebook::react::Tag parentTag, const facebook::react::ShadowView& childShadowView) {
    damageSubtree(childShadowView.tag);

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

    damageSubtree(childShadowView.tag);
}

void RetainedScene::updateNode(const facebook::react::ShadowView& shadowView) {
    damageSubtree(shadowView.tag);
    writeNode(shadowView);
    damageSubtree(shadowView.tag);
}

void RetainedScene::damageImageSource(const std::string& uri) {
    std::vector<facebook::react::Tag> drawingTags;

    for (const auto& [tag, node] : nodes_) {
        if (node.image.has_value() && node.image.value().uri == uri) {
            drawingTags.push_back(tag);
        }
    }

    for (facebook::react::Tag tag : drawingTags) {
        damageSubtree(tag);
    }
}

void RetainedScene::setFocus(facebook::react::Tag tag, bool isFocusVisible) {
    if (tag == focusedTag_ && isFocusVisible == isFocusVisible_) {
        return;
    }

    // Only a ring that appears or disappears changes a pixel. The focused tag is tracked either way, because it
    // is what the next Tab moves from, so a click that focuses without drawing anything damages nothing.
    if (isFocusVisible_) {
        damageSubtree(focusedTag_);
    }

    focusedTag_ = tag;
    isFocusVisible_ = isFocusVisible;

    if (isFocusVisible) {
        damageSubtree(tag);
    }
}

void RetainedScene::setEditorState(facebook::react::Tag tag, const SceneEditorState& editorState) {
    SceneEditorState& current = editorStates_[tag];

    if (std::tie(current.caretUtf16, current.selectionBeginUtf16, current.selectionEndUtf16,
                 current.compositionBeginUtf16, current.compositionEndUtf16, current.scrollOffsetX,
                 current.isCaretVisible) ==
        std::tie(editorState.caretUtf16, editorState.selectionBeginUtf16, editorState.selectionEndUtf16,
                 editorState.compositionBeginUtf16, editorState.compositionEndUtf16, editorState.scrollOffsetX,
                 editorState.isCaretVisible)) {
        return;
    }

    current = editorState;

    // One rectangle, not two: everything a caret, a selection or a composing underline draws is inside the
    // field's own frame, which is also the extent this damages. A blink is therefore one field's repaint.
    damageSubtree(tag);
}

SceneSnapshot RetainedScene::snapshot() const {
    SceneSnapshot primitives;
    const ScenePaintState rootState{};

    for (facebook::react::Tag tag : sortedRootTags()) {
        appendPrimitives(primitives, tag, rootState);
    }

    return primitives;
}

SceneDamage RetainedScene::takeDamage() {
    return std::exchange(damage_, SceneDamage{});
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
    readTextContent(node, shadowView);
    readEditorContent(node, shadowView);
    readImageContent(node, shadowView);
    readScrollContent(node, shadowView);

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
    SceneVisit visit = visitNode(node, state);

    visit.primitive.focusRing = isFocusVisible_ && tag == focusedTag_;

    if (visit.primitive.editor.has_value()) {
        const auto editorState = editorStates_.find(tag);

        if (editorState != editorStates_.end()) {
            visit.primitive.editor.value().state = editorState->second;
        }
    }

    if (isPrimitiveVisible(visit.primitive)) {
        primitives.push_back(std::move(visit.primitive));
    }

    for (facebook::react::Tag childTag : node.childTags) {
        appendPrimitives(primitives, childTag, visit.childState);
    }
}

std::optional<facebook::react::Rect> RetainedScene::subtreeExtent(facebook::react::Tag tag) const {
    SceneSnapshot primitives;

    appendPrimitives(primitives, tag, paintStateOfAncestors(nodes_, tag));

    std::optional<facebook::react::Rect> extent;

    for (const ScenePrimitive& primitive : primitives) {
        const facebook::react::Rect bounds = primitiveDamageBounds(primitive);

        if (!hasArea(bounds)) {
            continue;
        }

        if (extent.has_value()) {
            extent.value().unionInPlace(bounds);
        } else {
            extent = bounds;
        }
    }

    return extent;
}

void RetainedScene::damageSubtree(facebook::react::Tag tag) {
    const std::optional<facebook::react::Rect> extent = subtreeExtent(tag);

    if (extent.has_value()) {
        addDamageRect(damage_, extent.value());
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

    if (node.text.has_value()) {
        output += " text=\"";
        output += node.text.value().attributedString.getString();
        output += '"';
    }

    if (node.image.has_value()) {
        output += " image=\"";
        output += node.image.value().uri;
        output += '"';
    }

    if (node.scrollContentOffset.has_value()) {
        output += " contentOffset=";
        output += formatPoint(node.scrollContentOffset.value());
    }

    output += '\n';

    for (facebook::react::Tag childTag : node.childTags) {
        appendNode(output, childTag, depth + 1);
    }
}

} // namespace react_native_linux
