#include "AutomationProtocol.h"

#include "SceneTestSupport.h"

#include <folly/json/dynamic.h>
#include <folly/json/json.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace {

using react_native_linux::AutomationCommand;
using react_native_linux::AutomationError;
using react_native_linux::AutomationErrorLog;
using react_native_linux::automationErrorLog;
using react_native_linux::AutomationRequestParse;
using react_native_linux::describeAccessibilityTree;
using react_native_linux::describeErrors;
using react_native_linux::describeVisualTree;
using react_native_linux::formatAutomationFailure;
using react_native_linux::formatAutomationResponse;
using react_native_linux::parseAutomationRequest;
using react_native_linux::reportNativeError;
using facebook::react::AccessibilityState;
using facebook::react::AccessibilityValue;
using facebook::react::Role;
using react_native_linux::SceneAccessibility;
using react_native_linux::SceneNode;
using react_native_linux::SceneNodes;

folly::dynamic parseLine(const std::string& line) { return folly::parseJson(line); }

std::shared_ptr<ViewProps> propsWithIdentity(const std::string& testId, const std::string& accessibilityLabel) {
    const std::shared_ptr<ViewProps> viewProps = std::make_shared<ViewProps>();

    viewProps->testId = testId;
    viewProps->accessibilityLabel = accessibilityLabel;

    return viewProps;
}

// The tree DumpVisualTree is asked about: a root, a labelled child view and a paragraph under it.
SceneNodes buildMountedTree() {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 400, .height = 300});
    scene.createNode(makeStyledView(2, makeRect(0, 0, 400, 300), propsWithIdentity("card", "Card")));
    scene.createNode(makeParagraph(3, makeRect(8, 8, 100, 20), "Hello"));
    scene.insertChild(kSurfaceTag, makeStyledView(2, makeRect(0, 0, 400, 300), propsWithIdentity("card", "Card")), 0);
    scene.insertChild(2, makeParagraph(3, makeRect(8, 8, 100, 20), "Hello"), 0);

    return scene.nodes();
}

TEST(AutomationProtocol, ParsesACommandThatTakesNoArgument) {
    const AutomationRequestParse parsed = parseAutomationRequest(R"({"command":"ListErrors"})");

    ASSERT_TRUE(parsed.request.has_value());
    EXPECT_EQ(parsed.request.value().command, AutomationCommand::ListErrors);
    EXPECT_TRUE(parsed.error.empty());
}

TEST(AutomationProtocol, ParsesMarkTestPassedAndTheTwoTreeDumps) {
    EXPECT_EQ(parseAutomationRequest(R"({"command":"MarkTestPassed"})").request.value().command,
              AutomationCommand::MarkTestPassed);
    EXPECT_EQ(parseAutomationRequest(R"({"command":"DumpVisualTree"})").request.value().command,
              AutomationCommand::DumpVisualTree);
    EXPECT_EQ(parseAutomationRequest(R"({"command":"DumpAccessibilityTree"})").request.value().command,
              AutomationCommand::DumpAccessibilityTree);
}

TEST(AutomationProtocol, ParsesTakeScreenshotWithItsPath) {
    const AutomationRequestParse parsed =
        parseAutomationRequest(R"({"command":"TakeScreenshot","path":"/tmp/shot.png"})");

    ASSERT_TRUE(parsed.request.has_value());
    EXPECT_EQ(parsed.request.value().command, AutomationCommand::TakeScreenshot);
    EXPECT_EQ(parsed.request.value().screenshotPath, "/tmp/shot.png");
}

TEST(AutomationProtocol, RejectsTakeScreenshotWithoutAUsablePath) {
    EXPECT_FALSE(parseAutomationRequest(R"({"command":"TakeScreenshot"})").request.has_value());
    EXPECT_FALSE(parseAutomationRequest(R"({"command":"TakeScreenshot","path":7})").request.has_value());
    EXPECT_EQ(parseAutomationRequest(R"({"command":"TakeScreenshot","path":""})").error,
              "TakeScreenshot needs a non-empty \"path\"");
}

