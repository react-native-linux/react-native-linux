#pragma once

#include <react/renderer/core/LayoutMetrics.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/graphics/Size.h>
#include <react/renderer/mounting/ShadowView.h>

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace react_native_linux {

struct SceneNode {
    facebook::react::Tag tag{};
    facebook::react::Tag parentTag{};
    std::string componentName;
    std::vector<facebook::react::Tag> childTags;
    facebook::react::LayoutMetrics layoutMetrics{};
    std::optional<std::string> backgroundColor;
};

/**
 * The retained render tree mounting transactions are applied to.
 *
 * The tree survives across commits: Fabric computes the diff and hands us the five mutation operations, so the
 * scene is mutated in place and never rebuilt. Node identity is the Fabric tag, and child order is mount order.
 * The surface root is created by the host, mirroring every other React Native platform, because the differ never
 * emits a Create for the root shadow node.
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
    std::string dump() const;

private:
    SceneNode& writeNode(const facebook::react::ShadowView& shadowView);
    void appendNode(std::string& output, facebook::react::Tag tag, size_t depth) const;

    std::unordered_map<facebook::react::Tag, SceneNode> nodes_;
};

} // namespace react_native_linux
