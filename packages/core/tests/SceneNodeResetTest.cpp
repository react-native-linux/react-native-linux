#include "RetainedScene.h"
#include "SceneTestSupport.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <set>
#include <string>
#include <string_view>
#include <vector>
#include <yoga/enums/Edge.h>
#include <yoga/style/StyleLength.h>

#include <react/renderer/components/rncore/Props.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/Transform.h>
#include <react/renderer/graphics/ValueUnit.h>
#include <react/renderer/mounting/ShadowView.h>

// Issue #420 asked for iOS's component-view recycle pool to be pinned: a strictly balanced dequeue/enqueue that
// throws on imbalance, and a generated `prepareForRecycle` test per registered component. We have no view pool —
// there is no view object to hand back, only `SceneNode` entries in `RetainedScene::nodes_` keyed by Fabric tag,
// and the balance upstream enforces is `LinuxMountingManager`'s create/delete ordering, already pinned by
// `MountingRegistryConformanceTest.cpp` (#419). What does carry over is the hazard underneath the 17
// `prepareForRecycle` implementations: a field that outlives the node it described.
//
// `SceneReuseTest.cpp` (#107) pins that hazard per component by hand. This file pins the structural half #420
// asked for and #107 did not give: `RetainedScene::writeNode` rewrites a `SceneNode` **in place**
// (`nodes_[shadowView.tag]`), so every field it does not assign is a field remembered from whatever occupied the
// tag before. The two tests below are the generated form of that contract — one rewrites every component shape
// we mount with a bare `<View>` and asserts nothing of the old shape is left, the other reads `SceneNode`'s own
// declaration out of `RetainedScene.h` so a field added without a reset fails CI instead of leaking a value into
// the next node that reuses the tag.

