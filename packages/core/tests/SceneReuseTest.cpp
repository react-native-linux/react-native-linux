#include "LinuxMountingManager.h"
#include "RetainedScene.h"
#include "TextInputComponent.h"

#include <gtest/gtest.h>

#include <stdexcept>

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/AttributedStringBox.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>
#include <react/renderer/attributedstring/TextAttributes.h>
#include <react/renderer/components/image/ImageProps.h>
#include <react/renderer/components/image/ImageState.h>
#include <react/renderer/components/scrollview/ScrollViewState.h>
#include <react/renderer/components/text/ParagraphState.h>
#include <react/renderer/components/textinput/TextInputState.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteState.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/core/ShadowNodeFamily.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/graphics/Size.h>
#include <react/renderer/graphics/ValueUnit.h>
#include <react/renderer/imagemanager/ImageRequest.h>
#include <react/renderer/imagemanager/ImageRequestParams.h>
#include <react/renderer/imagemanager/primitives.h>
#include <react/renderer/mounting/MountingTransaction.h>
#include <react/renderer/mounting/ShadowView.h>
#include <react/renderer/mounting/ShadowViewMutation.h>
#include <react/renderer/telemetry/TransactionTelemetry.h>
#include <yoga/enums/Edge.h>
#include <yoga/style/StyleLength.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Issue #107: `RetainedScene` reuses a Fabric tag across a delete-then-create pair exactly the way iOS Fabric
// recycles a view, and every one of the upstream `prepareForRecycle` bugs it links (core#55090, core#53050,
// core#55768, core#48790) is the same hazard with a different noun — state keyed by something that survives a
// node it should have died with. This file is the mutation script the issue asks for: build a node up, replace it
// or its neighbour, and assert the replacement started clean.