TEST(AutomationProtocol, ParsesHangForTestingWithItsDuration) {
    const AutomationRequestParse parsed = parseAutomationRequest(R"({"command":"HangForTesting","milliseconds":250})");

    ASSERT_TRUE(parsed.request.has_value());
    EXPECT_EQ(parsed.request.value().command, AutomationCommand::HangForTesting);
    EXPECT_EQ(parsed.request.value().hangMilliseconds, 250);
}

TEST(AutomationProtocol, RejectsHangForTestingWithoutANonNegativeDuration) {
    EXPECT_FALSE(parseAutomationRequest(R"({"command":"HangForTesting"})").request.has_value());
    EXPECT_FALSE(parseAutomationRequest(R"({"command":"HangForTesting","milliseconds":"250"})").request.has_value());
    EXPECT_EQ(parseAutomationRequest(R"({"command":"HangForTesting","milliseconds":-1})").error,
              "HangForTesting needs a non-negative integer \"milliseconds\"");
}

TEST(AutomationProtocol, RejectsALineThatIsNotOneJsonObjectNamingAKnownCommand) {
    EXPECT_NE(parseAutomationRequest("not json").error.find("one JSON object per line"), std::string::npos);
    EXPECT_EQ(parseAutomationRequest("[1,2]").error, "a request has to be a JSON object");
    EXPECT_EQ(parseAutomationRequest("{}").error, "a request needs a \"command\" string");
    EXPECT_EQ(parseAutomationRequest(R"({"command":9})").error, "a request needs a \"command\" string");
    EXPECT_EQ(parseAutomationRequest(R"({"command":"Explode"})").error, "unknown command Explode");
}

TEST(AutomationProtocol, FormatsOneResponsePerLine) {
    const std::string line =
        formatAutomationResponse(AutomationCommand::MarkTestPassed, folly::dynamic::object("passed", true));

    ASSERT_TRUE(line.ends_with("\n"));
    EXPECT_EQ(line.find('\n'), line.size() - 1);

    const folly::dynamic response = parseLine(line);

    EXPECT_TRUE(response["ok"].asBool());
    EXPECT_EQ(response["command"].asString(), "MarkTestPassed");
    EXPECT_TRUE(response["result"]["passed"].asBool());
}

TEST(AutomationProtocol, NamesEveryCommandInItsResponse) {
    for (const std::string& name : {"DumpAccessibilityTree", "DumpVisualTree", "HangForTesting", "ListErrors",
                                    "MarkTestPassed", "TakeScreenshot"}) {
        const AutomationCommand command =
            parseAutomationRequest(R"({"command":")" + name + R"(","path":"p","milliseconds":0})")
                .request.value()
                .command;

        EXPECT_EQ(parseLine(formatAutomationResponse(command, folly::dynamic::object()))["command"].asString(), name);
    }
}

TEST(AutomationProtocol, FormatsAFailureAsOneLine) {
    const folly::dynamic response = parseLine(formatAutomationFailure("unknown command Explode"));

    EXPECT_FALSE(response["ok"].asBool());
    EXPECT_EQ(response["error"].asString(), "unknown command Explode");
}

TEST(AutomationProtocol, DescribesTheErrorsTheRuntimeReported) {
    const folly::dynamic described = describeErrors({AutomationError{.source = "javascript", .message = "boom"},
                                                     AutomationError{.source = "image", .message = "no codec"}});

    ASSERT_EQ(described["errors"].size(), 2U);
    EXPECT_EQ(described["errors"][0]["source"].asString(), "javascript");
    EXPECT_EQ(described["errors"][0]["message"].asString(), "boom");
    EXPECT_EQ(described["errors"][1]["source"].asString(), "image");
}

TEST(AutomationProtocol, DescribesNoErrorsAsAnEmptyList) { EXPECT_TRUE(describeErrors({})["errors"].empty()); }

