#pragma once

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>
#include <react/renderer/components/view/primitives.h>
#include <react/renderer/core/LayoutMetrics.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/graphics/RectangleEdges.h>
#include <react/renderer/graphics/Size.h>
#include <react/renderer/mounting/ShadowView.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace react_native_linux {

/**
 * A 2D affine transform in surface coordinates: `x' = scaleX * x + skewX * y + translateX` and
 * `y' = skewY * x + scaleY * y + translateY`.
 *
 * The member order is Skia's `SkMatrix::MakeAll` order, so the painter builds one without arithmetic of its own.
 * React Native hands Fabric a 4x4 matrix; the reduction to these six values happens in the scene, where it is
 * covered, and drops the third row and column. See *View props fidelity* in docs/cpp-toolchain.md.
 */
struct SceneMatrix {
    float scaleX{1.0F};
    float skewX{0.0F};
    float translateX{0.0F};
    float skewY{0.0F};
    float scaleY{1.0F};
    float translateY{0.0F};
};

/**
 * One `overflow: hidden` ancestor: the rounded border box every descendant primitive is clipped to, together with
 * the transform that ancestor was drawn under. The matrix travels with the clip because an ancestor may carry a
 * transform its descendants do not.
 */
struct SceneClip {
    facebook::react::Rect frame;
    facebook::react::BorderRadii borderRadii;
    SceneMatrix matrix;
};

/**
 * Everything a `<Paragraph>` needs to be laid out and drawn: the fragment list React built out of the nested
 * `<Text>` and `<RawText>` children, and the paragraph-level attributes.
 *
 * These are the two values `ParagraphState` carries to the mounting layer, kept as the upstream types rather than
 * copied into a parallel model on purpose: `TextLayoutManager::measure` was given exactly these during layout, so
 * the paragraph the painter builds from them is the paragraph Yoga measured. A second description of the same
 * text is a second chance for the drawn line breaks to disagree with the measured ones.
 *
 * Fragment colours arrive here with the inherited opacity already multiplied into their alpha channel, exactly
 * like every other colour in a snapshot; nothing else about them is resolved, because resolving a font needs
 * Skia and the scene does not link it.
 */
struct SceneTextContent {
    facebook::react::AttributedString attributedString;
    facebook::react::ParagraphAttributes paragraphAttributes;

    /**
     * The absolute content box the paragraph is laid out and drawn in: the node's frame inset by its borders and
     * padding, which is the box `ParagraphShadowNode` measured against and therefore the box the same text has to
     * be re-laid-out in to break the same way.
     *
     * Only a snapshot fills this in, because only a snapshot knows the absolute frame; on a retained `SceneNode`
     * the whole struct is the unresolved half and this member is empty.
     */
    facebook::react::Rect frame;
};

/**
 * How a decoded image is fitted into the node's frame. These are React Native's own `resizeMode` values, minus
 * `none`, which maps onto `Center` because both draw the image at its natural size.
 */
enum class SceneImageResizeMode : uint8_t { Cover, Contain, Stretch, Center, Repeat };

/**
 * Everything a `<Image>` needs to be drawn that does not need Skia: which source to draw, how to fit it into the
 * frame, and the colours it is drawn with.
 *
 * The pixels are deliberately absent. Decoding produces an `SkImage`, and the scene links no Skia, so the decoded
 * image lives in the painter-side cache and this struct carries only the key into it. That is the same split
 * `SceneTextContent` makes: the inputs travel through the scene, the Skia object is built where Skia is linked.
 *
 * On a retained `SceneNode` the tint is the colour as authored and `opacity` is unused. Only a snapshot resolves
 * them: the tint alpha carries the inherited opacity, exactly like every other colour a primitive emits, and
 * `opacity` carries the same value for the untinted case, where there is no colour to fold it into.
 */
struct SceneImageContent {
    std::string uri;
    SceneImageResizeMode resizeMode{SceneImageResizeMode::Stretch};
    uint32_t tintColorArgb{};
    float opacity{1.0F};
};

/**
 * One painted node in surface coordinates: the frame origin is absolute, composed from every ancestor frame,
 * because a renderer that walks a flat list per frame must not repeat the tree walk.
 *
 * `frame` is the untransformed border box; `matrix` carries this node's transform composed with every ancestor
 * transform. Colours are packed ARGB with the cumulative opacity of this node and all its ancestors already
 * multiplied into the alpha channel, so the painter never composes anything.
 */
struct ScenePrimitive {
    facebook::react::Rect frame;
    SceneMatrix matrix;
    std::vector<SceneClip> clips;
    facebook::react::BorderRadii borderRadii;
    facebook::react::BorderWidths borderWidths;
    facebook::react::RectangleEdges<uint32_t> borderColorsArgb;
    uint32_t backgroundColorArgb{};
    std::optional<SceneTextContent> text;
    std::optional<SceneImageContent> image;
};