namespace {

using facebook::react::MountingTransaction;
using facebook::react::Point;
using facebook::react::Rect;
using facebook::react::SharedColor;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::ShadowViewMutationList;
using facebook::react::Size;
using facebook::react::Tag;
using facebook::react::UnitType;
using facebook::react::ValueUnit;
using facebook::react::ViewProps;
using react_native_linux::LinuxMountingManager;
using react_native_linux::RetainedScene;
using react_native_linux::SceneEditorState;
using react_native_linux::SceneSnapshot;
using react_native_linux::TextInputProps;

namespace yoga = facebook::yoga;

constexpr Tag kSurfaceTag = 1;
constexpr uint32_t kBlueArgb = 0xFF3366CCU;
constexpr uint32_t kRedArgb = 0xFFCC3333U;

Rect makeRect(float x, float y, float width, float height) {
    return Rect{.origin = Point{.x = x, .y = y}, .size = Size{.width = width, .height = height}};
}

SharedColor blue() {
    return facebook::react::colorFromRGBA(51, 102, 204, 255);
}

SharedColor red() {
    return facebook::react::colorFromRGBA(204, 51, 51, 255);
}

ShadowView makeView(Tag tag, Rect frame) {
    ShadowView shadowView;

    shadowView.tag = tag;
    shadowView.componentName = "View";
    shadowView.layoutMetrics.frame = frame;

    return shadowView;
}

ShadowView makeStyledView(Tag tag, Rect frame, const std::shared_ptr<ViewProps>& viewProps) {
    ShadowView shadowView = makeView(tag, frame);

    shadowView.props = viewProps;

    return shadowView;
}

std::shared_ptr<ViewProps> propsWithBackground(SharedColor backgroundColor) {
    const std::shared_ptr<ViewProps> viewProps = std::make_shared<ViewProps>();

    viewProps->backgroundColor = backgroundColor;

    return viewProps;
}

ShadowView makePaintedView(Tag tag, Rect frame, SharedColor backgroundColor) {
    return makeStyledView(tag, frame, propsWithBackground(backgroundColor));
}

/**
 * A view with every non-content paint prop turned on at once — opacity, a border, and a radius — so a single
 * replacement test can prove none of the three survives it, the way `propsWithBackground` alone could not.
 */
std::shared_ptr<ViewProps> decoratedProps(SharedColor backgroundColor, SharedColor borderColor) {
    const std::shared_ptr<ViewProps> viewProps = propsWithBackground(backgroundColor);

    viewProps->opacity = 0.5;
    viewProps->borderRadii.all = ValueUnit{20.0F, UnitType::Point};
    viewProps->borderColors.all = borderColor;
    viewProps->yogaStyle.setBorder(yoga::Edge::All, yoga::StyleLength::points(4));

    return viewProps;
}

/**
 * A `<Paragraph>` as it reaches the mounting layer, exactly as `SceneTest.cpp` builds one: the nested `<Text>`
 * and `<RawText>` nodes never do, so the flattened `AttributedString` arrives inside `ParagraphState`.
 */
ShadowView makeParagraph(Tag tag, Rect frame, const std::string& text) {
    facebook::react::AttributedString attributedString;

    if (!text.empty()) {
        facebook::react::AttributedString::Fragment fragment;

        fragment.string = text;
        fragment.textAttributes = facebook::react::TextAttributes::defaultTextAttributes();
        attributedString.appendFragment(std::move(fragment));
    }

    ShadowView shadowView;

    shadowView.tag = tag;
    shadowView.componentName = "Paragraph";
    shadowView.layoutMetrics.frame = frame;
    shadowView.state = std::make_shared<const facebook::react::ConcreteState<facebook::react::ParagraphState>>(
        std::make_shared<const facebook::react::ParagraphState>(
            facebook::react::ParagraphState{attributedString, facebook::react::ParagraphAttributes{}, {}}),
        facebook::react::ShadowNodeFamily::Weak{});

    return shadowView;
}

ShadowView makeImage(Tag tag, Rect frame, const std::string& uri, SharedColor tintColor) {
    const std::shared_ptr<facebook::react::ImageProps> imageProps =
        std::make_shared<facebook::react::ImageProps>();

    imageProps->resizeMode = facebook::react::ImageResizeMode::Cover;
    imageProps->tintColor = tintColor;

    facebook::react::ImageSource imageSource;

    imageSource.type = facebook::react::ImageSource::Type::Local;
    imageSource.uri = uri;

    ShadowView shadowView;

    shadowView.tag = tag;
    shadowView.componentName = "Image";
    shadowView.layoutMetrics.frame = frame;
    shadowView.props = imageProps;
    shadowView.state = std::make_shared<const facebook::react::ConcreteState<facebook::react::ImageState>>(
        std::make_shared<const facebook::react::ImageState>(
            imageSource, facebook::react::ImageRequest{imageSource, nullptr},
            facebook::react::ImageRequestParams{}),
        facebook::react::ShadowNodeFamily::Weak{});

    return shadowView;
}

ShadowView makeScrollView(Tag tag, Rect frame, Point contentOffset, Rect contentBoundingRect) {
    ShadowView shadowView;

    shadowView.tag = tag;
    shadowView.componentName = "ScrollView";
    shadowView.layoutMetrics.frame = frame;
    shadowView.state = std::make_shared<const facebook::react::ConcreteState<facebook::react::ScrollViewState>>(
        std::make_shared<const facebook::react::ScrollViewState>(
            facebook::react::ScrollViewState{contentOffset, contentBoundingRect, 0}),
        facebook::react::ShadowNodeFamily::Weak{});

    return shadowView;
}

std::shared_ptr<TextInputProps> textInputProps() {
    return std::make_shared<TextInputProps>();
}

/**
 * A `<TextInput>` as it reaches the mounting layer, exactly as `SceneTest.cpp` builds one: the value lives in
 * `TextInputState`, which is the one description of the field's contents the picture and React share.
 */
ShadowView makeTextInput(Tag tag, Rect frame, const std::string& value,
                         const std::shared_ptr<TextInputProps>& props) {
    facebook::react::AttributedString attributedString;

    if (!value.empty()) {
        facebook::react::AttributedString::Fragment fragment;

        fragment.string = value;
        fragment.textAttributes = facebook::react::TextAttributes::defaultTextAttributes();
        attributedString.appendFragment(std::move(fragment));
    }

    ShadowView shadowView;

    shadowView.tag = tag;
    shadowView.componentName = "TextInput";
    shadowView.layoutMetrics.frame = frame;
    shadowView.props = props;
    shadowView.state = std::make_shared<const facebook::react::ConcreteState<facebook::react::TextInputState>>(
        std::make_shared<const facebook::react::TextInputState>(
            facebook::react::TextInputState{facebook::react::AttributedStringBox{attributedString}, attributedString,
                                            facebook::react::ParagraphAttributes{}, 0}),
        facebook::react::ShadowNodeFamily::Weak{});

    return shadowView;
}

void insertChildAt(RetainedScene& scene, Tag parentTag, const ShadowView& child, int index) {
    scene.createNode(child);
    scene.insertChild(parentTag, child, index);
}

void addChild(RetainedScene& scene, Tag parentTag, const ShadowView& child) {
    insertChildAt(scene, parentTag, child, 0);
}

void removeAndDelete(RetainedScene& scene, Tag parentTag, const ShadowView& child) {
    scene.removeChild(parentTag, child);
    scene.deleteNode(child.tag);
}

MountingTransaction transactionOf(ShadowViewMutationList&& mutations) {
    return MountingTransaction{kSurfaceTag, 1, std::move(mutations), facebook::react::TransactionTelemetry{}};
}

// Case 1: delete a node, then create a new node with the same tag in a later transaction.

TEST(RetainedSceneReuseTest, AScrollViewReplacedByTheSameTagStartsWithNoContentOffset) {
    RetainedScene scene;
    const Rect scrollFrame = makeRect(60, 60, 200, 150);
    const ShadowView oldScrollView = makeScrollView(2, scrollFrame, Point{.x = 0, .y = 120}, makeRect(0, 0, 200, 470));
    const ShadowView oldRow = makePaintedView(3, makeRect(0, 80, 200, 70), blue());

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, oldScrollView);
    addChild(scene, 2, oldRow);