namespace {

using facebook::react::ActivityIndicatorViewProps;
using facebook::react::ImportantForAccessibility;
using facebook::react::PointerEventsMode;
using facebook::react::Role;
using facebook::react::SwitchProps;
using react_native_linux::SceneNode;

namespace yoga = facebook::yoga;

constexpr Tag kNodeTag = 2;

/**
 * Every `ViewProps`-level field `readPaintProps` copies onto a node, set to something that is not its default, so
 * a rewrite that forgot any one of them is visible rather than accidentally equal to the reset value.
 */
void decorate(ViewProps& viewProps) {
    viewProps.backgroundColor = blue();
    viewProps.opacity = 0.25;
    viewProps.borderRadii.all = ValueUnit{16.0F, UnitType::Point};
    viewProps.borderColors.all = red();
    viewProps.yogaStyle.setBorder(yoga::Edge::All, yoga::StyleLength::points(3));
    viewProps.yogaStyle.setOverflow(yoga::Overflow::Hidden);
    viewProps.transform = Transform::Scale(2.0F, 3.0F, 1.0F);
    viewProps.transformOrigin.xy = {ValueUnit{10.0F, UnitType::Point}, ValueUnit{20.0F, UnitType::Point}};
    viewProps.pointerEvents = PointerEventsMode::BoxNone;
    viewProps.testId = "the-previous-occupant";
    viewProps.accessibilityLabel = "the previous occupant";
    viewProps.accessible = true;
    viewProps.accessibilityElementsHidden = true;
    viewProps.importantForAccessibility = ImportantForAccessibility::Yes;
    viewProps.role = Role::Button;
    viewProps.accessibilityRole = "button";
    viewProps.accessibilityHint = "press it";
    viewProps.nativeId = "previous-native-id";
}

std::shared_ptr<ViewProps> decoratedViewProps() {
    const std::shared_ptr<ViewProps> viewProps = std::make_shared<ViewProps>();

    decorate(*viewProps);

    return viewProps;
}

Rect loadedFrame() { return makeRect(0, 0, 100, 40); }

ShadowView decoratedView() { return makeStyledView(kNodeTag, loadedFrame(), decoratedViewProps()); }

ShadowView decoratedParagraph() {
    ShadowView shadowView = makeParagraph(kNodeTag, loadedFrame(), "text the next node must not inherit");

    shadowView.props = decoratedViewProps();

    return shadowView;
}

ShadowView decoratedImage() {
    ShadowView shadowView = makeImage(kNodeTag, loadedFrame(), "file:///previous.png", red());
    const std::shared_ptr<facebook::react::ImageProps> imageProps = std::make_shared<facebook::react::ImageProps>(
        *std::dynamic_pointer_cast<const facebook::react::ImageProps>(shadowView.props));

    decorate(*imageProps);
    shadowView.props = imageProps;

    return shadowView;
}

ShadowView decoratedScrollView() {
    ShadowView shadowView = makeScrollView(kNodeTag, loadedFrame(), Point{.x = 0, .y = 120}, makeRect(0, 0, 100, 900));

    shadowView.props = decoratedViewProps();

    return shadowView;
}

ShadowView decoratedTextInput() {
    const std::shared_ptr<react_native_linux::TextInputProps> props = textInputProps();

    decorate(*props);

    return makeTextInput(kNodeTag, loadedFrame(), "value the next node must not inherit", props);
}

ShadowView decoratedSwitch() {
    const std::shared_ptr<SwitchProps> switchProps = std::make_shared<SwitchProps>();

    decorate(*switchProps);
    switchProps->value = true;
    switchProps->disabled = true;

    ShadowView shadowView;

    shadowView.tag = kNodeTag;
    shadowView.componentName = "Switch";
    shadowView.layoutMetrics.frame = loadedFrame();
    shadowView.props = switchProps;

    return shadowView;
}

ShadowView decoratedActivityIndicator() {
    const std::shared_ptr<ActivityIndicatorViewProps> indicatorProps = std::make_shared<ActivityIndicatorViewProps>();

    decorate(*indicatorProps);
    indicatorProps->animating = true;
    indicatorProps->size = facebook::react::ActivityIndicatorViewSize::Large;

    ShadowView shadowView;

    shadowView.tag = kNodeTag;
    shadowView.componentName = "ActivityIndicator";
    shadowView.layoutMetrics.frame = loadedFrame();
    shadowView.props = indicatorProps;

    return shadowView;
}

ShadowView bareView() { return makeView(kNodeTag, makeRect(5, 5, 20, 10)); }

/**
 * The node a bare `<View>` produces when it is the first thing the tag ever held, which is what "reset" has to
 * mean: the expectation is generated by the same production path under test rather than restated here, so a
 * reader that starts assigning a field cannot make this test agree by accident.
 */
SceneNode nodeWrittenIntoAFreshScene() {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
    addChild(scene, kSurfaceTag, bareView());

    return scene.nodes().at(kNodeTag);
}

void expectResetTo(const SceneNode& node, const SceneNode& reference) {
    EXPECT_EQ(node.tag, reference.tag);
    EXPECT_EQ(node.parentTag, reference.parentTag);
    EXPECT_EQ(node.componentName, reference.componentName);
    EXPECT_EQ(node.testId, reference.testId);
    EXPECT_EQ(node.accessibilityLabel, reference.accessibilityLabel);
    EXPECT_EQ(node.accessibility.accessible, reference.accessibility.accessible);
    EXPECT_EQ(node.accessibility.elementsHidden, reference.accessibility.elementsHidden);
    EXPECT_EQ(node.accessibility.importantForAccessibility, reference.accessibility.importantForAccessibility);
    EXPECT_EQ(node.accessibility.role, reference.accessibility.role);
    EXPECT_EQ(node.accessibility.accessibilityRole, reference.accessibility.accessibilityRole);
    EXPECT_EQ(node.accessibility.hint, reference.accessibility.hint);
    EXPECT_EQ(node.accessibility.nativeId, reference.accessibility.nativeId);
    EXPECT_EQ(node.accessibility.labelledBy, reference.accessibility.labelledBy);
    EXPECT_EQ(node.accessibility.state.has_value(), reference.accessibility.state.has_value());
    EXPECT_EQ(node.childTags, reference.childTags);
    EXPECT_EQ(node.layoutMetrics, reference.layoutMetrics);
    EXPECT_EQ(node.backgroundColor.has_value(), reference.backgroundColor.has_value());
    EXPECT_EQ(node.backgroundImage.size(), reference.backgroundImage.size());
    EXPECT_EQ(node.shadows.size(), reference.shadows.size());
    EXPECT_EQ(node.borderMetrics, reference.borderMetrics);
    EXPECT_FLOAT_EQ(node.transform.scaleX, reference.transform.scaleX);
    EXPECT_FLOAT_EQ(node.transform.scaleY, reference.transform.scaleY);
    EXPECT_FLOAT_EQ(node.transform.skewX, reference.transform.skewX);
    EXPECT_FLOAT_EQ(node.transform.skewY, reference.transform.skewY);
    EXPECT_FLOAT_EQ(node.transform.translateX, reference.transform.translateX);
    EXPECT_FLOAT_EQ(node.transform.translateY, reference.transform.translateY);
    EXPECT_EQ(node.transformOrigin, reference.transformOrigin);
    EXPECT_FLOAT_EQ(node.opacity, reference.opacity);
    EXPECT_EQ(node.pointerEvents, reference.pointerEvents);
    EXPECT_EQ(node.clipsChildren, reference.clipsChildren);
    EXPECT_EQ(node.text.has_value(), reference.text.has_value());
    EXPECT_EQ(node.image.has_value(), reference.image.has_value());
    EXPECT_EQ(node.editor.has_value(), reference.editor.has_value());
    EXPECT_EQ(node.switchControl.has_value(), reference.switchControl.has_value());
    EXPECT_EQ(node.activityIndicator.has_value(), reference.activityIndicator.has_value());
    EXPECT_EQ(node.scrollContentOffset.has_value(), reference.scrollContentOffset.has_value());
    EXPECT_EQ(node.maintainedScroll.has_value(), reference.maintainedScroll.has_value());
}

// Every field `expectResetTo` compares, which is every field `SceneNode` declares. The oracle below holds this
// list against the header, so the comparison above cannot silently stop covering the struct.
const std::vector<std::string>& comparedSceneNodeFields() {
    static const std::vector<std::string> fields{"tag",
                                                 "parentTag",
                                                 "componentName",
                                                 "testId",
                                                 "accessibilityLabel",
                                                 "accessibility",
                                                 "childTags",
                                                 "layoutMetrics",
                                                 "backgroundColor",
                                                 "backgroundImage",
                                                 "shadows",
                                                 "borderMetrics",
                                                 "transform",
                                                 "transformOrigin",
                                                 "opacity",
                                                 "pointerEvents",
                                                 "clipsChildren",
                                                 "text",
                                                 "image",
                                                 "editor",
                                                 "switchControl",
                                                 "activityIndicator",
                                                 "scrollContentOffset",
                                                 "maintainedScroll"};

    return fields;
}

/**
 * The member names `struct SceneNode` declares, read out of the header this test compiles against. Only the
 * declaration lines matter: a member is a line inside the struct body that ends in `;` and is not part of a
 * comment, and its name is the last identifier before the initializer or the semicolon.
 */
std::vector<std::string> declaredSceneNodeFields() {
    const std::filesystem::path header =
        std::filesystem::path{__FILE__}.parent_path().parent_path() / "src" / "RetainedScene.h";
    std::ifstream stream{header};

    EXPECT_TRUE(stream.is_open()) << "cannot read " << header;

    std::vector<std::string> fields;
    std::string line;
    bool insideStruct = false;
    bool insideBlockComment = false;

    while (std::getline(stream, line)) {
        if (!insideStruct) {
            insideStruct = line == "struct SceneNode {";

            continue;
        }

        if (line == "};") {
            break;
        }

        if (insideBlockComment) {
            insideBlockComment = line.find("*/") == std::string::npos;

            continue;
        }

        const std::string_view trimmed{line.data() + std::min(line.find_first_not_of(' '), line.size())};

        if (trimmed.starts_with("/*")) {
            insideBlockComment = trimmed.find("*/") == std::string_view::npos;

            continue;
        }

        if (trimmed.starts_with("//") || !trimmed.ends_with(";")) {
            continue;
        }

        const size_t nameEnd = trimmed.find_first_of("{=;");
        const size_t nameStart = trimmed.find_last_of(' ', nameEnd) + 1;

        fields.emplace_back(trimmed.substr(nameStart, nameEnd - nameStart));
    }

    return fields;
}

} // namespace

