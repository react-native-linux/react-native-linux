#include "InputDispatcher.h"
#include "InputPipeline.h"
#include "ShadowTreeTestSupport.h"

#include <folly/dynamic.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include <react/renderer/core/RawEvent.h>
#include <react/renderer/core/ShadowNodeFamily.h>

/**
 * The half of upstream's `private/renderer/core/__tests__` event-dispatching itests that describes *this*
 * platform, at v0.87.1 (`a59eff64fa907ed6e919fafe6cbd26d1d54c2de3`): the shape of the raw event a
 * `wl_pointer` gesture becomes before React sees it.
 *
 * Those three files — `EventTargetDispatching-itest.js` (42 cases), `EventDispatching-itest.js` (14) and
 * `ResponderEventTarget-itest.js` (24) — assert almost entirely on machinery that lives *above* the platform
 * boundary and is identical on every platform because it is the same JavaScript: capture and bubble phase
 * order, listener ordering between declarative props and `addEventListener`, `stopPropagation`,
 * `preventDefault`, retargeting onto `document`, `dispatchConfig`, and the whole responder negotiation. None of
 * that is reachable from C++, and none of it is ours to get wrong: `packages/core/src` contains no capture or
 * bubble walk at all, by design — `PointerRouter` emits one event at one target and upstream's
 * `PointerEventsProcessor`, reached from `UIManagerBinding` on the JavaScript thread, derives everything else.
 * Porting those assertions into C++ would mean building a second event-path implementation purely so a test
 * could assert against it, which is the mechanism-for-a-test the Prime Directive forbids.
 *
 * What upstream's `EventDispatching-itest.js` *does* pin about a platform is the two properties React reads off
 * every raw event before any handler runs, and which a platform alone decides: the `RawEvent::Category`, which
 * `EventQueueProcessor` turns into the React lane the handler runs in, and `isUnique`, which decides whether
 * `EventQueue` may collapse an event into the one before it. Those cases — "dispatches events with discrete
 * priority", "dispatches events with continuous priority", "uses discrete event priority for both
 * ContinuousStart and ContinuousEnd", and the four "unique events" cases — are the ones this file ports, plus
 * `EventTargetDispatching-itest.js`'s "dispatches basic press event to handler" for the target itself.
 *
 * The category-to-priority arithmetic and the queue's collapsing rule are upstream's own and are already run
 * here unmodified, from `react/renderer/core/tests/EventQueueProcessorTest.cpp` in `rnl_core_hermes_tests`, so
 * this file stops at the boundary between them: it pins what our gesture hands the queue, and does not
 * re-derive what upstream's queue then does with it.
 */

namespace {

using facebook::react::RawEvent;
using react_native_linux::InputDispatcher;
using react_native_linux::InputEvent;
using react_native_linux::InputEventKind;
using react_native_linux::LinuxMountingManager;

constexpr Tag kPanelTag = 30;
constexpr Tag kChildTag = 31;
constexpr int kPrimaryButton = 0;
constexpr facebook::react::SurfaceId kSurfaceId = 1;

using ChildList = std::vector<std::shared_ptr<const ShadowNode>>;

/** One `RawEvent` as it left the platform, in the four fields React decides anything from. */
struct RecordedRawEvent {
    Tag tag;
    std::string type;
    RawEvent::Category category;
    bool isUnique;
};

/**
 * An `EventDispatcher` that records the platform's own view of every event and carries it no further.
 *
 * The listener answers "handled", which is what keeps the event out of the `EventQueue` behind it: this file
 * asserts on what the platform enqueues, and upstream's `EventQueueProcessorTest` is what asserts on what the
 * queue then does. The tag is read off the event's `ShadowNodeFamily` rather than off its `EventTarget`,
 * because a fixture that commits without a JavaScript runtime has no instance handle to build one from, and the
 * family is the identity Fabric itself carries for exactly that reason.
 */
std::shared_ptr<const facebook::react::EventDispatcher>
makeRawEventRecordingDispatcher(const std::shared_ptr<std::vector<RecordedRawEvent>>& recordedEvents) {
    return react_native_linux::makeListeningEventDispatcher(
        [recordedEvents](const facebook::react::RawEvent& rawEvent) {
            const std::shared_ptr<const facebook::react::ShadowNodeFamily> family = rawEvent.shadowNodeFamily.lock();

            recordedEvents->push_back(RecordedRawEvent{.tag = family == nullptr ? 0 : family->getTag(),
                                                       .type = rawEvent.type,
                                                       .category = rawEvent.category,
                                                       .isUnique = rawEvent.isUnique});

            return true;
        });
}

/**
 * A panel holding one smaller child, both absolutely placed, both carrying a live event emitter — the tree
 * upstream's dispatching itests build with two nested `<View>`s and a handler on each.
 *
 * The retained scene is left empty on purpose, so `InputDispatcher::resolveTarget` answers from
 * `UIManager::findNodeAtPoint` over the committed tree: this file is about what a resolved target is *sent*,
 * and the scene hit test itself is `AnimatedHitTestTest` and `HitTestDifferentialTest`'s subject.
 */
class InputEventDispatchConformanceTest : public ::testing::Test {
protected:
    void SetUp() override {
        uiManager_ = react_native_linux::makeTaskDroppingUIManager(contextContainer_);
        mountingManager_ = std::make_shared<LinuxMountingManager>();
        shadowTree_ = react_native_linux::addRegisteredShadowTree(*uiManager_, shadowTreeDelegate_, *contextContainer_,
                                                                  kSurfaceId);

        const ShadowTreeCommitOptions commitOptions{.enableStateReconciliation = false, .mountSynchronously = true};

        shadowTree_->commit(
            [this](const RootShadowNode& oldRootShadowNode) {
                return react_native_linux::cloneRootWithChildren(
                    oldRootShadowNode, std::make_shared<const ChildList>(ChildList{makePanel()}));
            },
            commitOptions);

        dispatcher_ = std::make_unique<InputDispatcher>(uiManager_, mountingManager_, kSurfaceId);
    }