    removeAndDelete(scene, 2, oldRow);
    removeAndDelete(scene, kSurfaceTag, oldScrollView);

    const ShadowView newScrollView = makeScrollView(2, scrollFrame, Point{}, makeRect(0, 0, 200, 470));
    const ShadowView newRow = makePaintedView(4, makeRect(0, 80, 200, 70), blue());

    addChild(scene, kSurfaceTag, newScrollView);
    addChild(scene, 2, newRow);

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_FLOAT_EQ(snapshot[0].frame.origin.y, 60 + 80);
}

TEST(RetainedSceneReuseTest, AParagraphReplacedByTheSameTagCarriesNoOldText) {
    RetainedScene scene;
    const ShadowView oldParagraph = makeParagraph(2, makeRect(0, 0, 200, 40), "the old paragraph");

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, oldParagraph);
    removeAndDelete(scene, kSurfaceTag, oldParagraph);

    addChild(scene, kSurfaceTag, makeParagraph(2, makeRect(0, 0, 200, 40), "new"));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].text.has_value());
    EXPECT_EQ(snapshot[0].text.value().attributedString.getString(), "new");
}

TEST(RetainedSceneReuseTest, AParagraphReplacedByAnEmptyOneCarriesNoOldTextEither) {
    RetainedScene scene;
    const ShadowView oldParagraph = makeParagraph(2, makeRect(0, 0, 200, 40), "the old paragraph");

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, oldParagraph);
    removeAndDelete(scene, kSurfaceTag, oldParagraph);

    addChild(scene, kSurfaceTag, makeParagraph(2, makeRect(0, 0, 200, 40), ""));

    EXPECT_TRUE(scene.snapshot().empty());
    EXPECT_EQ(scene.dump().find("text="), std::string::npos);
}

