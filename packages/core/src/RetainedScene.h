#pragma once

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
};

using SceneSnapshot = std::vector<ScenePrimitive>;

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
};

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
    SceneSnapshot snapshot() const;
    std::string dump() const;

private:
    SceneNode& writeNode(const facebook::react::ShadowView& shadowView);
    std::vector<facebook::react::Tag> sortedRootTags() const;
    void appendPrimitives(SceneSnapshot& primitives, facebook::react::Tag tag, const ScenePaintState& state) const;
    void appendNode(std::string& output, facebook::react::Tag tag, size_t depth) const;

    std::unordered_map<facebook::react::Tag, SceneNode> nodes_;
};

} // namespace react_native_linux
