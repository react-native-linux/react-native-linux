#include "DimensionsSource.h"
#include "InputDispatcher.h"
#include "InputPipeline.h"
#include "LinuxMountingManager.h"
#include "ModalHostComponent.h"
#include "RecordingEventDispatcher.h"
#include "ScrollController.h"
#include "ShadowTreeTestSupport.h"
#include "TextInputComponent.h"

#include <folly/dynamic.h>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <react/renderer/components/scrollview/ScrollViewComponentDescriptor.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>
#include <react/renderer/core/RawEvent.h>
#include <react/renderer/mounting/MountingCoordinator.h>

// ADR-0005's input slice: while a `<Modal>` is presented, the focus trap, the initial focus and its restore, Escape
// as `onRequestClose`, and the wheel and the keyboard's scroll target all answer from the topmost overlay the scene
// presents. Every tree here is committed and then mounted into a real scene, because the scene's overlay list is
// where the topmost modal comes from.

namespace {

using facebook::react::ComponentDescriptorParameters;
using facebook::react::ShadowNode;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using react_native_linux::InputDispatcher;
using react_native_linux::InputEvent;
using react_native_linux::InputEventKind;
using react_native_linux::LinuxMountingManager;
using react_native_linux::ScrollController;

using ChildList = std::vector<std::shared_ptr<const ShadowNode>>;

constexpr SurfaceId kSurfaceId = 1;
constexpr Tag kFieldTag = 20;
constexpr Tag kInvokerTag = 21;
constexpr Tag kPageScrollViewTag = 30;
constexpr Tag kModalTag = 40;
constexpr Tag kFirstButtonTag = 41;
constexpr Tag kSecondButtonTag = 42;
constexpr Tag kModalFieldTag = 43;
constexpr Tag kModalScrollViewTag = 44;
constexpr Tag kNestedModalTag = 50;
constexpr Tag kNestedButtonTag = 51;

InputEvent keyPress(const std::string& key, bool isShiftDown = false) {
    return InputEvent{.kind = InputEventKind::KeyPress, .key = key, .modifiers = {.shift = isShiftDown}};
}

class ModalFocusTest : public ::testing::Test {
protected:
    void SetUp() override {
        dimensions_->configure(800, 600, 1);
        contextContainer_->insert(react_native_linux::kDimensionsSourceContextKey,
                                  std::shared_ptr<const react_native_linux::DimensionsSource>(dimensions_));
        uiManager_ = react_native_linux::makeTaskDroppingUIManager(contextContainer_);
        shadowTree_ = react_native_linux::addRegisteredShadowTree(*uiManager_, shadowTreeDelegate_, *contextContainer_,
                                                                  kSurfaceId);
        mountingManager_->startSurface(kSurfaceId, {.width = 800, .height = 600});
        dispatcher_ = std::make_unique<InputDispatcher>(uiManager_, mountingManager_, kSurfaceId,
                                                        [this]() { ++hardwareBackPresses_; });
    }

    void TearDown() override { react_native_linux::removeShadowTree(*uiManager_, kSurfaceId); }

    /** Commits `children` under the root and mounts the transaction, as the JavaScript thread does. */
    void present(ChildList children) {
        react_native_linux::commitChildren(*shadowTree_, std::make_shared<const ChildList>(std::move(children)));

        std::optional<facebook::react::MountingTransaction> transaction =
            shadowTree_->getMountingCoordinator()->pullTransaction();

        mountingManager_->executeMount(kSurfaceId, std::move(transaction.value()));
    }

    void focus(Tag tag) {
        dispatcher_->dispatchCommands({{.tag = tag, .name = "focus", .args = folly::dynamic::object()}});
    }

    /** Every `tag:type` the emitters produced since the last call. */
    std::vector<std::string> takeEvents() { return std::exchange(*events_, {}); }

    /** The tags that took focus since the last call, in order. */
    std::vector<Tag> takeFocusedTags() {
        std::vector<Tag> focused;

        for (const std::string& event : takeEvents()) {
            const size_t separator = event.find(':');

            if (event.substr(separator + 1) == "topFocus") {
                focused.push_back(std::stoi(event.substr(0, separator)));
            }
        }

        return focused;
    }

    std::shared_ptr<const ShadowNode> node(auto& descriptor, Tag tag, folly::dynamic props, ChildList children = {}) {
        return react_native_linux::makeConfiguredShadowNode(descriptor, tag, kSurfaceId, contextContainer_,
                                                            std::move(props),
                                                            std::make_shared<const ChildList>(std::move(children)));
    }

    std::shared_ptr<const ShadowNode> button(Tag tag) {
        return node(viewDescriptor_, tag, folly::dynamic::object("accessible", true)("width", 100)("height", 40));
    }

    std::shared_ptr<const ShadowNode> field(Tag tag) {
        return node(fieldDescriptor_, tag, folly::dynamic::object("accessible", true)("width", 200)("height", 40));
    }

    std::shared_ptr<const ShadowNode> modal(Tag tag, ChildList children) {
        return node(modalDescriptor_, tag, folly::dynamic::object(), std::move(children));
    }

