#include "InputDispatcher.h"
#include "InputPipeline.h"
#include "ScrollController.h"
#include "ScrollPhysics.h"
#include "ShadowTreeTestSupport.h"
#include "TextInputComponent.h"

#include <LinuxMountingManager.h>
#include <folly/dynamic.h>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <react/renderer/components/scrollview/ScrollViewComponentDescriptor.h>
#include <react/renderer/components/scrollview/ScrollViewShadowNode.h>
#include <react/renderer/mounting/ShadowTree.h>

namespace {

using facebook::react::ComponentDescriptorParameters;
using facebook::react::ContextContainer;
using facebook::react::EventDispatcher;
using facebook::react::RootShadowNode;
using facebook::react::ScrollViewComponentDescriptor;
using facebook::react::ShadowNode;
using facebook::react::ShadowNodeFragment;
using facebook::react::ShadowTree;
using facebook::react::ShadowTreeCommitOptions;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::UIManager;
using facebook::react::ViewComponentDescriptor;
using react_native_linux::addRegisteredShadowTree;
using react_native_linux::InputDispatcher;
using react_native_linux::InputEvent;
using react_native_linux::InputEventKind;
using react_native_linux::InputModifiers;
using react_native_linux::keyboardScrollDestination;
using react_native_linux::KeyboardScrollIntent;
using react_native_linux::keyboardScrollIntent;
using react_native_linux::KeyboardScrollStep;
using react_native_linux::kKeyboardLineDistance;
using react_native_linux::kWheelNotchDistance;
using react_native_linux::LinuxMountingManager;
using react_native_linux::makeConfiguredShadowNode;
using react_native_linux::makeTaskDroppingUIManager;
using react_native_linux::maximumScrollOffset;
using react_native_linux::minimumScrollOffset;
using react_native_linux::PassThroughShadowTreeDelegate;
using react_native_linux::SceneCommand;
using react_native_linux::ScrollAxisBounds;
using react_native_linux::ScrollController;
using react_native_linux::TextInputComponentDescriptor;

using ChildList = std::vector<std::shared_ptr<const ShadowNode>>;

constexpr double kFrameMilliseconds60Hz = 1000.0 / 60.0;

/** The rig every arithmetic case reads: a 100-point window on 300 points of content, no inset. */
constexpr ScrollAxisBounds kWindowOnAPage{
    .contentLength = 300.0, .viewportLength = 100.0, .leadingInset = 0.0, .trailingInset = 0.0};

/** One viewport less one line of overlap, which is what a page key is worth on `kWindowOnAPage`. */
constexpr double kPage = 60.0;

InputEvent keyPress(std::string key, bool isShiftDown = false) {
    return InputEvent{
        .kind = InputEventKind::KeyPress, .key = std::move(key), .modifiers = InputModifiers{.shift = isShiftDown}};
}

double destinationOf(const std::string& key, double currentOffset, const ScrollAxisBounds& bounds) {
    const std::optional<KeyboardScrollIntent> intent = keyboardScrollIntent(key, false);

    EXPECT_TRUE(intent.has_value());

    return keyboardScrollDestination(intent.value_or(KeyboardScrollIntent{}), currentOffset, bounds);
}

#pragma mark - the key-to-step table (#441)

TEST(KeyboardScrollTableTest, ThePageKeysArePagesAndTheExtremeKeysAreExtremes) {
    EXPECT_EQ(keyboardScrollIntent("PageUp", false).value().step, KeyboardScrollStep::PageBackward);
    EXPECT_EQ(keyboardScrollIntent("PageDown", false).value().step, KeyboardScrollStep::PageForward);
    EXPECT_EQ(keyboardScrollIntent("Home", false).value().step, KeyboardScrollStep::ToStart);
    EXPECT_EQ(keyboardScrollIntent("End", false).value().step, KeyboardScrollStep::ToEnd);
}

TEST(KeyboardScrollTableTest, TheVerticalArrowsAreLinesOnTheVerticalAxis) {
    const KeyboardScrollIntent up = keyboardScrollIntent("ArrowUp", false).value();
    const KeyboardScrollIntent down = keyboardScrollIntent("ArrowDown", false).value();

    EXPECT_EQ(up.step, KeyboardScrollStep::LineBackward);
    EXPECT_FALSE(up.isHorizontal);
    EXPECT_EQ(down.step, KeyboardScrollStep::LineForward);
    EXPECT_FALSE(down.isHorizontal);
}

TEST(KeyboardScrollTableTest, TheHorizontalArrowsAreLinesOnTheHorizontalAxis) {
    const KeyboardScrollIntent left = keyboardScrollIntent("ArrowLeft", false).value();
    const KeyboardScrollIntent right = keyboardScrollIntent("ArrowRight", false).value();

    EXPECT_EQ(left.step, KeyboardScrollStep::LineBackward);
    EXPECT_TRUE(left.isHorizontal);
    EXPECT_EQ(right.step, KeyboardScrollStep::LineForward);
    EXPECT_TRUE(right.isHorizontal);
}

TEST(KeyboardScrollTableTest, SpaceIsPageDownAndShiftSpaceIsPageUp) {
    EXPECT_EQ(keyboardScrollIntent(" ", false).value().step, KeyboardScrollStep::PageForward);
    EXPECT_EQ(keyboardScrollIntent(" ", true).value().step, KeyboardScrollStep::PageBackward);
    EXPECT_FALSE(keyboardScrollIntent(" ", true).value().isHorizontal);
}

TEST(KeyboardScrollTableTest, AKeyThatIsNotInTheTableIsNotAScroll) {
    EXPECT_FALSE(keyboardScrollIntent("a", false).has_value());
    EXPECT_FALSE(keyboardScrollIntent("Tab", false).has_value());
    EXPECT_FALSE(keyboardScrollIntent("Enter", false).has_value());
    EXPECT_FALSE(keyboardScrollIntent("", false).has_value());
}

// Shift with anything but the space bar extends a selection everywhere on the desktop, so it is not a scroll here.
TEST(KeyboardScrollTableTest, ShiftWithAnythingButSpaceIsLeftForSelection) {
    EXPECT_FALSE(keyboardScrollIntent("PageDown", true).has_value());
    EXPECT_FALSE(keyboardScrollIntent("ArrowDown", true).has_value());
    EXPECT_FALSE(keyboardScrollIntent("Home", true).has_value());
    EXPECT_FALSE(keyboardScrollIntent("End", true).has_value());
}

#pragma mark - the step-to-distance arithmetic (#441)

// A page is one viewport minus the overlap, and the overlap is exactly one line: 100 - 40 = 60, and the pair of
// keys is symmetric, so paging down and back up again is where it started.
TEST(KeyboardScrollTableTest, APageIsOneViewportLessOneLineOfOverlap) {
    EXPECT_DOUBLE_EQ(destinationOf("PageDown", 0.0, kWindowOnAPage), kPage);
    EXPECT_DOUBLE_EQ(destinationOf("PageDown", kPage, kWindowOnAPage), kPage + kPage);
    EXPECT_DOUBLE_EQ(destinationOf("PageUp", kPage, kWindowOnAPage), 0.0);
}

// A viewport shorter than the overlap would otherwise page backwards. One line is the floor.
TEST(KeyboardScrollTableTest, APageIsNeverShorterThanOneLine) {
    const ScrollAxisBounds shortWindow{
        .contentLength = 300.0, .viewportLength = 30.0, .leadingInset = 0.0, .trailingInset = 0.0};

    EXPECT_DOUBLE_EQ(destinationOf("PageDown", 0.0, shortWindow), kKeyboardLineDistance);
}

// One arrow key travels exactly one wheel notch, so the keyboard and the wheel agree about what a step is.
TEST(KeyboardScrollTableTest, AnArrowKeyIsOneLineWhichIsOneWheelNotch) {
    EXPECT_DOUBLE_EQ(kKeyboardLineDistance, kWheelNotchDistance);
    EXPECT_DOUBLE_EQ(destinationOf("ArrowDown", 0.0, kWindowOnAPage), kKeyboardLineDistance);
    EXPECT_DOUBLE_EQ(destinationOf("ArrowUp", 100.0, kWindowOnAPage), 100.0 - kKeyboardLineDistance);
    EXPECT_DOUBLE_EQ(destinationOf("ArrowRight", 0.0, kWindowOnAPage), kKeyboardLineDistance);
    EXPECT_DOUBLE_EQ(destinationOf("ArrowLeft", 100.0, kWindowOnAPage), 100.0 - kKeyboardLineDistance);
}

// Home and End are the extremes the content inset defines rather than zero and the content height, so they land
// exactly where `scrollToEnd` and a scroll to the top land: -20 and 300 + 40 - 100.
TEST(KeyboardScrollTableTest, HomeAndEndAreTheInsetAdjustedContentExtremes) {
    const ScrollAxisBounds inset{
        .contentLength = 300.0, .viewportLength = 100.0, .leadingInset = 20.0, .trailingInset = 40.0};

    EXPECT_DOUBLE_EQ(destinationOf("Home", 150.0, inset), -20.0);
    EXPECT_DOUBLE_EQ(destinationOf("Home", 150.0, inset), minimumScrollOffset(inset));
    EXPECT_DOUBLE_EQ(destinationOf("End", 0.0, inset), 240.0);
    EXPECT_DOUBLE_EQ(destinationOf("End", 0.0, inset), maximumScrollOffset(inset));
}

TEST(KeyboardScrollTableTest, EveryStepIsClampedToTheScrollableRange) {
    EXPECT_DOUBLE_EQ(destinationOf("PageDown", 190.0, kWindowOnAPage), 200.0);
    EXPECT_DOUBLE_EQ(destinationOf("ArrowDown", 190.0, kWindowOnAPage), 200.0);
    EXPECT_DOUBLE_EQ(destinationOf("PageUp", 10.0, kWindowOnAPage), 0.0);
    EXPECT_DOUBLE_EQ(destinationOf("ArrowUp", 10.0, kWindowOnAPage), 0.0);
}

#pragma mark - the arbitration (#441)

/**
 * The arbitration rig: a real dispatcher over a committed tree shaped like an application — a spacer and a
 * container beside each other at the root, the container holding the one `<ScrollView>`, and inside it a
 * focusable row, a `<TextInput>` and enough filler to scroll both axes.
 *
 * What a key did is read from the mounting manager's command queue, because that is the whole of what this class
 * does about a scroll: it enqueues the ordinary `scrollTo` and `ScrollController` performs it. One test then
 * feeds that command to a real controller, which is where the offset and the event cadence are proved.
 */
class KeyboardScrollArbitrationTest : public ::testing::Test {
protected:
    static constexpr SurfaceId kSurfaceId = 1;
    static constexpr Tag kSpacerTag = 19;
    static constexpr Tag kContainerTag = 20;
    static constexpr Tag kScrollViewTag = 21;
    static constexpr Tag kRowTag = 22;
    static constexpr Tag kFieldTag = 23;
    static constexpr Tag kFillerTag = 24;

