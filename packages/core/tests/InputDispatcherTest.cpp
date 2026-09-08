#include "InputDispatcher.h"

#include "InputPipeline.h"
#include "ShadowTreeTestSupport.h"

#include <folly/dynamic.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

namespace {

using facebook::react::RootShadowNode;
using facebook::react::ShadowNode;
using facebook::react::ShadowNodeFragment;
using facebook::react::ShadowTreeCommitOptions;
using facebook::react::Tag;
using react_native_linux::InputDispatcher;
using react_native_linux::TextInputContentPurpose;
using react_native_linux::TextInputFieldFixture;
using react_native_linux::TextInputFocusSink;

constexpr Tag kFieldAlphaTag = 20;
constexpr Tag kFieldBetaTag = 21;
constexpr char kFocusCommandName[] = "focus";

using ChildList = std::vector<std::shared_ptr<const ShadowNode>>;

/** One `focusField`/`blurField` call, in the order the dispatcher made it. */
struct RecordedFocusCall {
    bool isFocus;
    Tag fieldIdentifier;
    TextInputContentPurpose contentPurpose;
};

/**
 * A `TextInputFocusSink` that records every `focusField`/`blurField` call rather than acting on it, so a test can
 * assert on the exact sequence a focus change produced without a real `zwp_text_input_v3` object behind it.
 */
class RecordingTextInputFocusSink final : public TextInputFocusSink {
public:
    void focusField(int32_t fieldIdentifier, TextInputContentPurpose contentPurpose) override {
        calls.push_back(
            RecordedFocusCall{.isFocus = true, .fieldIdentifier = fieldIdentifier, .contentPurpose = contentPurpose});
    }

    void blurField() override {
        calls.push_back(RecordedFocusCall{
            .isFocus = false, .fieldIdentifier = 0, .contentPurpose = TextInputContentPurpose::Normal});
    }

    void setSurroundingText(std::string /*text*/, int32_t /*cursor*/, int32_t /*anchor*/) override {}

    void setCursorRectangle(int32_t /*x*/, int32_t /*y*/, int32_t /*width*/, int32_t /*height*/) override {}

    void flushTextInput() override {}

    std::vector<RecordedFocusCall> calls;
};

/**
 * Issue #397's regression: two mounted `<TextInput>` fields of the same content purpose, focused through the
 * dispatcher exactly as a `<View>` ref's `focus()` or a click would, with a recording sink standing in for
 * `TextInputClient`. `reportedContentPurpose_` alone could not tell the two fields apart — both are `Normal` —
 * so a focus change from one straight to the other found nothing had "changed" and `focusField` was never called
 * for the new field at all, leaving `TextInputSession` (and so the compositor's session) still pointed at the
 * field that had already lost focus. `TextInputSession::focusField` is what turns a field-identifier change into
 * the protocol's own disable-then-enable — see `TwoFieldsSwappedInOneFrameAreATeardownAndASetup` in
 * TextInputSessionTest.cpp — so the dispatcher's own contract is only ever "call `focusField` for the field that
 * now holds the caret", once per change.
 */
class InputDispatcherTest : public TextInputFieldFixture {
protected:
    void SetUp() override {
        TextInputFieldFixture::SetUp();

        const ShadowTreeCommitOptions commitOptions{.enableStateReconciliation = false, .mountSynchronously = true};

        shadowTree_->commit(
            [this](const RootShadowNode& oldRootShadowNode) {
                return std::static_pointer_cast<RootShadowNode>(oldRootShadowNode.ShadowNode::clone(
                    ShadowNodeFragment{.props = ShadowNodeFragment::propsPlaceholder(),
                                       .children = std::make_shared<const ChildList>(ChildList{
                                           makeAccessibleField(kFieldAlphaTag), makeAccessibleField(kFieldBetaTag)})}));
            },
            commitOptions);

        dispatcher_ = std::make_unique<InputDispatcher>(uiManager_, mountingManager_, kSurfaceId);
        dispatcher_->setTextInputFocusSink(&sink_);

        // Picks up the two fields just committed; neither is focused yet, so this produces no sink call.
        dispatcher_->dispatch({});
    }

    void focus(Tag tag) {
        dispatcher_->dispatchCommands({{.tag = tag, .name = kFocusCommandName, .args = folly::dynamic::object()}});
    }

    /** The one shape every `focusField` call in these tests takes: a Normal-purpose field, named by its tag. */
    static void expectFocusFieldCall(const RecordedFocusCall& call, Tag fieldTag) {
        EXPECT_TRUE(call.isFocus);
        EXPECT_EQ(call.fieldIdentifier, fieldTag);
        EXPECT_EQ(call.contentPurpose, TextInputContentPurpose::Normal);
    }

    std::unique_ptr<InputDispatcher> dispatcher_;
    RecordingTextInputFocusSink sink_;

private:
    std::shared_ptr<const ShadowNode> makeAccessibleField(Tag tag) {
        folly::dynamic props = folly::dynamic::object("accessible", true);
        props["width"] = 200;
        props["height"] = 40;

        return makeField(tag, std::move(props));
    }
};

TEST_F(InputDispatcherTest, FocusMovingBetweenTwoFieldsOfTheSamePurposeStillCallsFocusFieldForTheNewOne) {
    focus(kFieldAlphaTag);

    ASSERT_EQ(sink_.calls.size(), 1U);
    expectFocusFieldCall(sink_.calls[0], kFieldAlphaTag);

    focus(kFieldBetaTag);

    // The bug: comparing only the content purpose left this second focus indistinguishable from "nothing
    // happened", because both fields are `Normal` — this second `focusField` call never reached the sink at all.
    ASSERT_EQ(sink_.calls.size(), 2U);
    expectFocusFieldCall(sink_.calls[1], kFieldBetaTag);
}

TEST_F(InputDispatcherTest, FocusingTheSameFieldAgainProducesNoSecondCall) {
    focus(kFieldAlphaTag);

    ASSERT_EQ(sink_.calls.size(), 1U);

    focus(kFieldAlphaTag);

    EXPECT_EQ(sink_.calls.size(), 1U);
}

/**
 * A second regression on the same cache: a field focused while no sink is installed still writes
 * `reportedTextInputField_`, because `updateTextInput()` runs every frame regardless of whether there is anyone to
 * tell. Installing a sink afterward must not find that cached value unchanged and stay silent — the session would
 * never turn on until focus happened to move again. `setTextInputFocusSink()` now forgets the cached field, so the
 * very next `dispatch()` replays `focusField()` for whatever is focused.
 */
TEST_F(InputDispatcherTest, InstallingASinkAfterAFieldWasFocusedWithoutOneReplaysFocusField) {
    dispatcher_->setTextInputFocusSink(nullptr);

    focus(kFieldAlphaTag);

    ASSERT_TRUE(sink_.calls.empty());

    dispatcher_->setTextInputFocusSink(&sink_);
    dispatcher_->dispatch({});

    ASSERT_EQ(sink_.calls.size(), 1U);
    expectFocusFieldCall(sink_.calls[0], kFieldAlphaTag);
}

} // namespace