TEST(AutomationProtocol, DescribesTheCommittedTreeWithTheIdentityPropsASnapshotAssertsOn) {
    const folly::dynamic tree = describeVisualTree(buildMountedTree());

    ASSERT_EQ(tree["roots"].size(), 1U);

    const folly::dynamic& root = tree["roots"][0];

    EXPECT_EQ(root["tag"].asInt(), kSurfaceTag);
    EXPECT_EQ(root["frame"]["width"].asDouble(), 400.0);
    EXPECT_EQ(root["frame"]["height"].asDouble(), 300.0);
    EXPECT_EQ(root.count("testID"), 0U);

    const folly::dynamic& card = root["children"][0];

    EXPECT_EQ(card["componentName"].asString(), "View");
    EXPECT_EQ(card["testID"].asString(), "card");
    EXPECT_EQ(card["accessibilityLabel"].asString(), "Card");
    EXPECT_EQ(card.count("text"), 0U);

    const folly::dynamic& paragraph = card["children"][0];

    EXPECT_EQ(paragraph["componentName"].asString(), "Paragraph");
    EXPECT_EQ(paragraph["text"].asString(), "Hello");
    EXPECT_EQ(paragraph["frame"]["x"].asDouble(), 8.0);
    EXPECT_EQ(paragraph["frame"]["y"].asDouble(), 8.0);
    EXPECT_EQ(paragraph.count("children"), 0U);
}

TEST(AutomationProtocol, DescribesAParagraphWithNoTextWithoutATextField) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 100, .height = 100});
    scene.createNode(makeParagraph(2, makeRect(0, 0, 10, 10), ""));
    scene.insertChild(kSurfaceTag, makeParagraph(2, makeRect(0, 0, 10, 10), ""), 0);

    EXPECT_EQ(describeVisualTree(scene.nodes())["roots"][0]["children"][0].count("text"), 0U);
}

TEST(AutomationProtocol, DescribesAnEmptySceneAsNoRoots) { EXPECT_TRUE(describeVisualTree({})["roots"].empty()); }

TEST(AutomationProtocol, MarksAChildTheSceneNoLongerHoldsRatherThanDroppingIt) {
    SceneNodes nodes = buildMountedTree();

    nodes.erase(3);

    const folly::dynamic tree = describeVisualTree(nodes);
    const folly::dynamic& paragraph = tree["roots"][0]["children"][0]["children"][0];

    EXPECT_EQ(paragraph["tag"].asInt(), 3);
    EXPECT_TRUE(paragraph["missing"].asBool());
}

TEST(AutomationProtocol, DumpsTheTreeThroughTheMountingManagersLock) {
    LinuxMountingManager mountingManager;

    mountingManager.startSurface(kSurfaceTag, Size{.width = 200, .height = 100});
    mountingManager.executeMount(
        kSurfaceTag, MountingTransaction(kSurfaceTag, 1,
                                         ShadowViewMutationList{ShadowViewMutation::CreateMutation(makeStyledView(
                                             2, makeRect(0, 0, 20, 10), propsWithIdentity("box", "Box")))},
                                         {}));

    const folly::dynamic described = describeVisualTree(mountingManager.visualTreeNodes());

    EXPECT_EQ(described["roots"].size(), 2U);
    EXPECT_EQ(described["roots"][0]["tag"].asInt(), kSurfaceTag);
    EXPECT_EQ(described["roots"][1]["testID"].asString(), "box");
}

TEST(AutomationProtocol, ForgetsTheIdentityPropsOfANodeThatMountsWithoutViewProps) {
    RetainedScene scene;

    scene.createNode(makeStyledView(2, makeRect(0, 0, 10, 10), propsWithIdentity("box", "Box")));
    scene.updateNode(makeView(2, makeRect(0, 0, 10, 10)));

    EXPECT_EQ(describeVisualTree(scene.nodes())["roots"][0].count("testID"), 0U);
}

// A parent under the surface root with one plainly accessible child under it, which is the shape every pruning
// and exposure rule is asked about: what happens to the parent, and where its child ends up when it does not
// survive the projection.
SceneNodes buildProjectionTree(SceneAccessibility accessibility, const std::string& accessibilityLabel) {
    SceneNodes nodes;

    nodes[kSurfaceTag] = SceneNode{.tag = kSurfaceTag, .parentTag = 0, .childTags = {2}};
    nodes[2] = SceneNode{.tag = 2,
                         .parentTag = kSurfaceTag,
                         .accessibilityLabel = accessibilityLabel,
                         .accessibility = std::move(accessibility),
                         .childTags = {3}};
    nodes[3] = SceneNode{.tag = 3,
                         .parentTag = 2,
                         .accessibilityLabel = "Child",
                         .accessibility = SceneAccessibility{.accessible = true}};

    return nodes;
}

