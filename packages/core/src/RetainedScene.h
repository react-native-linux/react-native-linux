#pragma once

#include "AnimatedPropAllowlist.h"

#include <folly/dynamic.h>
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
#include <react/renderer/graphics/Transform.h>
#include <react/renderer/mounting/ShadowView.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
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
 * One rounded box, in the untransformed coordinates the frame it came from is written in.
 *
 * This is the single geometry issue #99 requires every consumer of a node's shape to derive from: the background
 * fill, the outer edge of the border ring, the `overflow: hidden` clip, the content clip an `<Image>` or a
 * gradient layer is cut by, and the region a press has to land in. Five consumers that each computed their own
 * rounded rect is what every upstream rounded-corner bug is made of; there is one here, and both the painter and
 * the hit test read it.
 *
 * It carries no Skia type, because the hit test is one of those consumers and this translation unit links no
 * Skia. `ScenePainter` turns it into an `SkRRect` where it draws.
 */
struct SceneRoundedBox {
    facebook::react::Rect bounds;
    facebook::react::BorderRadii radii;
};

/**
 * The rounded border box of a frame: the box itself and the radii `BaseViewProps::resolveBorderMetrics` already
 * cascaded, resolved against the frame and clamped so adjacent corners cannot overlap.
 */
SceneRoundedBox roundedBorderBox(const facebook::react::Rect& frame, const facebook::react::BorderRadii& radii);

/**
 * The box inside a ring of `widths`. React Native draws borders inside the frame, so the inner edge is the border
 * box inset by the per-side widths with every corner reduced by the two widths that meet at it, which is the CSS
 * inner-radius rule. Widths larger than the box collapse it to nothing rather than inverting it.
 */
SceneRoundedBox roundedContentBox(const SceneRoundedBox& borderBox, const facebook::react::BorderWidths& widths);

/**
 * Whether the point is inside the rounded box: inside its bounds and inside each of the four corner ellipses.
 *
 * The corner test is the ellipse equation multiplied through by `(horizontal * vertical)^2`, so it needs no
 * division and therefore no zero-radius case: a square corner has an overshoot of zero on both axes and can never
 * be outside. This is what makes a press miss a rounded card exactly where the card's corner was not painted.
 */
bool roundedBoxContainsPoint(const SceneRoundedBox& box, facebook::react::Point point);

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
    float scrollOffsetY{0.0F};
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
 * One decoded source: its frames in order, and how long each of them is shown.
 *
 * Every frame is type-erased to `std::shared_ptr<void>` exactly as upstream's own `ImageResponse` erases its
 * bitmap, because the scene links no Skia; the painter casts one back to `SkImage`. A still image is one frame
 * and no durations. An animated one carries a duration per frame, in the milliseconds the codec read out of the
 * file, and `repetitionCount` as `SkCodec::getRepetitionCount` reports it: how many times to play *after* the
 * first play-through, with `kAnimatedImageRepeatsForever` for a loop that never ends.
 *
 * The whole animation is one cache entry and one shared pointer, so a node drawing an animated source owns every
 * frame of it for exactly as long as it draws it — the lifetime rule of issue #108 applied to N bitmaps instead
 * of one.
 */
struct DecodedImageFrames {
    std::vector<std::shared_ptr<void>> frames;
    std::vector<int32_t> frameDurationsMilliseconds;
    int32_t repetitionCount{0};
};

constexpr int32_t kAnimatedImageRepeatsForever = -1;

/**
 * Everything a `<Image>` needs to be drawn that does not need Skia: which source to draw, how to fit it into the
 * frame, and the colours it is drawn with.
 *
 * `frames` is the decoded source, held by the node rather than looked up by `uri` at paint time so that a decoded
 * bitmap lives exactly as long as the nodes drawing it and the cache holding it — issue #108. A node that has it
 * cannot be blanked by an eviction (react-native-macos#921), and the last node dropping it is what makes the
 * pixels reclaimable. `elapsedMilliseconds` is how far into the animation this node is; it advances only while
 * the node is on screen, which is what pauses a clipped-out animation without losing its place.
 *
 * On a retained `SceneNode` the tint is the colour as authored and `opacity` and `pixels` are unused. Only a
 * snapshot resolves them: `pixels` is the one frame this snapshot draws, the tint alpha carries the inherited
 * opacity exactly like every other colour a primitive emits, and `opacity` carries the same value for the
 * untinted case, where there is no colour to fold it into.
 */