    void SetUp() override {
        uiManager_ = makeTaskDroppingUIManager(contextContainer_);
        shadowTree_ = addRegisteredShadowTree(*uiManager_, shadowTreeDelegate_, *contextContainer_, kSurfaceId);
    }

    void TearDown() override { react_native_linux::removeShadowTree(*uiManager_, kSurfaceId); }

    void commitScrollableTree() { commitRootChildren(ChildList{makeSpacer(), makeContainer()}); }

    /** The same application without a `<ScrollView>` anywhere in it: one focusable row and nothing else. */
    void commitTreeWithoutAScrollView() { commitRootChildren(ChildList{makeRow()}); }

    std::unique_ptr<InputDispatcher> makeDispatcher(SurfaceId surfaceId = kSurfaceId) {
        std::unique_ptr<InputDispatcher> dispatcher =
            std::make_unique<InputDispatcher>(uiManager_, mountingManager_, surfaceId);

        dispatcher->dispatch({});

        return dispatcher;
    }

    /** Focuses `tag` and drops whatever the reveal did, so the next command queue holds only the key's own work. */
    void focus(InputDispatcher& dispatcher, Tag tag) {
        dispatcher.dispatchCommands({SceneCommand{.tag = tag, .name = "focus", .args = folly::dynamic::object()}});
        mountingManager_->takeCommands();
    }