std::vector<int> projectedTags(const folly::dynamic& projected) {
    std::vector<int> tags;

    for (const folly::dynamic& node : projected) {
        tags.push_back(static_cast<int>(node["tag"].asInt()));
    }

    return tags;
}

struct ExposureCase {
    std::string_view what;
    SceneAccessibility accessibility;
    std::string_view accessibilityLabel;
    std::vector<int> expectedTags;
};

TEST(AutomationProtocol, PrunesSkipsAndExposesEachNodeByItsAccessibilityProps) {
    const std::vector<ExposureCase> cases{
        {.what = "a plain box is skipped and its child takes its place",
         .accessibility = {},
         .accessibilityLabel = "",
         .expectedTags = {3}},
        {.what = "accessible makes a box an element of its own",
         .accessibility = SceneAccessibility{.accessible = true},
         .accessibilityLabel = "",
         .expectedTags = {2}},
        {.what = "a label alone is enough, which is rn-macos#428's labelled image",
         .accessibility = {},
         .accessibilityLabel = "Portrait",
         .expectedTags = {2}},
        {.what = "an authored accessibilityRole is enough",
         .accessibility = SceneAccessibility{.accessibilityRole = "button"},
         .accessibilityLabel = "",
         .expectedTags = {2}},
        {.what = "an ARIA role is enough",
         .accessibility = SceneAccessibility{.role = Role::Img},
         .accessibilityLabel = "",
         .expectedTags = {2}},
        {.what = "importantForAccessibility yes forces a plain box in",
         .accessibility =
             SceneAccessibility{.importantForAccessibility = facebook::react::ImportantForAccessibility::Yes},
         .accessibilityLabel = "",
         .expectedTags = {2}},
        {.what = "importantForAccessibility no drops the node but keeps its subtree",
         .accessibility = SceneAccessibility{.accessible = true,
                                             .importantForAccessibility =
                                                 facebook::react::ImportantForAccessibility::No},
         .accessibilityLabel = "Ignored",
         .expectedTags = {3}},
        {.what = "importantForAccessibility no-hide-descendants drops the subtree",
         .accessibility = SceneAccessibility{.accessible = true,
                                             .importantForAccessibility =
                                                 facebook::react::ImportantForAccessibility::NoHideDescendants},
         .accessibilityLabel = "",
         .expectedTags = {}},
        {.what = "accessibilityElementsHidden drops the subtree too",
         .accessibility = SceneAccessibility{.accessible = true, .elementsHidden = true},
         .accessibilityLabel = "",
         .expectedTags = {}},
    };

    for (const ExposureCase& exposure : cases) {
        const folly::dynamic projected =
            describeAccessibilityTree(buildProjectionTree(exposure.accessibility,
                                                          std::string(exposure.accessibilityLabel)))["nodes"];

        EXPECT_EQ(projectedTags(projected), exposure.expectedTags) << exposure.what;
    }
}

TEST(AutomationProtocol, KeepsAnExposedNodesChildrenUnderIt) {
    const folly::dynamic projected =
        describeAccessibilityTree(buildProjectionTree(SceneAccessibility{.accessible = true}, ""))["nodes"];

    ASSERT_EQ(projected.size(), 1U);
    EXPECT_EQ(projected[0]["role"].asString(), "none");
    EXPECT_EQ(projected[0].count("name"), 0U);
    ASSERT_EQ(projected[0]["children"].size(), 1U);
    EXPECT_EQ(projected[0]["children"][0]["tag"].asInt(), 3);
    EXPECT_EQ(projected[0]["children"][0]["name"].asString(), "Child");
    EXPECT_EQ(projected[0]["children"][0].count("children"), 0U);
}

struct RoleCase {
    SceneAccessibility accessibility;
    std::string_view expectedRole;
};

