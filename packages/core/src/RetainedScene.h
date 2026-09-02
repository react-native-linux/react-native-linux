#pragma once

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>
#include <react/renderer/components/view/primitives.h>
#include <react/renderer/core/LayoutMetrics.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/graphics/BackgroundImage.h>
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
 * Where a `<TextInput>`'s caret, selection and composing run are, as UTF-16 indices into the string the scene is
 * about to draw, plus how far a single-line field has scrolled to keep the caret visible.
 *
 * UTF-16 rather than bytes because that is the index space SkParagraph's `getRectsForRange` speaks, and the
 * conversion is arithmetic the editor has already done — see `EditorModel.h`. The painter therefore hands these
 * numbers straight to Skia and computes nothing.
 *
 * This half of a field's state does not arrive as a mounting transaction, because none of it is React's: a
 * blinking caret would be a commit per half-second and a drag-selection a commit per frame. It comes through
 * `setEditorState` instead, which is the same side channel `setFocus` uses and damages the field for the same
 * reason.
 */
struct SceneEditorState {
    size_t caretUtf16{0};
    size_t selectionBeginUtf16{0};
    size_t selectionEndUtf16{0};
    size_t compositionBeginUtf16{0};
    size_t compositionEndUtf16{0};
    float scrollOffsetX{0.0F};
    bool isCaretVisible{false};
};

/**
 * Everything a `<TextInput>` draws that its text does not: the caret, the selection highlight, the composing
 * run's underline, and whether the string in `SceneTextContent` is the value or the placeholder.
 *
 * The value itself is not here. It travels as ordinary text, in `SceneTextContent`, because a field's text is
 * laid out and painted by exactly the same code a `<Text>` is — and because it is the **masked** string when
 * `secureTextEntry` is set, so the buffer never reaches a paragraph at all. See *TextInput* in
 * docs/cpp-toolchain.md.
 */
struct SceneEditorContent {
    SceneEditorState state;
    uint32_t caretColorArgb{};
    uint32_t selectionColorArgb{};
    bool isPlaceholder{false};
    bool isMultiline{false};
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

    /**
     * The `experimental_backgroundImage` gradient layers, in the order React Native parsed them, which is CSS
     * paint order: the first entry is nearest the viewer and the last is behind all the others.
     *
     * These are React Native's own parsed types rather than resolved ramps, for the reason `SceneTextContent`
     * keeps an `AttributedString`: turning a gradient into stop positions needs the box it fills and the CSS
     * gradient-line length, which is Skia geometry rather than scene state. See *Gradients* in
     * docs/cpp-toolchain.md.
     */
    std::vector<facebook::react::BackgroundImage> backgroundImage;

    /**
     * The cumulative opacity the gradient layers are painted with. It is not folded into the colour stops the
     * way every other colour in a snapshot carries it, because a gradient has an unbounded number of colours and
     * one paint alpha covers all of them.
     */
    float backgroundImageOpacity{1.0F};
    std::optional<SceneTextContent> text;
    std::optional<SceneImageContent> image;
    std::optional<SceneEditorContent> editor;

    /**
     * Whether this node draws the focus ring, which is the focused node and only while focus arrived from the
     * keyboard. A primitive that paints nothing else is still emitted when this is set — a `<Pressable>` with no
     * background is still focusable — and the ring is drawn inside the border box, so the frame that damages the
     * node also damages its ring.
     */
    bool focusRing{false};
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
    std::vector<facebook::react::BackgroundImage> backgroundImage;
    facebook::react::BorderMetrics borderMetrics{};
    SceneMatrix transform{};
    float opacity{1.0F};
    bool clipsChildren{false};
    std::optional<SceneTextContent> text;
    std::optional<SceneImageContent> image;
    std::optional<SceneEditorContent> editor;

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

    /**
     * Marks which node draws the focus ring, and damages the node that stops drawing it and the node that starts.
     * The old node is damaged before the mark moves, because a node whose only reason to be painted was the ring
     * has no extent once the mark has left it.
     *
     * `tag` of zero is "nothing is focused", and `isFocusVisible` is false for a focus that came from a click:
     * the platform still knows where focus is — it is what the next Tab moves from — and the ring is what a
     * pointer does not draw, so an invisible focus damages nothing.
     */
    void setFocus(facebook::react::Tag tag, bool isFocusVisible);

    /**
     * Publishes where a `<TextInput>`'s caret, selection and composing run are, and damages that field when any
     * of it moved. An unchanged state damages nothing, which is what keeps a caret that is not blinking and a
     * field nobody is typing in from repainting every frame.
     */
    void setEditorState(facebook::react::Tag tag, const SceneEditorState& editorState);
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
    std::unordered_map<facebook::react::Tag, SceneEditorState> editorStates_;
    facebook::react::Tag focusedTag_{0};
    bool isFocusVisible_{false};
};

} // namespace react_native_linux