struct SceneImageContent {
    std::string uri;
    std::shared_ptr<const DecodedImageFrames> frames;
    std::shared_ptr<void> pixels;
    double elapsedMilliseconds{0.0};
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
/**
 * One shadow a node casts, in the CSS `box-shadow` vocabulary React Native 0.76 adopted: an offset, a blur radius,
 * a spread distance, a colour, and whether it is cast inside the box or outside it. The legacy iOS quartet
 * (`shadowColor`, `shadowOffset`, `shadowOpacity`, `shadowRadius`) resolves onto the same shape, so a
 * cross-platform app casts one drawing whichever props it wrote.
 *
 * `blurRadius` is the CSS one — the full width of the blur — and the painter converts it to the Gaussian sigma
 * Skia wants; iOS' `shadowRadius` *is* the sigma, which is why the quartet maps with a factor of two.
 */
struct SceneShadow {
    float offsetX{0.0F};
    float offsetY{0.0F};
    float blurRadius{0.0F};
    float spreadDistance{0.0F};
    uint32_t colorArgb{};
    bool isInset{false};
};

/**
 * The rectangle a node's outset shadows can reach: `bounds` grown on each side by the furthest any shadow
 * extends past it — offset plus spread plus one and a half blur radii, which is where the painter's Gaussian
 * stops putting pixels — or `bounds` unchanged when every shadow is inset or there is none. This is what a
 * shadowed node damages, and it is arithmetic so the gate can hold it.
 */
facebook::react::Rect shadowExtent(const facebook::react::Rect& bounds, const std::vector<SceneShadow>& shadows);

struct ScenePrimitive {
    /**
     * The node this was painted for. The rest of a primitive is geometry and colour, deliberately: the painter
     * needs no identity. This is here because a proof does — the hit-versus-paint agreement of issue #35 has to
     * say which node painted the pixel it is looking at, and the hit test answers in tags.
     */
    facebook::react::Tag tag{};
    facebook::react::Rect frame;
    SceneMatrix matrix;
    std::vector<SceneClip> clips;
    facebook::react::BorderRadii borderRadii;
    facebook::react::BorderWidths borderWidths;
    facebook::react::BorderStyles borderStyles;
    facebook::react::RectangleEdges<uint32_t> borderColorsArgb;
    std::vector<SceneShadow> shadows;
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
 * A node one snapshot painted and another does not paint in the same place: which node, where it was, and where
 * it is now. `isMissing` means the second snapshot does not paint it at all, and `after` is then meaningless.
 */
struct ScenePrimitiveDisplacement {
    facebook::react::Tag tag{};
    facebook::react::Rect before;
    facebook::react::Rect after;
    bool isMissing{false};
};

/**
 * The first node of `before` that `after` does not paint in the same place, or nothing when every one of them is
 * exactly where it was.
 *
 * Nodes are matched **by tag rather than by index**, so a snapshot that gained nodes in front of the ones it
 * already had is still compared node for node — which is the question
 * `maintainVisibleContentPosition` asks after a prepend, and the only way to ask it.
 *
 * Two rules make the answer meaningful:
 *
 * - **A node the second snapshot does not paint at all is a displacement**, not something to skip. Content that
 *   was on screen and is not any more has not stayed put, and a comparison that ignored it would pass a snapshot
 *   that replaced everything it was supposed to preserve.
 * - **A node whose size changed is skipped**, because it is not the same box any more. A `<ScrollView>`'s content
 *   container grows by exactly what is prepended into it, so it is the node that is *supposed* to move, and
 *   naming it here would be a rule about one fixture rather than about the scene.
 *
 * What is compared is the absolute frame the node was painted at. The matrix and the clips are deliberately not:
 * see `--first-frame-golden`, whose question is a different one.
 */
std::optional<ScenePrimitiveDisplacement> findDisplacedPrimitive(const SceneSnapshot& before,
                                                                 const SceneSnapshot& after);

/**
 * What a hit test found: the deepest node under the point, the absolute origin it paints at, and the composed
 * transform and frame origin a pointer event's target-local offset is measured against.
 *
 * `matrix` and `frameOrigin` travel with the tag for the same reason `origin` does: `hitTestNode`'s own walk is
 * the one place that has them for the node it just matched, whether or not that node painted anything at all — a
 * fully transparent, rotated `opacity: 0` target composes a matrix nothing else can hand back, because
 * `RetainedScene::snapshot` drops it before a caller could read it off a primitive. Reading the transform
 * anywhere else risks pairing a hit from one scene revision with a snapshot from the next; this struct is
 * produced by one tree walk under one lock, so the two can never disagree. `origin` remains for the callers that
 * only need the absolute position — `mapPoint(matrix, frameOrigin)`, kept rather than derived, because #97's
 * tests already pin it as its own value. A `tag` of zero means nothing under the point can be a pointer target,
 * and every other field is then meaningless.
 */
struct SceneHit {
    facebook::react::Tag tag{};
    facebook::react::Point origin{};
    SceneMatrix matrix{};
    facebook::react::Point frameOrigin{};
};

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
    std::vector<SceneShadow> shadows;
    facebook::react::BorderMetrics borderMetrics{};
    SceneMatrix transform{};