    std::vector<SceneCommand> pressKey(InputDispatcher& dispatcher, const std::string& key, bool isShiftDown = false) {
        dispatcher.dispatch({keyPress(key, isShiftDown)});

        return mountingManager_->takeCommands();
    }

    /** The single `scrollTo` a scroll key produced, as (x, y). */
    static std::pair<double, double> scrollToOffset(const std::vector<SceneCommand>& commands) {
        EXPECT_EQ(commands.size(), 1U);

        if (commands.size() != 1U) {
            return {-1.0, -1.0};
        }

        EXPECT_EQ(commands[0].name, "scrollTo");
        EXPECT_EQ(commands[0].tag, kScrollViewTag);

        return {commands[0].args[0].asDouble(), commands[0].args[1].asDouble()};
    }

    std::shared_ptr<UIManager> uiManager_;
    std::shared_ptr<LinuxMountingManager> mountingManager_{std::make_shared<LinuxMountingManager>()};
    ShadowTree* shadowTree_{nullptr};
    std::shared_ptr<const ContextContainer> contextContainer_{std::make_shared<ContextContainer>()};

private:
    void commitRootChildren(ChildList children) {
        const ShadowTreeCommitOptions commitOptions{.enableStateReconciliation = false, .mountSynchronously = true};

        shadowTree_->commit(
            [&children](const RootShadowNode& oldRootShadowNode) {
                return std::static_pointer_cast<RootShadowNode>(oldRootShadowNode.ShadowNode::clone(
                    ShadowNodeFragment{.props = ShadowNodeFragment::propsPlaceholder(),
                                       .children = std::make_shared<const ChildList>(std::move(children))}));
            },
            commitOptions);
    }