TEST(AutomationProtocol, ResolvesTheRoleFromTheAuthoredNameThenTheAriaRole) {
    const std::vector<RoleCase> cases{
        {.accessibility = SceneAccessibility{.accessible = true}, .expectedRole = "none"},
        {.accessibility = SceneAccessibility{.accessibilityRole = "adjustable"}, .expectedRole = "adjustable"},
        {.accessibility = SceneAccessibility{.role = Role::Checkbox}, .expectedRole = "checkbox"},
        {.accessibility = SceneAccessibility{.role = Role::Heading}, .expectedRole = "heading"},
        {.accessibility = SceneAccessibility{.role = Role::Slider, .accessibilityRole = "adjustable"},
         .expectedRole = "adjustable"},
    };

    for (const RoleCase& role : cases) {
        const folly::dynamic projected = describeAccessibilityTree(buildProjectionTree(role.accessibility, ""))["nodes"];

        ASSERT_EQ(projected.size(), 1U);
        EXPECT_EQ(projected[0]["role"].asString(), role.expectedRole);
    }
}

TEST(AutomationProtocol, ExposesAParagraphAsTextNamedByWhatItLaidOut) {
    RetainedScene scene;

    scene.createSurfaceRoot(kSurfaceTag, Size{.width = 100, .height = 100});
    scene.createNode(makeParagraph(2, makeRect(0, 0, 100, 20), "Automation"));
    scene.createNode(makeParagraph(3, makeRect(0, 20, 100, 20), ""));
    scene.insertChild(kSurfaceTag, makeParagraph(2, makeRect(0, 0, 100, 20), "Automation"), 0);
    scene.insertChild(kSurfaceTag, makeParagraph(3, makeRect(0, 20, 100, 20), ""), 1);

    const folly::dynamic projected = describeAccessibilityTree(scene.nodes())["nodes"];

    ASSERT_EQ(projected.size(), 1U);
    EXPECT_EQ(projected[0]["tag"].asInt(), 2);
    EXPECT_EQ(projected[0]["role"].asString(), "text");
    EXPECT_EQ(projected[0]["name"].asString(), "Automation");
}

TEST(AutomationProtocol, PrefersTheLabelOverTheParagraphTextAsTheName) {
    RetainedScene scene;

    scene.createNode(makeParagraph(2, makeRect(0, 0, 100, 20), "Painted"));

    SceneNodes nodes = scene.nodes();

    nodes[2].accessibilityLabel = "Spoken instead";

    EXPECT_EQ(describeAccessibilityTree(nodes)["nodes"][0]["name"].asString(), "Spoken instead");
}

TEST(AutomationProtocol, ReportsTheHintTheValueAndTheStateOnlyWhenTheyCarrySomething) {
    const SceneAccessibility bare = SceneAccessibility{.accessible = true, .state = AccessibilityState{}};
    const folly::dynamic plain = describeAccessibilityTree(buildProjectionTree(bare, ""))["nodes"][0];

    EXPECT_EQ(plain.count("hint"), 0U);
    EXPECT_EQ(plain.count("value"), 0U);
    EXPECT_EQ(plain.count("state"), 0U);

    const SceneAccessibility filled = SceneAccessibility{
        .accessibilityRole = "adjustable",
        .hint = "Swipe to change",
        .state = AccessibilityState{.disabled = true, .selected = true, .busy = true, .expanded = false},
        .value = AccessibilityValue{.min = 0, .max = 10, .now = 4, .text = "four"}};
    const folly::dynamic described = describeAccessibilityTree(buildProjectionTree(filled, ""))["nodes"][0];

    EXPECT_EQ(described["hint"].asString(), "Swipe to change");
    EXPECT_EQ(described["value"]["min"].asInt(), 0);
    EXPECT_EQ(described["value"]["max"].asInt(), 10);
    EXPECT_EQ(described["value"]["now"].asInt(), 4);
    EXPECT_EQ(described["value"]["text"].asString(), "four");
    EXPECT_TRUE(described["state"]["busy"].asBool());
    EXPECT_TRUE(described["state"]["disabled"].asBool());
    EXPECT_TRUE(described["state"]["selected"].asBool());
    EXPECT_FALSE(described["state"]["expanded"].asBool());
    EXPECT_EQ(described["state"].count("checked"), 0U);
}