    void TearDown() override { react_native_linux::removeShadowTree(*uiManager_, kSurfaceId); }

    static InputEvent pointerEvent(InputEventKind kind, float x, float y) {
        return InputEvent{.kind = kind, .surfacePoint = {.x = x, .y = y}, .button = kPrimaryButton};
    }

    /** The whole of one primary-button press and release at a point, as one frame of compositor input. */
    void pressAndRelease(float x, float y) {
        dispatcher_->dispatch({pointerEvent(InputEventKind::PointerButtonPress, x, y),
                               pointerEvent(InputEventKind::PointerButtonRelease, x, y)});
    }

    /**
     * A press, one motion and the release, as one frame: the shortest input that carries all four of the events
     * a gesture can produce, and — when the two points are on different nodes — the drag that retargets.
     */
    void pressDragAndRelease(float pressX, float pressY, float releaseX, float releaseY) {
        dispatcher_->dispatch({pointerEvent(InputEventKind::PointerButtonPress, pressX, pressY),
                               pointerEvent(InputEventKind::PointerMotion, releaseX, releaseY),
                               pointerEvent(InputEventKind::PointerButtonRelease, releaseX, releaseY)});
    }

    std::vector<std::string> recordedTypes() const {
        std::vector<std::string> types;

        for (const RecordedRawEvent& recordedEvent : *recordedEvents_) {
            types.push_back(recordedEvent.type);
        }

        return types;
    }

    /** The one recorded event of a type, so a table-shaped assertion can name the type it is asking about. */
    const RecordedRawEvent& recordedEventOfType(const std::string& type) const {
        for (const RecordedRawEvent& recordedEvent : *recordedEvents_) {
            if (recordedEvent.type == type) {
                return recordedEvent;
            }
        }

        ADD_FAILURE() << "no recorded event of type " << type;

        return recordedEvents_->front();
    }

    std::shared_ptr<std::vector<RecordedRawEvent>> recordedEvents_{std::make_shared<std::vector<RecordedRawEvent>>()};
    react_native_linux::PassThroughShadowTreeDelegate shadowTreeDelegate_;
    std::shared_ptr<const facebook::react::ContextContainer> contextContainer_{
        std::make_shared<facebook::react::ContextContainer>()};
    std::shared_ptr<const facebook::react::EventDispatcher> eventDispatcher_{
        makeRawEventRecordingDispatcher(recordedEvents_)};
    facebook::react::ViewComponentDescriptor viewDescriptor_{facebook::react::ComponentDescriptorParameters{
        .eventDispatcher = eventDispatcher_, .contextContainer = contextContainer_, .flavor = nullptr}};
    std::shared_ptr<facebook::react::UIManager> uiManager_;
    std::shared_ptr<LinuxMountingManager> mountingManager_;
    facebook::react::ShadowTree* shadowTree_{nullptr};
    std::unique_ptr<InputDispatcher> dispatcher_;

private:
    static folly::dynamic box(int left, int top, int width, int height) {
        folly::dynamic props = folly::dynamic::object("position", "absolute");

        props["left"] = left;
        props["top"] = top;
        props["width"] = width;
        props["height"] = height;

        return props;
    }

    std::shared_ptr<const ShadowNode> makeView(Tag tag, folly::dynamic props,
                                               const std::shared_ptr<const ChildList>& children) {
        return react_native_linux::makeConfiguredShadowNode(viewDescriptor_, tag, kSurfaceId, contextContainer_,
                                                            std::move(props), children);
    }