    /**
     * The origin the node's transform is resolved about, as authored. It is kept unresolved, next to the resolved
     * matrix, because `applyAnimatedProps` re-resolves a transform that arrives without one and a percentage
     * origin is only meaningful against the node's frame.
     */
    facebook::react::TransformOrigin transformOrigin{};
    float opacity{1.0F};

    /**
     * Whether this node and its children can be pointer targets, as authored. It is the only prop the scene keeps
     * that paints nothing: `findNodeAtPoint` answers with the painted geometry, so it also has to answer with the
     * same `pointerEvents` rule `ConcreteViewShadowNode::canBeTouchTarget` applies to the shadow tree.
     */
    facebook::react::PointerEventsMode pointerEvents{facebook::react::PointerEventsMode::Auto};
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
     * Whether `tag` names a node the scene holds. A mutation that updates, removes or deletes a tag this returns
     * false for is describing a node the scene never mounted or has already dropped, which is the missing-tag
     * case its owner counts instead of applying blind. See *Commit termination and mounting atomicity* in
     * docs/cpp-toolchain.md.
     */
    bool hasNode(facebook::react::Tag tag) const;

    /**
     * Attaches `decoded` to every node drawing `uri` and damages them, which is what turns a decode that
     * finished after the last frame into a repaint. Nothing else can: a decode changes no shadow node, so Fabric
     * emits no mutation for it.
     *
     * The frames are passed in rather than fetched from the provider, because the cache is allowed to refuse
     * them: an animation whose frames exceed the whole capacity is never admitted, and asking the provider for it
     * would answer null and paint a blank box. A source the cache refused is then owned by the nodes drawing it
     * and by nothing else, which is the lifetime rule of #108 with the cache's share of it left out.
     */
    void damageImageSource(const std::string& uri, const std::shared_ptr<const DecodedImageFrames>& decoded);

    /**
     * Advances every animated `<Image>` by one frame of `frameMilliseconds`, damages the ones that changed frame,
     * and reports whether any of them did.
     *
     * The schedule is `animatedImageFrameIndex`: which frame is on screen is a function of the wall-clock time
     * elapsed since the animation started and of the durations the codec read out of the file, never of how many
     * times this has been called. A 120 Hz display therefore shows the same frame at the same instant a 60 Hz one
     * does, which is react-native#33039 — a GIF that runs at twice the speed on a 120 Hz device — made
     * impossible by construction rather than by a correction.
     *
     * A node the `overflow: hidden` of an ancestor has clipped away entirely does not advance at all: no elapsed
     * time accumulates for it, so it schedules no frame and damages nothing while it is off screen, and the first
     * advance after it is visible again resumes from the frame it paused on.
     *
     * Damage is the node's own extent, which for an `<Image>` is its box cut by the clips it inherits: a looping
     * animation repaints its own rectangle per frame and nothing else. See *Damage tracking* in
     * docs/cpp-toolchain.md.
     */
    bool advanceImageAnimations(double frameMilliseconds);