using SceneSnapshot = std::vector<ScenePrimitive>;

/**
 * The region that has to be repainted, as axis-aligned rectangles in absolute surface coordinates.
 *
 * The list is bounded: past a small cap it collapses into its own bounding rectangle, so a large mutation batch
 * costs one oversized repaint rather than an unbounded rectangle list. Rectangles may overlap; nothing here merges
 * them pairwise, because the intersection costs a repaint and a merge heuristic costs code.
 */
using SceneDamage = std::vector<facebook::react::Rect>;

/**
 * Appends every rectangle of `additions` to `damage` under the same bounded-merge policy the scene applies.
 */
void mergeDamage(SceneDamage& damage, const SceneDamage& additions);

struct SceneNode {
    facebook::react::Tag tag{};
    facebook::react::Tag parentTag{};
    std::string componentName;
    std::vector<facebook::react::Tag> childTags;
    facebook::react::LayoutMetrics layoutMetrics{};
    std::optional<facebook::react::SharedColor> backgroundColor;
    facebook::react::BorderMetrics borderMetrics{};
    SceneMatrix transform{};
    float opacity{1.0F};
    bool clipsChildren{false};
    std::optional<SceneTextContent> text;
    std::optional<SceneImageContent> image;

    /**
     * The `contentOffset` a `<ScrollView>` mounted with, and the marker that this node is one at all.
     *
     * Fabric lays a ScrollView's children out relative to the ScrollView itself and never moves them, so scrolling
     * is entirely this number: children are composed from the frame origin minus it. That is the same
     * `-contentOffset` translation `ScrollViewShadowNode::getContentOriginOffset` applies for hit testing, so the
     * picture and the hit test agree by construction.
     */
    std::optional<facebook::react::Point> scrollContentOffset;
};

using SceneNodes = std::unordered_map<facebook::react::Tag, SceneNode>;

struct ScenePaintState;

/**
 * The retained render tree mounting transactions are applied to.
 *
 * The tree survives across commits: Fabric computes the diff and hands us the five mutation operations, so the
 * scene is mutated in place and never rebuilt. Node identity is the Fabric tag, and child order is mount order —
 * which is already `zIndex` order, because Fabric stable-sorts siblings by `ShadowNode::getOrderIndex` before it
 * diffs them.
 *
 * Frames are stored exactly as Fabric computed them, relative to the parent, and are composed into absolute
 * coordinates by `snapshot` rather than by whoever draws them. Opacity, transforms and `overflow: hidden` clips
 * are composed down the tree in the same walk, for the same reason.
 *
 * Every mutation also accumulates damage: the region a renderer has to repaint for the picture to match the scene
 * again. The rule is uniform — a mutation damages the extent of the affected subtree as it was before the mutation
 * and as it is after it, so a create damages only its new extent, a delete only its old one, and a move both. A
 * subtree extent is the union of the absolute frames of the primitives it paints, each mapped through its own
 * transform and cut by its inherited clips, which is why a change to a parent's transform, opacity or clip damages
 * everything below it without any per-descendant analysis. Borders need no term of their own: React Native draws
 * them inside the frame. See *Damage tracking* in docs/cpp-toolchain.md.
 *
 * Threading contract: this type is not synchronised. Its owner serialises access.
 */
class RetainedScene final {
public:
    void createSurfaceRoot(facebook::react::SurfaceId surfaceId, facebook::react::Size size);
    void createNode(const facebook::react::ShadowView& shadowView);
    void deleteNode(facebook::react::Tag tag);
    void insertChild(facebook::react::Tag parentTag, const facebook::react::ShadowView& childShadowView, int index);
    void removeChild(facebook::react::Tag parentTag, const facebook::react::ShadowView& childShadowView);
    void updateNode(const facebook::react::ShadowView& shadowView);

    /**
     * Damages every node drawing `uri`, which is what turns a decode that finished after the last frame into a
     * repaint. Nothing else can: a decode changes no shadow node, so Fabric emits no mutation for it.
     */
    void damageImageSource(const std::string& uri);
    SceneSnapshot snapshot() const;
    SceneDamage takeDamage();
    std::string dump() const;

private:
    SceneNode& writeNode(const facebook::react::ShadowView& shadowView);
    std::vector<facebook::react::Tag> sortedRootTags() const;
    void appendPrimitives(SceneSnapshot& primitives, facebook::react::Tag tag, const ScenePaintState& state) const;
    void appendNode(std::string& output, facebook::react::Tag tag, size_t depth) const;
    std::optional<facebook::react::Rect> subtreeExtent(facebook::react::Tag tag) const;
    void damageSubtree(facebook::react::Tag tag);

    SceneNodes nodes_;
    SceneDamage damage_;
};

} // namespace react_native_linux