    std::shared_ptr<const ShadowNode> makePanel() {
        const std::shared_ptr<const ChildList> childOfPanel = std::make_shared<const ChildList>(
            ChildList{makeView(kChildTag, box(50, 50, 100, 100), std::make_shared<const ChildList>())});

        return makeView(kPanelTag, box(0, 0, 200, 200), childOfPanel);
    }
};

/**
 * `EventTargetDispatching-itest.js`, "dispatches basic press event to handler", and the inverse of "event
 * bubbles from child to parent": one press over the child produces `pointerDown`, `pointerUp` and the synthetic
 * `click`, in that order, on the child alone.
 *
 * The panel underneath it receives nothing. Upstream's bubbling case passes because React walks the fiber
 * ancestors after the fact, not because the platform dispatched twice — so the platform-side contract that
 * makes that case possible is precisely that exactly one event per gesture step leaves here, named for the
 * deepest node under the point. A second dispatch onto the ancestor would give React's own bubble walk a
 * duplicate to deliver.
 */
TEST_F(InputEventDispatchConformanceTest, APressReachesOnlyTheDeepestNodeUnderThePointAsDownUpAndClick) {
    pressAndRelease(100.0F, 100.0F);

    EXPECT_EQ(recordedTypes(), (std::vector<std::string>{"topPointerDown", "topPointerUp", "topClick"}));

    for (const RecordedRawEvent& recordedEvent : *recordedEvents_) {
        EXPECT_EQ(recordedEvent.tag, kChildTag) << recordedEvent.type << " was dispatched to the wrong node";
    }
}

/**
 * `EventDispatching-itest.js`, "dispatches events with discrete priority", "dispatches events with continuous
 * priority" and "uses discrete event priority for both ContinuousStart and ContinuousEnd": the category on each
 * event of a gesture, which is the only thing the platform contributes to the React lane its handler runs in.
 *
 * The pairing is what matters as much as the individual values. `pointerDown` opens the continuous window and
 * `pointerUp` closes it, so a drag's moves run at continuous priority and can be interrupted, while the press,
 * the release and the `click` that concludes them run discretely and cannot — that is upstream's
 * `EventQueueProcessor` rule, and it only produces that answer if the platform brackets the gesture this way. A
 * `pointerDown` that arrived as `Discrete` would leave every move in the drag at default priority instead.
 */
TEST_F(InputEventDispatchConformanceTest, TheGestureIsBracketedByTheCategoriesReactDerivesItsPrioritiesFrom) {
    pressDragAndRelease(100.0F, 100.0F, 110.0F, 110.0F);

    EXPECT_EQ(recordedEventOfType("topPointerDown").category, RawEvent::Category::ContinuousStart);
    EXPECT_EQ(recordedEventOfType("topPointerMove").category, RawEvent::Category::Continuous);
    EXPECT_EQ(recordedEventOfType("topPointerUp").category, RawEvent::Category::ContinuousEnd);
    EXPECT_EQ(recordedEventOfType("topClick").category, RawEvent::Category::Discrete);
}

/**
 * `EventDispatching-itest.js`'s four "unique events" cases, at the platform's end of them: the move is the only
 * event a gesture emits that `EventQueue` is allowed to collapse into the one before it.
 *
 * A 1000 Hz mouse produces sixteen motions per frame, and `InputQueue` already coalesces the contiguous run
 * before the dispatcher sees it — but the queue between here and JavaScript drains on its own beat and can hold
 * the moves of several frames at once, so the second, `isUnique` collapsing is what keeps a slow JavaScript
 * thread from being handed the whole backlog. It is only correct because it is confined to the move: a
 * `pointerDown` or a `click` marked unique would be silently dropped whenever two of them landed in one beat on
 * the same node, which is a double-click.
 */
TEST_F(InputEventDispatchConformanceTest, OnlyTheMoveIsCoalescible) {
    pressDragAndRelease(100.0F, 100.0F, 110.0F, 110.0F);

    EXPECT_TRUE(recordedEventOfType("topPointerMove").isUnique);
    EXPECT_FALSE(recordedEventOfType("topPointerDown").isUnique);
    EXPECT_FALSE(recordedEventOfType("topPointerUp").isUnique);
    EXPECT_FALSE(recordedEventOfType("topClick").isUnique);
}

/**
 * The observable statement of what this platform does instead of `EventDispatching-itest.js`'s "dispatches
 * pointer capture events": nothing captures, so a drag off the pressed node retargets to whatever is under the
 * pointer now, and the release there is a `pointerUp` with no `click`.
 *
 * Upstream's capture case drives `setPointerCapture` from JavaScript, which nothing on this platform calls —
 * there is no implicit capture on a desktop button-drag either. The consequence is a real contract rather than
 * an omission: `Pressability` reports `onPressOut` without `onPress` when the button comes up somewhere else,
 * which is what a desktop user dragging off a button expects. Were implicit capture ever added, this is the
 * assertion that would have to be rewritten deliberately rather than quietly satisfied.
 */
TEST_F(InputEventDispatchConformanceTest, ADragOffThePressedNodeRetargetsAndTheReleaseIsNotAClick) {
    pressDragAndRelease(100.0F, 100.0F, 20.0F, 20.0F);

    EXPECT_EQ(recordedTypes(), (std::vector<std::string>{"topPointerDown", "topPointerMove", "topPointerUp"}));
    EXPECT_EQ(recordedEventOfType("topPointerDown").tag, kChildTag);
    EXPECT_EQ(recordedEventOfType("topPointerMove").tag, kPanelTag);
    EXPECT_EQ(recordedEventOfType("topPointerUp").tag, kPanelTag);
}

} // namespace
