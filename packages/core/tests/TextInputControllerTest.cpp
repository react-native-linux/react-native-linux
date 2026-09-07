#include "TextInputController.h"

#include "RecordingEventDispatcher.h"
#include "ShadowTreeTestSupport.h"
#include "TextInputComponent.h"

#include <LinuxMountingManager.h>
#include <algorithm>
#include <folly/dynamic.h>
#include <gtest/gtest.h>
#include <memory>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/renderer/core/State.h>
#include <react/renderer/mounting/ShadowTree.h>
#include <vector>

namespace {

using facebook::react::ComponentDescriptorParameters;
using facebook::react::ContextContainer;
using facebook::react::LayoutConstraints;
using facebook::react::LayoutContext;
using facebook::react::PropsParserContext;
using facebook::react::RootShadowNode;
using facebook::react::ShadowNode;
using facebook::react::ShadowNodeFamily;
using facebook::react::ShadowNodeFragment;
using facebook::react::ShadowTree;
using facebook::react::ShadowTreeCommitOptions;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::UIManager;
using react_native_linux::InputEvent;
using react_native_linux::InputEventKind;
using react_native_linux::InputModifiers;
using react_native_linux::isScrollableField;
using react_native_linux::LinuxMountingManager;
using react_native_linux::makeConfiguredShadowNode;
using react_native_linux::makeRecordingEventDispatcher;
using react_native_linux::makeTaskDroppingUIManager;
using react_native_linux::PassThroughShadowTreeDelegate;
using react_native_linux::removeShadowTree;
using react_native_linux::TextInputComponentDescriptor;
using react_native_linux::TextInputController;
using react_native_linux::TextInputKeyResult;
using react_native_linux::TextInputProps;
using react_native_linux::TextInputShadowNode;

constexpr SurfaceId kSurfaceId = 1;
constexpr Tag kFieldTag = 20;
constexpr facebook::react::Point kPointInsideField{.x = 20, .y = 20};
constexpr char kSelectionChangeEvent[] = "topSelectionChange";
constexpr char kChangeEvent[] = "topChange";

using ChildList = std::vector<std::shared_ptr<const ShadowNode>>;

/**
 * The key-routing decision table of issue #54, driven through the real `TextInputController` against a real
 * committed shadow tree: one focused single-line or multiline field, the controller wired to a default-constructed
 * mounting manager, and emissions dropping on the empty dispatcher. The observable is the routing result itself -
 * `Consumed`, `ConsumedAndBlurred`, `Ignored` - which is the contract: who gets the key, and whether the field
 * blurs with it.
 */
class TextInputControllerTest : public ::testing::Test {
protected:
    void SetUp() override {
        uiManager_ = makeTaskDroppingUIManager(contextContainer_);
        mountingManager_ = std::make_shared<LinuxMountingManager>();
        shadowTree_ = addRegisteredShadowTree(*uiManager_, shadowTreeDelegate_, *contextContainer_, kSurfaceId);
    }

    void TearDown() override { removeShadowTree(*uiManager_, kSurfaceId); }

    TextInputController makeController() { return TextInputController(uiManager_, mountingManager_, kSurfaceId); }

    void commitTextInput(folly::dynamic extraProps) {
        extraProps["width"] = 200;
        extraProps["height"] = 40;

        const ShadowTreeCommitOptions commitOptions{.enableStateReconciliation = false, .mountSynchronously = true};

        shadowTree_->commit(
            [this, extraProps](const RootShadowNode& oldRootShadowNode) {
                return std::static_pointer_cast<RootShadowNode>(oldRootShadowNode.ShadowNode::clone(ShadowNodeFragment{
                    .props = ShadowNodeFragment::propsPlaceholder(),
                    .children = std::make_shared<const ChildList>(ChildList{makeField(std::move(extraProps))})}));
            },
            commitOptions);

        mountedField_ = newestFieldNode();

        controller_ = std::make_unique<TextInputController>(uiManager_, mountingManager_, kSurfaceId);
        controller_->setMountedFields({mountedField_});
        controller_->setFocusedNode(mountedField_);
    }