TEST(RetainedSceneReuseTest, AnImageReplacedByTheSameTagCarriesNoOldSourceOrTint) {
    RetainedScene scene;
    const ShadowView oldImage = makeImage(2, makeRect(0, 0, 64, 48), "old.png", red());

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, oldImage);
    removeAndDelete(scene, kSurfaceTag, oldImage);

    addChild(scene, kSurfaceTag, makeImage(2, makeRect(0, 0, 64, 48), "new.png", blue()));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].image.has_value());
    EXPECT_EQ(snapshot[0].image.value().uri, "new.png");
    EXPECT_EQ(snapshot[0].image.value().tintColorArgb, kBlueArgb);
}

TEST(RetainedSceneReuseTest, AnImageReplacedByAnUnrequestedSourceCarriesNoOldSourceEither) {
    RetainedScene scene;
    const ShadowView oldImage = makeImage(2, makeRect(0, 0, 64, 48), "old.png", red());

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, oldImage);
    removeAndDelete(scene, kSurfaceTag, oldImage);

    addChild(scene, kSurfaceTag, makeImage(2, makeRect(0, 0, 64, 48), "", SharedColor{}));

    EXPECT_TRUE(scene.snapshot().empty());
}

/**
 * The defect this test proves: `RetainedScene::deleteNode` erased `editorStates_[tag]` but left `focusedTag_`
 * pointing at a tag that no longer names any node, so a later `createNode` for the same tag inherited a focus
 * ring it never earned. Fixed in `RetainedScene::deleteNode` by clearing `focusedTag_`/`isFocusVisible_` when the
 * deleted tag was the focused one — see the comment there.
 */
TEST(RetainedSceneReuseTest, AFocusedNodeReplacedByTheSameTagDoesNotInheritTheFocusRing) {
    RetainedScene scene;
    const ShadowView oldView = makePaintedView(2, makeRect(10, 20, 200, 100), blue());

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, oldView);
    scene.setFocus(2, true);

    const SceneSnapshot beforeReplacement = scene.snapshot();

    ASSERT_EQ(beforeReplacement.size(), 1U);
    ASSERT_TRUE(beforeReplacement[0].focusRing);

    removeAndDelete(scene, kSurfaceTag, oldView);
    addChild(scene, kSurfaceTag, makePaintedView(2, makeRect(10, 20, 200, 100), blue()));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_FALSE(snapshot[0].focusRing);
}

TEST(RetainedSceneReuseTest, AFocusChangeAfterTheDeleteStillMarksTheNewNode) {
    RetainedScene scene;
    const ShadowView oldView = makePaintedView(2, makeRect(10, 20, 200, 100), blue());

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, oldView);
    scene.setFocus(2, true);
    removeAndDelete(scene, kSurfaceTag, oldView);
    addChild(scene, kSurfaceTag, makePaintedView(2, makeRect(10, 20, 200, 100), blue()));

    scene.setFocus(2, true);

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_TRUE(snapshot[0].focusRing);
}

TEST(RetainedSceneReuseTest, ADecoratedViewReplacedByAPlainOneCarriesNoOpacityBorderOrRadius) {
    RetainedScene scene;
    const ShadowView oldView = makeStyledView(2, makeRect(0, 0, 100, 100), decoratedProps(red(), blue()));

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, oldView);
    removeAndDelete(scene, kSurfaceTag, oldView);

    addChild(scene, kSurfaceTag, makePaintedView(2, makeRect(0, 0, 100, 100), blue()));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_EQ(snapshot[0].backgroundColorArgb, kBlueArgb);
    EXPECT_FLOAT_EQ(snapshot[0].borderWidths.left, 0);
    EXPECT_FLOAT_EQ(snapshot[0].borderRadii.topLeft.horizontal, 0);
    EXPECT_EQ(snapshot[0].borderColorsArgb.left, 0U);
}