    std::shared_ptr<const ShadowNode> makeSpacer() {
        return makeConfiguredShadowNode(viewDescriptor_, kSpacerTag, kSurfaceId, contextContainer_,
                                        folly::dynamic::object("width", 10)("height", 10),
                                        std::make_shared<const ChildList>());
    }

    std::shared_ptr<const ShadowNode> makeContainer() {
        return makeConfiguredShadowNode(viewDescriptor_, kContainerTag, kSurfaceId, contextContainer_,
                                        folly::dynamic::object("width", 100)("height", 100),
                                        std::make_shared<const ChildList>(ChildList{makeScrollView()}));
    }

    std::shared_ptr<const ShadowNode> makeScrollView() {
        ChildList children{makeRow(), makeField(), makeFiller()};

        return makeConfiguredShadowNode(scrollViewDescriptor_, kScrollViewTag, kSurfaceId, contextContainer_,
                                        folly::dynamic::object("width", 100)("height", 100),
                                        std::make_shared<const ChildList>(std::move(children)));
    }

    std::shared_ptr<const ShadowNode> makeRow() {
        return makeConfiguredShadowNode(viewDescriptor_, kRowTag, kSurfaceId, contextContainer_,
                                        folly::dynamic::object("width", 100)("height", 50)("accessible", true),
                                        std::make_shared<const ChildList>());
    }

    std::shared_ptr<const ShadowNode> makeField() {
        return makeConfiguredShadowNode(textInputDescriptor_, kFieldTag, kSurfaceId, contextContainer_,
                                        folly::dynamic::object("width", 100)("height", 50)("accessible", true),
                                        std::make_shared<const ChildList>());
    }