    /**
     * Where decoded frames come from: the image pipeline's cache, asked by source URI. Set once by the host, and
     * unset in every test that does not decode anything, in which case an `<Image>` node mounts with no pixels
     * and paints nothing until a decode reports one.
     *
     * The provider is asked at mount and at `damageImageSource`, never at paint time, so the pixels a frame draws
     * are the ones its nodes were holding when it was snapshotted — an eviction between the two cannot blank a
     * node that had them. See *Image* in docs/cpp-toolchain.md.
     *
     * Threading contract: called on whichever thread is writing the scene, under its owner's mutex.
     */
    using DecodedImageProvider = std::function<std::shared_ptr<const DecodedImageFrames>(const std::string& uri)>;

    void setDecodedImageProvider(DecodedImageProvider decodedImages);

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

    /**
     * Applies the non-layout props an animation frame changed, as `animationbackend::packAnimatedProps` packs
     * them: `opacity` as a double, `backgroundColor` as a packed int32 colour, `transform` as the raw operation
     * array. Each one is written where `readPaintProps` writes it, through the same conversions, so a node the
     * fast path moved and a node a commit moved carry the same matrix and the same colours.
     *
     * The subtree is damaged before and after, exactly as `updateNode` does it, because a node that moves stops
     * being correct where it was as well as where it now is. This is the fourth thing besides a mutation, a decode
     * and a focus change that decides what the next frame paints, and the only one React never commits.
     *
     * `kAnimatableProps` is the one table that decides which of the three a key is, so the set this applies and
     * the set the diagnostic names are the same set by construction.
     *
     * Returns the props it dropped, in arrival order, each with why — outside the table, or carrying a value that
     * is not finite — for its caller to count and name. A prop the driver is allowed to send and this does not
     * apply is a silently wrong picture, which is the failure the whole missing-tag policy exists to remove. An
     * unknown tag and a payload that is not an object are both no-ops.
     */
    std::vector<RejectedAnimatedProp> applyAnimatedProps(facebook::react::Tag tag, const folly::dynamic& props);

    /**
     * The deepest node painted under `surfacePoint`, searched from `rootTag` down, and the absolute origin it was
     * painted at.
     *
     * This is the hit test, and it reads the same state the painter reads for the reason issue #97 exists: an
     * animation frame writes the retained scene and commits nothing, so the shadow tree's `LayoutMetrics` describe
     * where a moving node started rather than where it is. Children are visited last-first, which is the reverse
     * of paint order and therefore front-to-back, and each node's frame is tested by mapping the point through the
     * inverse of the composed matrix `snapshot` would paint it with — so one composition answers both questions.
     *
     * A node paints inside every `overflow: hidden` ancestor it inherited, so the clip stack is part of the test.
     * The node and every clip are tested as `roundedBorderBox` — the same `SceneRoundedBox` the painter fills,
     * rings and clips to — so a press outside a rounded corner's arc misses, which is issue #99. A node that
     * paints nothing is still a target, exactly as an empty `<View>` is on every other platform, and so is a node
     * animated to `opacity: 0` — see *Hit-testing under animation* in docs/cpp-toolchain.md.
     */
    SceneHit findNodeAtPoint(facebook::react::Tag rootTag, facebook::react::Point surfacePoint) const;
    SceneSnapshot snapshot() const;
    SceneDamage takeDamage();
    std::string dump() const;

private:
    SceneNode& writeNode(const facebook::react::ShadowView& shadowView);
    std::vector<facebook::react::Tag> sortedRootTags() const;
    void appendPrimitives(SceneSnapshot& primitives, facebook::react::Tag tag, const ScenePaintState& state) const;
    SceneHit hitTestNode(facebook::react::Tag tag, facebook::react::Point surfacePoint,
                         const ScenePaintState& state) const;
    void appendNode(std::string& output, facebook::react::Tag tag, size_t depth) const;
    std::optional<facebook::react::Rect> subtreeExtent(facebook::react::Tag tag) const;
    void damageSubtree(facebook::react::Tag tag);

    SceneNodes nodes_;
    SceneDamage damage_;
    std::unordered_map<facebook::react::Tag, SceneEditorState> editorStates_;
    facebook::react::Tag focusedTag_{0};
    bool isFocusVisible_{false};
    DecodedImageProvider decodedImages_;
};

} // namespace react_native_linux
