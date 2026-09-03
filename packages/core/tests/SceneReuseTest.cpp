#include "LinuxMountingManager.h"
#include "RetainedScene.h"
#include "SceneTestSupport.h"

#include <gtest/gtest.h>

#include <stdexcept>

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/graphics/Size.h>
#include <react/renderer/graphics/ValueUnit.h>
#include <react/renderer/mounting/ShadowView.h>
#include <react/renderer/mounting/ShadowViewMutation.h>
#include <yoga/enums/Edge.h>
#include <yoga/style/StyleLength.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

// Issue #107: `RetainedScene` reuses a Fabric tag across a delete-then-create pair exactly the way iOS Fabric
// recycles a view, and every one of the upstream `prepareForRecycle` bugs it links (core#55090, core#53050,
// core#55768, core#48790) is the same hazard with a different noun — state keyed by something that survives a
// node it should have died with. This file is the mutation script the issue asks for: build a node up, replace it
// or its neighbour, and assert the replacement started clean.
//
// The fixture builders shared with `SceneTest.cpp` (makeRect/makeView/makePaintedView/makeParagraph/makeImage/
// makeScrollView/makeTextInput/addChild/transactionOf/kSurfaceTag/kBlueArgb/kRedArgb), and the `using`
// declarations every suite spells, live in `SceneTestSupport.h`; only the reuse-suite-specific helpers are
// defined here.

namespace {

namespace yoga = facebook::yoga;

/**
 * A view with every non-content paint prop turned on at once — opacity, a border, and a radius — so a single
 * replacement test can prove none of the three survives it, the way a plain painted view alone could not.
 */
std::shared_ptr<ViewProps> decoratedProps(SharedColor backgroundColor, SharedColor borderColor) {
    const std::shared_ptr<ViewProps> viewProps = propsWithBackground(backgroundColor);

    viewProps->opacity = 0.5;
    viewProps->borderRadii.all = ValueUnit{20.0F, UnitType::Point};
    viewProps->borderColors.all = borderColor;
    viewProps->yogaStyle.setBorder(yoga::Edge::All, yoga::StyleLength::points(4));

    return viewProps;
}

void insertChildAt(RetainedScene& scene, Tag parentTag, const ShadowView& child, int index) {
    scene.createNode(child);
    scene.insertChild(parentTag, child, index);
}

void removeAndDelete(RetainedScene& scene, Tag parentTag, const ShadowView& child) {
    scene.removeChild(parentTag, child);
    scene.deleteNode(child.tag);
}

// The mutation script every case-1 test below runs: delete the old node, then create its replacement under the
// same tag in what stands in for "a later transaction". Folding it into one call is what keeps five near-identical
// test bodies from being flagged as clones of each other.
void replaceWithSameTag(RetainedScene& scene, Tag parentTag, const ShadowView& oldView, const ShadowView& newView) {
    removeAndDelete(scene, parentTag, oldView);
    addChild(scene, parentTag, newView);
}

// The `LinuxMountingManager` analogue of the same script, for the one case (TextInput) whose replaced-in-place
// state travels through `setEditorState` rather than through `ShadowView` props alone and therefore has to be
// proven at the mounting-manager layer, not the bare `RetainedScene` one.
void replaceWithSameTag(LinuxMountingManager& mountingManager, Tag parentTag, const ShadowView& oldView,
                        const ShadowView& newView) {
    ShadowViewMutationList mutations;

    mutations.push_back(ShadowViewMutation::RemoveMutation(parentTag, oldView, 0));
    mutations.push_back(ShadowViewMutation::DeleteMutation(oldView));
    mutations.push_back(ShadowViewMutation::CreateMutation(newView));
    mutations.push_back(ShadowViewMutation::InsertMutation(parentTag, newView, 0));
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));
}

// The arrange every case-1 test shares: an 800x600 surface with one node under its root.
void mountUnderRoot(RetainedScene& scene, const ShadowView& view) {
    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, view);
}

// Arrange, replace and snapshot in one call, because otherwise every case-1 test body is the same six lines with
// one builder swapped and the duplication check reads them as copies of each other rather than as cases.
SceneSnapshot snapshotAfterReplacement(RetainedScene& scene, const ShadowView& oldView, const ShadowView& newView) {
    mountUnderRoot(scene, oldView);
    replaceWithSameTag(scene, kSurfaceTag, oldView, newView);

    return scene.snapshot();
}

// Both focus cases start from the same place: one painted node under the root, focused with the ring showing.
ShadowView mountFocusedView(RetainedScene& scene) {
    const ShadowView view = makePaintedView(2, makeRect(10, 20, 200, 100), blue());

    mountUnderRoot(scene, view);
    scene.setFocus(2, true);

    return view;
}

// Case 1: delete a node, then create a new node with the same tag in a later transaction.