TEST(AutomationProtocol, NamesEveryCheckedStateAndOmitsTheAbsentOne) {
    const std::vector<std::pair<AccessibilityState::CheckedState, std::string_view>> cases{
        {AccessibilityState::CheckedState::Unchecked, "unchecked"},
        {AccessibilityState::CheckedState::Checked, "checked"},
        {AccessibilityState::CheckedState::Mixed, "mixed"},
    };

    for (const auto& [checked, expectedName] : cases) {
        const SceneAccessibility accessibility =
            SceneAccessibility{.accessibilityRole = "checkbox", .state = AccessibilityState{.checked = checked}};
        const folly::dynamic described = describeAccessibilityTree(buildProjectionTree(accessibility, ""))["nodes"][0];

        EXPECT_EQ(described["state"]["checked"].asString(), expectedName);
    }
}

TEST(AutomationProtocol, ResolvesLabelledByToTheTagsCarryingThoseNativeIdsAndDropsTheRest) {
    SceneNodes nodes =
        buildProjectionTree(SceneAccessibility{.accessible = true, .labelledBy = {"caption", "nobody"}}, "");

    nodes[3].accessibility.nativeId = "caption";

    const folly::dynamic described = describeAccessibilityTree(nodes)["nodes"][0];

    ASSERT_EQ(described["labelledBy"].size(), 1U);
    EXPECT_EQ(described["labelledBy"][0].asInt(), 3);
}

TEST(AutomationProtocol, ResolvesAReusedNativeIdToItsLowestTagOnEveryRun) {
    SceneNodes nodes = buildProjectionTree(SceneAccessibility{.accessible = true, .labelledBy = {"caption"}}, "");

    nodes[3].accessibility.nativeId = "caption";
    nodes[kSurfaceTag].accessibility.nativeId = "caption";

    EXPECT_EQ(describeAccessibilityTree(nodes)["nodes"][0]["labelledBy"][0].asInt(), kSurfaceTag);
}

TEST(AutomationProtocol, DropsAChildTheSceneNoLongerHoldsInsteadOfMarkingIt) {
    SceneNodes nodes = buildProjectionTree(SceneAccessibility{.accessible = true}, "");

    nodes.erase(3);

    EXPECT_EQ(describeAccessibilityTree(nodes)["nodes"][0].count("children"), 0U);
}

TEST(AutomationProtocol, ProjectsAnEmptySceneAsNoNodes) {
    EXPECT_TRUE(describeAccessibilityTree({})["nodes"].empty());
}

TEST(AutomationProtocol, ReadsTheAccessibilityPropsOffTheMountedShadowView) {
    const std::shared_ptr<ViewProps> viewProps = std::make_shared<ViewProps>();

    viewProps->accessible = true;
    viewProps->accessibilityRole = "button";
    viewProps->accessibilityLabel = "Send";
    viewProps->accessibilityHint = "Sends the message";
    viewProps->accessibilityState = AccessibilityState{.selected = true};

    RetainedScene scene;

    scene.createNode(makeStyledView(2, makeRect(0, 0, 10, 10), viewProps));

    const folly::dynamic described = describeAccessibilityTree(scene.nodes())["nodes"][0];

    EXPECT_EQ(described["role"].asString(), "button");
    EXPECT_EQ(described["name"].asString(), "Send");
    EXPECT_EQ(described["hint"].asString(), "Sends the message");
    EXPECT_TRUE(described["state"]["selected"].asBool());

    scene.updateNode(makeView(2, makeRect(0, 0, 10, 10)));

    EXPECT_TRUE(describeAccessibilityTree(scene.nodes())["nodes"].empty());
}

TEST(AutomationProtocol, KeepsTheOrderTheErrorsWereReportedIn) {
    AutomationErrorLog log;

    EXPECT_TRUE(log.list().empty());

    log.record("javascript", "first");
    log.record("image", "second");

    const std::vector<AutomationError> listed = log.list();

    ASSERT_EQ(listed.size(), 2U);
    EXPECT_EQ(listed[0].message, "first");
    EXPECT_EQ(listed[1].source, "image");
}

TEST(AutomationProtocol, RecordsANativeFaultInTheProcessWideLog) {
    const size_t before = automationErrorLog().list().size();

    reportNativeError("text", "fontFamily \"Nope\" is not registered");

    const std::vector<AutomationError> listed = automationErrorLog().list();

    ASSERT_EQ(listed.size(), before + 1);
    EXPECT_EQ(listed.back().source, "text");
    EXPECT_EQ(listed.back().message, "fontFamily \"Nope\" is not registered");
}

} // namespace