/**
 * Case 4: a `<TextInput>` replaced in place. The buffer, the selection and the composing run all travel through
 * `LinuxMountingManager::setEditorState`, never through a `ShadowViewMutation`, so this is the one case that has
 * to go through the mounting manager rather than the bare scene to exercise the same code path production does.
 */
TEST(RetainedSceneReuseTest, ATextInputReplacedByTheSameTagStartsWithNoCaretSelectionOrComposition) {
    LinuxMountingManager mountingManager;
    const Rect fieldFrame = makeRect(10, 20, 200, 40);
    const ShadowView oldField = makeTextInput(2, fieldFrame, "secret", textInputProps());
    ShadowViewMutationList mount;

    mountingManager.startSurface(kSurfaceTag, Size{.width = 800, .height = 600});
    mount.push_back(ShadowViewMutation::CreateMutation(oldField));
    mount.push_back(ShadowViewMutation::InsertMutation(kSurfaceTag, oldField, 0));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mount)));
    mountingManager.setEditorState(2, SceneEditorState{.caretUtf16 = 5,
                                                       .selectionBeginUtf16 = 1,
                                                       .selectionEndUtf16 = 5,
                                                       .compositionBeginUtf16 = 2,
                                                       .compositionEndUtf16 = 4,
                                                       .scrollOffsetX = 12.0F,
                                                       .isCaretVisible = true});
    mountingManager.takeFrame();

    const ShadowView newField = makeTextInput(2, fieldFrame, "", textInputProps());
    ShadowViewMutationList replace;

    replace.push_back(ShadowViewMutation::RemoveMutation(kSurfaceTag, oldField, 0));
    replace.push_back(ShadowViewMutation::DeleteMutation(oldField));
    replace.push_back(ShadowViewMutation::CreateMutation(newField));
    replace.push_back(ShadowViewMutation::InsertMutation(kSurfaceTag, newField, 0));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(replace)));

    const SceneSnapshot snapshot = mountingManager.snapshotScene();

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].text.has_value());
    EXPECT_EQ(snapshot[0].text.value().attributedString.getString(), "");
    ASSERT_TRUE(snapshot[0].editor.has_value());

    const SceneEditorState& state = snapshot[0].editor.value().state;

    EXPECT_EQ(state.caretUtf16, 0U);
    EXPECT_EQ(state.selectionBeginUtf16, 0U);
    EXPECT_EQ(state.selectionEndUtf16, 0U);
    EXPECT_EQ(state.compositionBeginUtf16, 0U);
    EXPECT_EQ(state.compositionEndUtf16, 0U);
    EXPECT_FALSE(state.isCaretVisible);
    EXPECT_FLOAT_EQ(state.scrollOffsetX, 0.0F);
}

// Case 2: reordering siblings must not transfer any per-tag state between them — `nodes_` is keyed by tag, not by
// position, so a `removeChild` followed by an `insertChild` at a different index only ever touches the childTags
// list of the parent; this is the regression test that keeps that true rather than assumed.

TEST(RetainedSceneReuseTest, ReorderingSiblingsCarriesNoStateBetweenTags) {
    RetainedScene scene;
    // 100x100 so the requested 20-point radius survives resolveBorderMetrics' corner-overlap clamp unchanged
    // (the clamp only bites below 2x the radius) and is not confused with a clamp side effect.
    const ShadowView first = makeStyledView(2, makeRect(0, 0, 100, 100), decoratedProps(blue(), blue()));
    const ShadowView second = makePaintedView(3, makeRect(0, 0, 50, 50), red());

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    insertChildAt(scene, kSurfaceTag, first, 0);
    insertChildAt(scene, kSurfaceTag, second, 1);

    scene.removeChild(kSurfaceTag, first);
    scene.removeChild(kSurfaceTag, second);
    scene.insertChild(kSurfaceTag, second, 0);
    scene.insertChild(kSurfaceTag, first, 1);

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_EQ(snapshot[0].backgroundColorArgb, kRedArgb);
    EXPECT_FLOAT_EQ(snapshot[0].frame.size.width, 50);
    EXPECT_FLOAT_EQ(snapshot[0].borderRadii.topLeft.horizontal, 0);
    EXPECT_FLOAT_EQ(snapshot[0].borderWidths.left, 0);

    EXPECT_FLOAT_EQ(snapshot[1].frame.size.width, 100);
    EXPECT_FLOAT_EQ(snapshot[1].borderRadii.topLeft.horizontal, 20);
    EXPECT_FLOAT_EQ(snapshot[1].borderWidths.left, 4);
}