    /** A 100x100 `<ScrollView>` at the top left over 1000 points of content, which is what a wheel can move. */
    std::shared_ptr<const ShadowNode> scrollView(Tag tag, ChildList content = {}) {
        content.push_back(node(viewDescriptor_, tag + 1, folly::dynamic::object("width", 100)("height", 1000)));

        return node(scrollViewDescriptor_, tag, folly::dynamic::object("width", 100)("height", 100),
                    std::move(content));
    }

    std::shared_ptr<react_native_linux::DimensionsSource> dimensions_{
        std::make_shared<react_native_linux::DimensionsSource>()};
    std::shared_ptr<const facebook::react::ContextContainer> contextContainer_{
        std::make_shared<facebook::react::ContextContainer>()};
    std::shared_ptr<facebook::react::UIManager> uiManager_;
    std::shared_ptr<LinuxMountingManager> mountingManager_{std::make_shared<LinuxMountingManager>()};
    std::unique_ptr<InputDispatcher> dispatcher_;
    int hardwareBackPresses_{0};

private:
    std::shared_ptr<std::vector<std::string>> events_{std::make_shared<std::vector<std::string>>()};
    std::shared_ptr<const facebook::react::EventDispatcher> eventDispatcher_{
        react_native_linux::makeListeningEventDispatcher([events = events_](const facebook::react::RawEvent& event) {
            const std::shared_ptr<const facebook::react::ShadowNodeFamily> family = event.shadowNodeFamily.lock();

            events->push_back(std::to_string(family->getTag()) + ":" + event.type);

            return true;
        })};
    ComponentDescriptorParameters parameters_{
        .eventDispatcher = eventDispatcher_, .contextContainer = contextContainer_, .flavor = nullptr};
    facebook::react::ViewComponentDescriptor viewDescriptor_{parameters_};
    react_native_linux::TextInputComponentDescriptor fieldDescriptor_{parameters_};
    react_native_linux::ModalHostComponentDescriptor modalDescriptor_{parameters_};
    facebook::react::ScrollViewComponentDescriptor scrollViewDescriptor_{parameters_};
    react_native_linux::PassThroughShadowTreeDelegate shadowTreeDelegate_;
    facebook::react::ShadowTree* shadowTree_{nullptr};
};

TEST_F(ModalFocusTest, PresentingMovesFocusInsideAndTabCyclesOnlyThere) {
    present({field(kFieldTag), button(kInvokerTag)});
    focus(kInvokerTag);
    takeEvents();

    present(
        {field(kFieldTag), button(kInvokerTag), modal(kModalTag, {button(kFirstButtonTag), button(kSecondButtonTag)})});
    dispatcher_->dispatch({keyPress("Tab"), keyPress("Tab"), keyPress("Tab", true), keyPress("Tab", true)});

    EXPECT_EQ(takeFocusedTags(), (std::vector<Tag>{kFirstButtonTag, kSecondButtonTag, kFirstButtonTag, kSecondButtonTag,
                                                   kFirstButtonTag}));
}

/** `onShow`'s `ref.focus()` and `autoFocus` arrive as commands, which drain after the present and so win. */
TEST_F(ModalFocusTest, AnApplicationFocusAfterThePresentWins) {
    present({button(kInvokerTag), modal(kModalTag, {button(kFirstButtonTag), button(kSecondButtonTag)})});
    focus(kSecondButtonTag);

    EXPECT_EQ(takeFocusedTags(), (std::vector<Tag>{kFirstButtonTag, kSecondButtonTag}));

    focus(kInvokerTag);

    EXPECT_TRUE(takeFocusedTags().empty());
}

TEST_F(ModalFocusTest, DismissingRestoresFocusToTheInvokerThroughANestedModal) {
    present({button(kInvokerTag)});
    focus(kInvokerTag);
    present({button(kInvokerTag), modal(kModalTag, {button(kFirstButtonTag), button(kSecondButtonTag)})});
    dispatcher_->dispatch({keyPress("Tab")});
    present({button(kInvokerTag), modal(kModalTag, {button(kFirstButtonTag), button(kSecondButtonTag),
                                                    modal(kNestedModalTag, {button(kNestedButtonTag)})})});
    dispatcher_->dispatch({});
    present({button(kInvokerTag), modal(kModalTag, {button(kFirstButtonTag), button(kSecondButtonTag)})});
    dispatcher_->dispatch({});
    present({button(kInvokerTag)});
    dispatcher_->dispatch({});

    EXPECT_EQ(takeFocusedTags(), (std::vector<Tag>{kInvokerTag, kFirstButtonTag, kSecondButtonTag, kNestedButtonTag,
                                                   kSecondButtonTag, kInvokerTag}));
}

/** The trap filters focus only: the field beneath is still a mounted field, so its draft survives the modal. */
TEST_F(ModalFocusTest, AFieldBeneathKeepsItsBufferAcrossPresentAndDismiss) {
    present({field(kFieldTag)});
    focus(kFieldTag);
    dispatcher_->dispatch({keyPress("a"), keyPress("b")});

    present({field(kFieldTag), modal(kModalTag, {button(kFirstButtonTag)})});
    dispatcher_->dispatch({});
    present({field(kFieldTag)});
    dispatcher_->dispatch({});
    takeEvents();

    // An emptied buffer would have nothing to delete, and a deletion that changes nothing emits no `change`.
    dispatcher_->dispatch({keyPress("Backspace")});

    EXPECT_EQ(takeEvents(),
              (std::vector<std::string>{"20:topKeyDown", "20:topKeyPress", "20:topChange", "20:topSelectionChange"}));
}

TEST_F(ModalFocusTest, EscapeIsTheTopmostModalsCloseRequestAndNotABackPress) {
    present({modal(kModalTag, {field(kModalFieldTag), modal(kNestedModalTag, {button(kNestedButtonTag)})})});
    dispatcher_->dispatch({});
    takeEvents();

    dispatcher_->dispatch({keyPress("Escape"), InputEvent{.kind = InputEventKind::KeyRelease, .key = "Escape"}});

    EXPECT_EQ(takeEvents(), (std::vector<std::string>{"51:topKeyDown", "50:topRequestClose", "51:topKeyUp"}));
    EXPECT_EQ(hardwareBackPresses_, 0);
}

/** A focused field inside the modal does not blur on Escape: the modal takes it, as Android's dialog takes back. */
TEST_F(ModalFocusTest, EscapeInAFieldInsideTheModalClosesTheModalAndKeepsTheField) {
    present({modal(kModalTag, {field(kModalFieldTag)})});
    dispatcher_->dispatch({});
    takeEvents();

    dispatcher_->dispatch({keyPress("Escape")});

    EXPECT_EQ(takeEvents(), (std::vector<std::string>{"43:topKeyDown", "40:topRequestClose"}));
}

TEST_F(ModalFocusTest, EscapeWhileComposingDoesNotCloseTheModal) {
    present({modal(kModalTag, {field(kModalFieldTag)})});
    dispatcher_->dispatch({InputEvent{.kind = InputEventKind::ImePreedit, .text = "k"}});
    takeEvents();

    dispatcher_->dispatch({keyPress("Escape")});

    EXPECT_EQ(takeEvents(), (std::vector<std::string>{"43:topKeyDown"}));
}

TEST_F(ModalFocusTest, PresentingCancelsAPressBeneath) {
    present({button(kInvokerTag)});
    dispatcher_->dispatch({InputEvent{.kind = InputEventKind::PointerButtonPress, .surfacePoint = {.x = 10, .y = 10}}});
    takeEvents();

    present({button(kInvokerTag), modal(kModalTag, {})});
    dispatcher_->dispatch({});

    EXPECT_EQ(takeEvents(), (std::vector<std::string>{"21:topTouchCancel", "21:topBlur"}));
}

/** A modal declared inside the page's `<ScrollView>` still never scrolls it, by key or by focus. */
TEST_F(ModalFocusTest, KeysScrollOnlyInsideTheTopmostModal) {
    present({scrollView(kPageScrollViewTag, {modal(kModalTag, {button(kFirstButtonTag)})})});
    dispatcher_->dispatch({});
    mountingManager_->takeCommands();

    dispatcher_->dispatch({keyPress("PageDown")});

    EXPECT_TRUE(mountingManager_->takeCommands().empty());

    present({scrollView(kPageScrollViewTag), modal(kModalTag, {scrollView(kModalScrollViewTag)})});
    dispatcher_->dispatch({keyPress("PageDown")});

    const std::vector<react_native_linux::SceneCommand> commands = mountingManager_->takeCommands();

    ASSERT_EQ(commands.size(), 1U);
    EXPECT_EQ(commands[0].tag, kModalScrollViewTag);
}

/** The shadow tree's own hit test would find the page's content first, because the modal is declared inside it. */
TEST_F(ModalFocusTest, TheWheelOverAModalScrollsOnlyInsideIt) {
    ScrollController controller(uiManager_, mountingManager_, kSurfaceId);
    const InputEvent wheel{.kind = InputEventKind::PointerScrollDiscrete,
                           .surfacePoint = {.x = 50, .y = 50},
                           .scrollAxis = react_native_linux::ScrollAxisKind::Vertical,
                           .scrollAmount = 1.0};

    present({scrollView(kPageScrollViewTag, {modal(kModalTag, {})})});
    controller.dispatch({wheel});
    controller.advance(16.0);

    EXPECT_TRUE(takeEvents().empty());

    present({scrollView(kPageScrollViewTag, {modal(kModalTag, {scrollView(kModalScrollViewTag)})})});
    controller.dispatch({wheel});
    controller.advance(16.0);

    EXPECT_EQ(takeEvents(), (std::vector<std::string>{"44:topScroll", "44:topMomentumScrollBegin"}));
}

TEST_F(ModalFocusTest, AFrameCarriesTheModalsItPaintsForTheFirstTime) {
    present({modal(kModalTag, {})});

    ASSERT_EQ(mountingManager_->takeFrame().presentedOverlays.size(), 1U);
    EXPECT_TRUE(mountingManager_->takeFrame().presentedOverlays.empty());
}

} // namespace