namespace {

/**
 * The generated recycle test #420 asked for, in the shape our architecture has one: the "registry" is the set of
 * component shapes `writeNode` reads content off, and every one of them has to leave the tag as clean as if it
 * had never held anything.
 */
TEST(SceneNodeResetTest, RewritingATagWithABareViewLeavesNothingOfThePreviousComponentBehind) {
    const std::vector<std::pair<const char*, ShadowView>> previousOccupants{
        {"View", decoratedView()},
        {"Paragraph", decoratedParagraph()},
        {"Image", decoratedImage()},
        {"ScrollView", decoratedScrollView()},
        {"TextInput", decoratedTextInput()},
        {"Switch", decoratedSwitch()},
        {"ActivityIndicator", decoratedActivityIndicator()}};
    const SceneNode reference = nodeWrittenIntoAFreshScene();

    for (const auto& [name, occupant] : previousOccupants) {
        SCOPED_TRACE(name);

        RetainedScene scene;

        scene.createSurfaceRoot(kSurfaceTag, Size{.width = 800, .height = 600});
        addChild(scene, kSurfaceTag, occupant);
        scene.createNode(bareView());

        expectResetTo(scene.nodes().at(kNodeTag), reference);
    }
}

/**
 * The half of #420 that makes the test above survive the next component: `writeNode` rewrites a node in place, so
 * a `SceneNode` field no reader assigns is remembered from the tag's previous occupant. Adding one without
 * deciding how it resets fails here rather than as a wrong pixel somewhere else.
 */
TEST(SceneNodeResetTest, TheResetComparisonCoversEveryFieldSceneNodeDeclares) {
    const std::vector<std::string> declared = declaredSceneNodeFields();

    EXPECT_EQ(declared, comparedSceneNodeFields());
    EXPECT_EQ(std::set<std::string>(declared.begin(), declared.end()).size(), declared.size());
}

} // namespace