// Case 3: what the paragraph measure cache and the image cache are keyed by.
//
// Neither cache lives in this binary. `TextLayoutManager::measure`'s `textMeasureCache_` is defined in
// `TextLayoutManager.cpp`, and the decoded-image cache is populated from `ImagePipeline.cpp`/`ImageManager.cpp`;
// all three need Skia, and `packages/core/tests/CMakeLists.txt` never lists them — this suite links no Skia at
// all, which is the whole reason it can run under ASan/UBSan/TSan and score 100% coverage without a GPU. Per
// docs/cpp-toolchain.md ("The cache" under both *Text* and *Image*), both caches are keyed by content rather than
// by node identity: the paragraph cache by the attributed string, the paragraph attributes, the layout
// constraints and the pixel scale (deliberately ignoring colour), and the image cache — `ImageCache` in
// `ImageContent.h`, which *is* Skia-free and already covered by `ImageTest.cpp` — by source URI. Two nodes with
// identical content are therefore meant to share the immutable decoded/measured artifact; that is the cache
// working as designed, not the hazard #107 is about. The hazard would be something *mutable* leaking across that
// shared key, so what is tested here is the scene-layer half of the guarantee: `RetainedScene` copies the
// content it reads off each `ShadowView` into that node's own `SceneNode`, so two sibling nodes with identical
// text or the identical image URI never alias the same mutable storage, and mutating one can never reach the
// other through a shared tag or a shared cache entry.

const ScenePrimitive& primitiveAtY(const SceneSnapshot& snapshot, float y) {
    for (const ScenePrimitive& primitive : snapshot) {
        if (primitive.frame.origin.y == y) {
            return primitive;
        }
    }

    throw std::out_of_range("no primitive at that y");
}

TEST(RetainedSceneReuseTest, TwoParagraphsWithIdenticalTextShareNoMutableAttributedString) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeParagraph(2, makeRect(0, 0, 200, 40), "same"));
    addChild(scene, kSurfaceTag, makeParagraph(3, makeRect(0, 40, 200, 40), "same"));

    scene.updateNode(makeParagraph(2, makeRect(0, 0, 200, 40), "changed"));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_EQ(primitiveAtY(snapshot, 0).text.value().attributedString.getString(), "changed");
    EXPECT_EQ(primitiveAtY(snapshot, 40).text.value().attributedString.getString(), "same");
}

TEST(RetainedSceneReuseTest, TwoImagesWithTheSameUriShareNoMutableTint) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, makeImage(2, makeRect(0, 0, 64, 48), "shared.png", red()));
    addChild(scene, kSurfaceTag, makeImage(3, makeRect(0, 48, 64, 48), "shared.png", red()));

    scene.updateNode(makeImage(2, makeRect(0, 0, 64, 48), "shared.png", blue()));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_EQ(primitiveAtY(snapshot, 0).image.value().tintColorArgb, kBlueArgb);
    EXPECT_EQ(primitiveAtY(snapshot, 48).image.value().tintColorArgb, kRedArgb);
    EXPECT_EQ(primitiveAtY(snapshot, 0).image.value().uri, primitiveAtY(snapshot, 48).image.value().uri);
}

} // namespace
