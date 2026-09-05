#include "TextInputController.h"

#include "ShadowTreeTestSupport.h"
#include "TextInputComponent.h"

#include <LinuxMountingManager.h>
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
using facebook::react::EventDispatcher;
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
using react_native_linux::LinuxMountingManager;
using react_native_linux::makeConfiguredShadowNode;
using react_native_linux::makeTaskDroppingUIManager;
using react_native_linux::PassThroughShadowTreeDelegate;
using react_native_linux::removeShadowTree;
using react_native_linux::TextInputComponentDescriptor;
using react_native_linux::TextInputController;
using react_native_linux::TextInputKeyResult;
using react_native_linux::TextInputShadowNode;

constexpr SurfaceId kSurfaceId = 1;
constexpr Tag kFieldTag = 20;

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

    std::unique_ptr<TextInputController> controller_;
    std::shared_ptr<const TextInputShadowNode> mountedField_;

private:
    std::shared_ptr<const ShadowNode> makeField(folly::dynamic props) {
        return makeConfiguredShadowNode(fieldDescriptor_, kFieldTag, kSurfaceId, contextContainer_, std::move(props),
                                        std::make_shared<const ChildList>());
    }

    PassThroughShadowTreeDelegate shadowTreeDelegate_;
    std::shared_ptr<const ContextContainer> contextContainer_{std::make_shared<ContextContainer>()};
    TextInputComponentDescriptor fieldDescriptor_{ComponentDescriptorParameters{
        .eventDispatcher = EventDispatcher::Shared{}, .contextContainer = contextContainer_, .flavor = nullptr}};
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

TEST_F(TextInputControllerTest, ATextKeyIsConsumedAndAnUnfocusedControllerIgnoresEverything) {
    commitTextInput(folly::dynamic::object());

    EXPECT_EQ(controller_->handleKey(key("a")), TextInputKeyResult::Consumed);

    TextInputController unfocused = makeController();

    EXPECT_EQ(unfocused.handleKey(key("a")), TextInputKeyResult::Ignored);
    EXPECT_EQ(unfocused.handleKey(key("Enter")), TextInputKeyResult::Ignored);
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

} // namespace