    std::shared_ptr<const TextInputShadowNode> newestFieldNode() {
        std::shared_ptr<const ShadowNode> fieldNode;

        uiManager_->getShadowTreeRegistry().visit(kSurfaceId, [&fieldNode](const ShadowTree& tree) {
            const std::shared_ptr<const RootShadowNode> root = tree.getCurrentRevision().rootShadowNode;

            for (const std::shared_ptr<const ShadowNode>& child : root->getChildren()) {
                if (child->getTag() == kFieldTag) {
                    fieldNode = child;
                }
            }
        });

        return std::dynamic_pointer_cast<const TextInputShadowNode>(fieldNode);
    }

    static InputEvent key(const std::string& key, const InputModifiers& modifiers = {}) {
        return InputEvent{.kind = InputEventKind::KeyPress, .key = key, .modifiers = modifiers};
    }

    static InputEvent pointer(InputEventKind kind, facebook::react::Point surfacePoint) {
        return InputEvent{.kind = kind, .surfacePoint = surfacePoint};
    }

    void clickInsideField() {
        controller_->handlePointer(pointer(InputEventKind::PointerButtonPress, kPointInsideField));
        controller_->handlePointer(pointer(InputEventKind::PointerButtonRelease, kPointInsideField));
    }

    void type(const std::string& text) {
        for (const char character : text) {
            controller_->handleKey(key(std::string(1, character)));
        }
    }

    size_t countRecorded(const std::string& type) const {
        return static_cast<size_t>(std::count(recordedEventTypes_->begin(), recordedEventTypes_->end(), type));
    }

    std::unique_ptr<TextInputController> controller_;
    std::shared_ptr<const TextInputShadowNode> mountedField_;
    std::shared_ptr<std::vector<std::string>> recordedEventTypes_{std::make_shared<std::vector<std::string>>()};

private:
    std::shared_ptr<const ShadowNode> makeField(folly::dynamic props) {
        return makeConfiguredShadowNode(fieldDescriptor_, kFieldTag, kSurfaceId, contextContainer_, std::move(props),
                                        std::make_shared<const ChildList>());
    }

