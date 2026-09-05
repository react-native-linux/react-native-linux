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
using react_native_linux::describeErrors;
using react_native_linux::describeVisualTree;
using react_native_linux::formatAutomationFailure;
using react_native_linux::formatAutomationResponse;
using react_native_linux::parseAutomationRequest;
using react_native_linux::reportNativeError;
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

TEST(AutomationProtocol, ParsesMarkTestPassedAndDumpVisualTree) {
    EXPECT_EQ(parseAutomationRequest(R"({"command":"MarkTestPassed"})").request.value().command,
              AutomationCommand::MarkTestPassed);
    EXPECT_EQ(parseAutomationRequest(R"({"command":"DumpVisualTree"})").request.value().command,
              AutomationCommand::DumpVisualTree);
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
    for (const std::string& name :
         {"DumpVisualTree", "HangForTesting", "ListErrors", "MarkTestPassed", "TakeScreenshot"}) {
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