TEST(RetainedSceneReuseTest, AScrollViewReplacedByTheSameTagStartsWithNoContentOffset) {
    RetainedScene scene;
    const Rect scrollFrame = makeRect(60, 60, 200, 150);
    const ShadowView oldScrollView = makeScrollView(2, scrollFrame, Point{.x = 0, .y = 120}, makeRect(0, 0, 200, 470));
    const ShadowView oldRow = makePaintedView(3, makeRect(0, 80, 200, 70), blue());

    mountUnderRoot(scene, oldScrollView);
    addChild(scene, 2, oldRow);

    removeAndDelete(scene, 2, oldRow);
    replaceWithSameTag(scene, kSurfaceTag, oldScrollView,
                       makeScrollView(2, scrollFrame, Point{}, makeRect(0, 0, 200, 470)));
    addChild(scene, 2, makePaintedView(4, makeRect(0, 80, 200, 70), blue()));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_FLOAT_EQ(snapshot[0].frame.origin.y, 60 + 80);
}

TEST(RetainedSceneReuseTest, AParagraphReplacedByTheSameTagCarriesNoOldText) {
    RetainedScene scene;
    const SceneSnapshot snapshot =
        snapshotAfterReplacement(scene, makeParagraph(2, makeRect(0, 0, 200, 40), "the old paragraph"),
                                 makeParagraph(2, makeRect(0, 0, 200, 40), "new"));

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].text.has_value());
    EXPECT_EQ(snapshot[0].text.value().attributedString.getString(), "new");
}

TEST(RetainedSceneReuseTest, AParagraphReplacedByAnEmptyOneCarriesNoOldTextEither) {
    RetainedScene scene;

    EXPECT_TRUE(snapshotAfterReplacement(scene, makeParagraph(2, makeRect(0, 0, 200, 40), "the old paragraph"),
                                         makeParagraph(2, makeRect(0, 0, 200, 40), ""))
                    .empty());
    EXPECT_EQ(scene.dump().find("text="), std::string::npos);
}

TEST(RetainedSceneReuseTest, AnImageReplacedByTheSameTagCarriesNoOldSourceOrTint) {
    RetainedScene scene;
    const SceneSnapshot snapshot = snapshotAfterReplacement(scene, makeImage(2, makeRect(0, 0, 64, 48), "old.png",
                                                                            red()),
                                                            makeImage(2, makeRect(0, 0, 64, 48), "new.png", blue()));

    ASSERT_EQ(snapshot.size(), 1U);
    ASSERT_TRUE(snapshot[0].image.has_value());
    EXPECT_EQ(snapshot[0].image.value().uri, "new.png");
    EXPECT_EQ(snapshot[0].image.value().tintColorArgb, kBlueArgb);
}

TEST(RetainedSceneReuseTest, AnImageReplacedByAnUnrequestedSourceCarriesNoOldSourceEither) {
    RetainedScene scene;

    EXPECT_TRUE(snapshotAfterReplacement(scene, makeImage(2, makeRect(0, 0, 64, 48), "old.png", red()),
                                         makeImage(2, makeRect(0, 0, 64, 48), "", SharedColor{}))
                    .empty());
}

/**
 * The defect this test proves: `RetainedScene::deleteNode` erased `editorStates_[tag]` but left `focusedTag_`
 * pointing at a tag that no longer names any node, so a later `createNode` for the same tag inherited a focus
 * ring it never earned. Fixed in `RetainedScene::deleteNode` by clearing `focusedTag_`/`isFocusVisible_` when the
 * deleted tag was the focused one — see the comment there.
 */
TEST(RetainedSceneReuseTest, AFocusedNodeReplacedByTheSameTagDoesNotInheritTheFocusRing) {
    RetainedScene scene;
    const ShadowView oldView = mountFocusedView(scene);
    const SceneSnapshot beforeReplacement = scene.snapshot();

    ASSERT_EQ(beforeReplacement.size(), 1U);
    ASSERT_TRUE(beforeReplacement[0].focusRing);

    replaceWithSameTag(scene, kSurfaceTag, oldView, makePaintedView(2, makeRect(10, 20, 200, 100), blue()));

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_FALSE(snapshot[0].focusRing);
}

TEST(RetainedSceneReuseTest, AFocusChangeAfterTheDeleteStillMarksTheNewNode) {
    RetainedScene scene;
    const ShadowView oldView = mountFocusedView(scene);

    replaceWithSameTag(scene, kSurfaceTag, oldView, makePaintedView(2, makeRect(10, 20, 200, 100), blue()));
    scene.setFocus(2, true);

    const SceneSnapshot snapshot = scene.snapshot();

    ASSERT_EQ(snapshot.size(), 1U);
    EXPECT_TRUE(snapshot[0].focusRing);
}

TEST(RetainedSceneReuseTest, ADecoratedViewReplacedByAPlainOneCarriesNoOpacityBorderOrRadius) {
    RetainedScene scene;
    const SceneSnapshot snapshot =
        snapshotAfterReplacement(scene, makeStyledView(2, makeRect(0, 0, 100, 100), decoratedProps(red(), blue())),
                                 makePaintedView(2, makeRect(0, 0, 100, 100), blue()));

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

    replaceWithSameTag(mountingManager, kSurfaceTag, oldField, makeTextInput(2, fieldFrame, "", textInputProps()));

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