    PassThroughShadowTreeDelegate shadowTreeDelegate_;
    std::shared_ptr<const ContextContainer> contextContainer_{std::make_shared<ContextContainer>()};
    std::shared_ptr<const facebook::react::EventDispatcher> eventDispatcher_{
        makeRecordingEventDispatcher(recordedEventTypes_)};
    TextInputComponentDescriptor fieldDescriptor_{ComponentDescriptorParameters{
        .eventDispatcher = eventDispatcher_, .contextContainer = contextContainer_, .flavor = nullptr}};
    std::shared_ptr<UIManager> uiManager_;
    std::shared_ptr<LinuxMountingManager> mountingManager_;
    ShadowTree* shadowTree_;
};

TEST_F(TextInputControllerTest, EnterOnADefaultSingleLineFieldSubmitsAndBlurs) {
    commitTextInput(folly::dynamic::object());

    const auto result = controller_->handleKey(key("Enter"));

    EXPECT_EQ(result, TextInputKeyResult::ConsumedAndBlurred);
}

TEST_F(TextInputControllerTest, CoreIssue1082EnterWithSubmitBehaviorFiresAndNeverBlurs) {
    commitTextInput(folly::dynamic::object("submitBehavior", "submit"));

    const auto result = controller_->handleKey(key("Enter"));

    // Fires (Consumed, not Ignored) and does not blur - react-native-macos#1082's "swallowed entirely" is the
    // regression this row pins.
    EXPECT_EQ(result, TextInputKeyResult::Consumed);
}

TEST_F(TextInputControllerTest, EnterOnAMultilineFieldInsertsANewlineAndKeepsFocus) {
    commitTextInput(folly::dynamic::object("multiline", true));

    const auto result = controller_->handleKey(key("Enter"));

    EXPECT_EQ(result, TextInputKeyResult::Consumed);
}

TEST_F(TextInputControllerTest, EscapeBlurs) {
    commitTextInput(folly::dynamic::object());

    EXPECT_EQ(controller_->handleKey(key("Escape")), TextInputKeyResult::ConsumedAndBlurred);
}

TEST_F(TextInputControllerTest, TabIsNeverConsumedByTheField) {
    commitTextInput(folly::dynamic::object());

    // The focus model owns Tab in both single-line and multiline fields; the controller ignoring it is the
    // decision, so a field is never a keyboard dead end.
    EXPECT_EQ(controller_->handleKey(key("Tab")), TextInputKeyResult::Ignored);
}

TEST_F(TextInputControllerTest, TheShortcutKeysReachTheEditorAndCtrlZIsDeliberatelyUnconsumed) {
    commitTextInput(folly::dynamic::object());

    InputModifiers control;
    control.control = true;

    EXPECT_EQ(controller_->handleKey(key("a", control)), TextInputKeyResult::Consumed);
    EXPECT_EQ(controller_->handleKey(key("c", control)), TextInputKeyResult::Consumed);
    EXPECT_EQ(controller_->handleKey(key("v", control)), TextInputKeyResult::Consumed);
    EXPECT_EQ(controller_->handleKey(key("x", control)), TextInputKeyResult::Consumed);

    // No undo stack yet: Ctrl+Z is left for the application rather than swallowed into a no-op.
    EXPECT_EQ(controller_->handleKey(key("z", control)), TextInputKeyResult::Ignored);
}

/**
 * Issue #330's `text-input-clipboard-click` e2e, at unit scale and Skia-free: the field is reached by a pointer
 * press and release rather than by Tab, and Ctrl+A after that click still selects the whole value — the
 * `topSelectionChange` the scenario waits for is emitted, so pointer focus is not what the scenario was failing
 * on. `isSelectingByPointer_` is left false by the release, which is the state a following shortcut needs.
 */
TEST_F(TextInputControllerTest, SelectAllAfterPointerFocusEmitsASelectionChangeJustAsAfterTabFocus) {
    commitTextInput(folly::dynamic::object());
    clickInsideField();
    type("SerialLedger");
    controller_->synchronize();
    recordedEventTypes_->clear();

    InputModifiers control;
    control.control = true;

    EXPECT_EQ(controller_->handleKey(key("a", control)), TextInputKeyResult::Consumed);
    controller_->synchronize();

    EXPECT_EQ(countRecorded(kSelectionChangeEvent), 1U);
}

/**
 * Why that scenario failed in CI anyway, and the contract behind it: emissions are differences against the last
 * frame, computed in `synchronize`, so a selection that both appears and disappears between two frames is never a
 * difference and is never emitted. Ctrl+A followed by Ctrl+X inside one frame is exactly that — the cut collapses
 * the select-all's range before any frame has seen it — and only the collapsed selection reaches JavaScript.
 *
 * The sibling `text-input-editing` scenario passes the same idiom because its Ctrl+C does not move the selection,
 * so the select-all survives to the next frame. Coalescing is deliberate (twelve keystrokes are one `topChange`,
 * not twelve), so the scenario spaces the two shortcuts instead; this row is what stops that spacing from being
 * read as superstition later.
 */
TEST_F(TextInputControllerTest, ASelectionMadeAndCollapsedInsideOneFrameIsCoalescedAwayAndOnlyEmittedWhenAFrameSeesIt) {
    commitTextInput(folly::dynamic::object());
    clickInsideField();
    type("SerialLedger");
    controller_->synchronize();
    recordedEventTypes_->clear();

    InputModifiers control;
    control.control = true;

    controller_->handleKey(key("a", control));
    controller_->handleKey(key("x", control));
    controller_->synchronize();

    // One, for the collapsed caret the cut left: the full range never existed at a frame boundary.
    EXPECT_EQ(countRecorded(kSelectionChangeEvent), 1U);
    EXPECT_EQ(countRecorded(kChangeEvent), 1U);

    recordedEventTypes_->clear();
    controller_->handleKey(key("v", control));
    controller_->synchronize();
    controller_->handleKey(key("a", control));
    controller_->synchronize();

    // Two, with a frame sitting between the paste and the select-all: the caret the paste left, then the full
    // range. That frame is what the scenario's added sleep buys, and what makes its `selection=0..12` reachable.
    EXPECT_EQ(countRecorded(kSelectionChangeEvent), 2U);
    EXPECT_EQ(countRecorded(kChangeEvent), 1U);
}

TEST_F(TextInputControllerTest, ATextKeyIsConsumedAndAnUnfocusedControllerIgnoresEverything) {
    commitTextInput(folly::dynamic::object());

    EXPECT_EQ(controller_->handleKey(key("a")), TextInputKeyResult::Consumed);

    TextInputController unfocused = makeController();

    EXPECT_EQ(unfocused.handleKey(key("a")), TextInputKeyResult::Ignored);
    EXPECT_EQ(unfocused.handleKey(key("Enter")), TextInputKeyResult::Ignored);
}

// Issue #256, and the prop that decides both routing sites. `scrollEnabled` is not on upstream's
// `BaseTextInputProps` — iOS and Android each carry it on their own platform props — so without this row the
// prop parses to nothing and `scrollEnabled={false}` cannot be expressed at all. `editable` is deliberately not
// part of the predicate: a read-only field still scrolls, and react/core#35388 is the bug filed when it did not.
TEST_F(TextInputControllerTest, ScrollEnabledDefaultsToTrueAndIsWhatMakesAFieldAWindowOnItsOwnContent) {
    commitTextInput(folly::dynamic::object("multiline", true));

    EXPECT_TRUE(mountedField_->getConcreteProps().scrollEnabled);
    EXPECT_TRUE(isScrollableField(mountedField_->getConcreteProps()));

    commitTextInput(folly::dynamic::object("multiline", true)("scrollEnabled", false));

    EXPECT_FALSE(mountedField_->getConcreteProps().scrollEnabled);
    EXPECT_FALSE(isScrollableField(mountedField_->getConcreteProps()));

    // A read-only field is still a window on its own content.
    commitTextInput(folly::dynamic::object("multiline", true)("editable", false));

    EXPECT_TRUE(isScrollableField(mountedField_->getConcreteProps()));

    // A single-line field wraps nothing and scrolls horizontally instead, so it is never one of these however
    // the prop is set.
    commitTextInput(folly::dynamic::object("scrollEnabled", true));

    EXPECT_FALSE(isScrollableField(mountedField_->getConcreteProps()));
}

TEST_F(TextInputControllerTest, TheCompositionLifecycleSuppressesNothingTheModelDoesNotOwn) {
    commitTextInput(folly::dynamic::object());

    EXPECT_FALSE(controller_->isComposing());

    controller_->onImePreedit("ni", 0, 2);

    EXPECT_TRUE(controller_->isComposing());

    controller_->onImeCommit("你");

    EXPECT_FALSE(controller_->isComposing());
}

// core#54570: an uncontrolled multiline field never grew. Upstream's `updateStateIfNeeded` skips a field whose
// React-tree text is empty, so the state's font-size multiplier stayed NaN, compared unequal to the layout's, and
// `attributedStringBoxToMeasure` measured the placeholder for the rest of the field's life. The initial state now
// carries the multiplier the root's default `LayoutContext` measures with, so the first platform-side edit — which
// advances the revision past the initial one — is what the next layout measures. `text-input-grow.png` is the
// picture of that; this is the number.
TEST_F(TextInputControllerTest, TheInitialStateCarriesTheMultiplierAPlatformEditIsMeasuredAgainst) {
    commitTextInput(folly::dynamic::object("multiline", true));

    const facebook::react::TextInputState& stateData = mountedField_->getStateData();

    EXPECT_EQ(mountedField_->getState()->getRevision(), facebook::react::State::initialRevisionValue);
    EXPECT_TRUE(stateData.attributedStringBox.getValue().isEmpty());
    EXPECT_FLOAT_EQ(stateData.reactTreeAttributedString.getBaseTextAttributes().fontSizeMultiplier, 1.0F);
}

// #251: `numberOfLines` and `ellipsizeMode` reach a field through the shared parser, and a field is never
// truncated, so the props it commits with carry neither — which is what makes Yoga's measure and the paint agree.
TEST_F(TextInputControllerTest, AFieldCommitsWithNoLineLimitAndNoEllipsisWhateverItWasGiven) {
    commitTextInput(folly::dynamic::object("multiline", true)("numberOfLines", 1)("ellipsizeMode", "tail"));

    const TextInputProps& props = mountedField_->getConcreteProps();

    EXPECT_EQ(props.paragraphAttributes.maximumNumberOfLines, 0);
    EXPECT_EQ(props.paragraphAttributes.ellipsizeMode, facebook::react::EllipsizeMode::Clip);
}

} // namespace