    std::shared_ptr<const ShadowNode> makeFiller() {
        return makeConfiguredShadowNode(viewDescriptor_, kFillerTag, kSurfaceId, contextContainer_,
                                        folly::dynamic::object("width", 300)("height", 200),
                                        std::make_shared<const ChildList>());
    }

    ComponentDescriptorParameters descriptorParameters() const {
        return ComponentDescriptorParameters{
            .eventDispatcher = EventDispatcher::Shared{}, .contextContainer = contextContainer_, .flavor = nullptr};
    }

    PassThroughShadowTreeDelegate shadowTreeDelegate_;
    ViewComponentDescriptor viewDescriptor_ = makeViewComponentDescriptor(contextContainer_);
    ScrollViewComponentDescriptor scrollViewDescriptor_{descriptorParameters()};
    TextInputComponentDescriptor textInputDescriptor_{descriptorParameters()};
};

// With nothing focused there is no nearest anything, and a page of unfocusable text is exactly the case a mouse
// is otherwise required for, so the key takes the surface's outermost `<ScrollView>` — found past a sibling that
// is not one and through the container that holds it.
TEST_F(KeyboardScrollArbitrationTest, PageDownWithNothingFocusedScrollsTheOutermostScrollView) {
    commitScrollableTree();
    const std::unique_ptr<InputDispatcher> dispatcher = makeDispatcher();

    const auto [x, y] = scrollToOffset(pressKey(*dispatcher, "PageDown"));

    EXPECT_DOUBLE_EQ(x, 0.0);
    EXPECT_DOUBLE_EQ(y, kPage);
}

TEST_F(KeyboardScrollArbitrationTest, PageDownWithANonEditableRowFocusedScrollsTheEnclosingScrollView) {
    commitScrollableTree();
    const std::unique_ptr<InputDispatcher> dispatcher = makeDispatcher();

    focus(*dispatcher, kRowTag);

    const auto [x, y] = scrollToOffset(pressKey(*dispatcher, "PageDown"));

    EXPECT_DOUBLE_EQ(x, 0.0);
    EXPECT_DOUBLE_EQ(y, kPage);
}

// The losing side of the arbitration. The field's own editor consumes Home and the horizontal arrows already;
// Page Down and the vertical arrows it answers `Ignored` for are refused here too, because a field that swallowed
// Home but let Page Down scroll the list out from under its caret would be worse than either half alone.
TEST_F(KeyboardScrollArbitrationTest, AFocusedTextInputRefusesEveryScrollKey) {
    commitScrollableTree();
    const std::unique_ptr<InputDispatcher> dispatcher = makeDispatcher();

    focus(*dispatcher, kFieldTag);

    EXPECT_TRUE(pressKey(*dispatcher, "PageDown").empty());
    EXPECT_TRUE(pressKey(*dispatcher, "PageUp").empty());
    EXPECT_TRUE(pressKey(*dispatcher, "Home").empty());
    EXPECT_TRUE(pressKey(*dispatcher, "End").empty());
    EXPECT_TRUE(pressKey(*dispatcher, "ArrowDown").empty());
    EXPECT_TRUE(pressKey(*dispatcher, " ").empty());
}

// Space activates the focused node before it pages anything, which is why it types a space in a field and clicks
// a `<Pressable>`. Page Down is the key that always scrolls; space only reaches the list when nothing is focused.
TEST_F(KeyboardScrollArbitrationTest, SpaceActivatesTheFocusedNodeInsteadOfPagingTheList) {
    commitScrollableTree();
    const std::unique_ptr<InputDispatcher> dispatcher = makeDispatcher();

    focus(*dispatcher, kRowTag);

    EXPECT_TRUE(pressKey(*dispatcher, " ").empty());
    EXPECT_TRUE(pressKey(*dispatcher, " ", true).empty());
}

TEST_F(KeyboardScrollArbitrationTest, SpaceWithNothingFocusedPagesTheListAndShiftSpacePagesItBack) {
    commitScrollableTree();
    const std::unique_ptr<InputDispatcher> dispatcher = makeDispatcher();

    EXPECT_DOUBLE_EQ(scrollToOffset(pressKey(*dispatcher, " ")).second, kPage);

    // Back up from a content offset still at zero, because nothing has published the page down yet: the shift is
    // what is under test, and it asks for the top rather than for the same place the plain space asked for.
    EXPECT_DOUBLE_EQ(scrollToOffset(pressKey(*dispatcher, " ", true)).second, 0.0);
}

TEST_F(KeyboardScrollArbitrationTest, AKeyThatIsNotAScrollKeyScrollsNothing) {
    commitScrollableTree();
    const std::unique_ptr<InputDispatcher> dispatcher = makeDispatcher();

    focus(*dispatcher, kRowTag);

    EXPECT_TRUE(pressKey(*dispatcher, "a").empty());
    EXPECT_TRUE(pressKey(*dispatcher, "PageDown", true).empty());
}

TEST_F(KeyboardScrollArbitrationTest, AnApplicationWithNoScrollViewInItScrollsNothing) {
    commitTreeWithoutAScrollView();
    const std::unique_ptr<InputDispatcher> dispatcher = makeDispatcher();

    EXPECT_TRUE(pressKey(*dispatcher, "PageDown").empty());

    focus(*dispatcher, kRowTag);

    EXPECT_TRUE(pressKey(*dispatcher, "PageDown").empty());
}

TEST_F(KeyboardScrollArbitrationTest, AKeyOnASurfaceThatHasNoTreeScrollsNothing) {
    commitScrollableTree();
    const std::unique_ptr<InputDispatcher> dispatcher = makeDispatcher(kSurfaceId + 1);

    EXPECT_TRUE(pressKey(*dispatcher, "PageDown").empty());
}

// The horizontal axis is the same arithmetic against the other pair of bounds, and the command carries the axis
// that did not move unchanged rather than dropping it.
TEST_F(KeyboardScrollArbitrationTest, TheHorizontalArrowsMoveTheHorizontalAxisOnly) {
    commitScrollableTree();
    const std::unique_ptr<InputDispatcher> dispatcher = makeDispatcher();

    const auto [x, y] = scrollToOffset(pressKey(*dispatcher, "ArrowRight"));

    EXPECT_DOUBLE_EQ(x, kKeyboardLineDistance);
    EXPECT_DOUBLE_EQ(y, 0.0);

    const auto [backX, backY] = scrollToOffset(pressKey(*dispatcher, "ArrowLeft"));

    EXPECT_DOUBLE_EQ(backX, 0.0);
    EXPECT_DOUBLE_EQ(backY, 0.0);
}

// The cadence half of #441: a keyboard scroll is the unanimated `scrollTo` `ScrollController` already performs,
// so `VirtualizedList` windowing sees one `onScroll` at the new offset and no momentum bracket after it — the
// same thing a programmatic scroll produces, rather than a third motion model.
TEST_F(KeyboardScrollArbitrationTest, AKeyboardScrollReachesTheControllerAsOneJumpWithNoMomentum) {
    commitScrollableTree();
    const std::unique_ptr<InputDispatcher> dispatcher = makeDispatcher();
    ScrollController controller(uiManager_, kSurfaceId);

    controller.dispatchCommands(pressKey(*dispatcher, "PageDown"));
    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_TRUE(controller.hasDispatchedScrollEvent());
    EXPECT_FALSE(controller.isScrollActive());

    // Where it landed, read the way ScrollTest reads a landing: a `scrollTo` to the offset the content already
    // rests at is no work at all, and one to any other offset is.
    controller.dispatchCommands(
        {SceneCommand{.tag = kScrollViewTag, .name = "scrollTo", .args = folly::dynamic::array(0, kPage, false)}});
    controller.advance(kFrameMilliseconds60Hz);

    EXPECT_FALSE(controller.hasDispatchedScrollEvent());
}

} // namespace
