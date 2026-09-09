#include "InputDispatcher.h"
#include "InputPipeline.h"
#include "RecordingEventDispatcher.h"
#include "ShadowTreeTestSupport.h"

#include <folly/dynamic.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

#include <react/renderer/components/view/PointerEvent.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>
#include <react/renderer/components/view/primitives.h>
#include <react/renderer/core/RawEvent.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/core/ShadowNode.h>
#include <react/renderer/core/ShadowNodeFamily.h>
#include <react/renderer/uimanager/PointerEventsProcessor.h>

// Issue #36. Hover is desktop-only, so upstream's own `PointerEventsProcessor` — the class that turns a bare
// `topPointerMove` into `pointerEnter`/`pointerLeave`/`pointerOver`/`pointerOut` and decides which of them bubble
// — never runs under upstream's mobile-first CI. `InputPipeline.h`'s own `PointerRouter` docblock says why this
// file does not reimplement any of that: this platform hands the processor raw `topPointerMove`/`topPointerDown`/
// `topPointerUp`/`topPointerLeave` calls at whatever target hit-testing found, and the processor — vendored
// unmodified — is what is under test here, the same way it is upstream's.
//
// Two things stand between "call `interceptPointerEvent` like upstream's own excluded test does" and a test that
// says anything about this platform:
//
//   1. Upstream's own `PointerEventsProcessorTest.cpp` (excluded from `rnl_core_tests`; see CMakeLists.txt) builds
//      its fixture with `ComponentBuilder`/`Element` and aborts at runtime on a null `ContextContainer`. This file
//      instead reuses `ShadowTreeTestSupport.h`'s `makeTaskDroppingUIManager`/`addRegisteredShadowTree` — the
//      fixture `InputDispatcherTest.cpp` already drives `UIManager::getRelativeLayoutMetrics` through without
//      crashing — because `PointerEventsProcessor::interceptPointerEvent` calls exactly those `UIManager` methods
//      internally (`getShadowTreeRegistry`, `getRelativeLayoutMetrics`, `getNewestCloneOfShadowNode`).
//   2. `RecordingEventDispatcher.h` only records an event's type string, which is enough for
///     `InputDispatcherTest.cpp`'s single-field fixtures but not for a tree of siblings: telling "leave on the old
//      sibling, enter on the new" apart needs the tag each event carries. `RawEvent::shadowNodeFamily` has that tag
//      with no need for a `jsi::Runtime` — the payload closure upstream's real `UIManagerBinding::dispatchEvent`
//      needs one for is not read here — so `taggedRecordingEventDispatcher` below reads it off the same listener
//      hook `RecordingEventDispatcher.h` already uses.
//
// The gap the acceptance table calls out separately — moving from a view onto the empty background never
// produces `pointerOut` — cannot be reproduced by this file: it is specific to `UIManager::startEmptySurface`,
// which gives the root shadow node no instance handle, so `PointerEventsProcessor::getShadowNodeFromEventTarget`
// returns null on the real JS thread and the processor's hover tracking never runs for it — `docs/cpp-toolchain.md`
// (*A root instance handle*) names both the cause and its owner ("belongs with React Native's JavaScript surface
// registry rather than here"). A `ShadowTree` registered the ordinary way, as this file's fixture does, gives the
// root a working emitter and does not hit the gap at all. `e2e/hover-chain.json`'s `onto-empty-root` steps pin it
// instead, against the real `hello_react` surface `FabricHost` boots through `startEmptySurface`.

namespace {

using facebook::react::ContextContainer;
using facebook::react::PointerEvent;
using facebook::react::PointerEventsProcessor;
using facebook::react::RawEvent;
using facebook::react::ReactEventPriority;
using facebook::react::ShadowNode;
using facebook::react::Tag;
using facebook::react::ViewComponentDescriptor;
using facebook::react::ViewEvents;
using react_native_linux::InputDispatcher;
using react_native_linux::makeConfiguredShadowNode;

constexpr Tag kSiblingLeftTag = 30;
constexpr Tag kSiblingRightTag = 31;
constexpr Tag kNestedChildTag = 32;

using ChildList = std::vector<std::shared_ptr<const ShadowNode>>;

/** One entry of the ordered `(tag, eventType)` trace a hover-chain assertion reads. */
struct RecordedPointerEvent {
    Tag tag;
    std::string type;

    bool operator==(const RecordedPointerEvent&) const = default;
};

std::vector<RecordedPointerEvent> traceOf(const std::vector<std::string>& printable) {
    std::vector<RecordedPointerEvent> trace;

    for (const std::string& entry : printable) {
        const size_t separator = entry.find(':');

        trace.push_back(
            RecordedPointerEvent{.tag = std::stoi(entry.substr(0, separator)), .type = entry.substr(separator + 1)});
    }

    return trace;
}

/**
 * A `RecordingEventDispatcher`-shaped listener that also records the tag `RawEvent::shadowNodeFamily` names, as
 * `tag:type` strings a test can build a `RecordedPointerEvent` table from without a second, tag-shaped recording
 * type this file would be the only user of.
 */
std::shared_ptr<const facebook::react::EventDispatcher>
makeTaggedRecordingEventDispatcher(const std::shared_ptr<std::vector<std::string>>& recorded) {
    const facebook::react::EventQueueProcessor eventQueueProcessor{
        [](facebook::jsi::Runtime& /*runtime*/, facebook::react::EventTarget* /*eventTarget*/,
           const std::string& /*type*/, ReactEventPriority /*priority*/,
           const facebook::react::EventPayload& /*payload*/, facebook::react::HighResTimeStamp /*eventTimestamp*/) {},
        [](facebook::jsi::Runtime& /*runtime*/) {}, [](const facebook::react::StateUpdate& /*stateUpdate*/) {},
        std::weak_ptr<facebook::react::EventLogger>{}};
    const std::shared_ptr<const facebook::react::EventDispatcher> eventDispatcher =
        std::make_shared<const facebook::react::EventDispatcher>(
            eventQueueProcessor,
            std::make_unique<facebook::react::EventBeat>(std::make_shared<facebook::react::EventBeat::OwnerBox>(),
                                                         react_native_linux::noopRuntimeScheduler()),
            [](const facebook::react::StateUpdate& /*stateUpdate*/) {}, std::weak_ptr<facebook::react::EventLogger>{});

    eventDispatcher->addListener(
        std::make_shared<const facebook::react::EventListener>([recorded](const RawEvent& event) {
            const std::shared_ptr<const facebook::react::ShadowNodeFamily> family = event.shadowNodeFamily.lock();
            const Tag tag = family != nullptr ? family->getTag() : Tag{0};

            recorded->push_back(std::to_string(tag) + ":" + event.type);

            return true;
        }));

    return eventDispatcher;
}

/**
 * Two siblings side by side — `kSiblingLeftTag` at (0, 0), `kSiblingRightTag` at (100, 0), both 100x100 — the
 * second holding `kNestedChildTag`, a 30x30 box at its own (10, 10). Every node listens for every pointer event,
 * matching upstream's own `listenToAllPointerEvents` fixture, so `shouldEmitPointerEvent` never filters a step
 * this table asserts on.
 */
class HoverChainTest : public ::testing::Test {
protected:
    static constexpr facebook::react::SurfaceId kSurfaceId = 1;

    void SetUp() override {
        uiManager_ = react_native_linux::makeTaskDroppingUIManager(contextContainer_);
        shadowTree_ = react_native_linux::addRegisteredShadowTree(*uiManager_, shadowTreeDelegate_, *contextContainer_,
                                                                  kSurfaceId);

        react_native_linux::commitChildren(
            *shadowTree_,
            std::make_shared<const ChildList>(ChildList{
                makeSibling(kSiblingLeftTag, 0), makeSiblingWithNestedChild(kSiblingRightTag, 100, kNestedChildTag)}));

        dispatcher_ = std::make_unique<InputDispatcher>(uiManager_, mountingManager_, kSurfaceId);
    }

    void TearDown() override { react_native_linux::removeShadowTree(*uiManager_, kSurfaceId); }

    /** Every raw event `InputDispatcher::dispatch` produced across every node, tag-tagged, oldest first. */
    std::vector<RecordedPointerEvent> drain() {
        const std::vector<RecordedPointerEvent> trace = traceOf(*recorded_);

        recorded_->clear();

        return trace;
    }

    std::unique_ptr<InputDispatcher> dispatcher_;
    std::shared_ptr<facebook::react::UIManager> uiManager_;
    std::shared_ptr<react_native_linux::LinuxMountingManager> mountingManager_{
        std::make_shared<react_native_linux::LinuxMountingManager>()};
    facebook::react::ShadowTree* shadowTree_{nullptr};

private:
    /** A hover-listening, absolutely positioned box, optionally holding one already-built child. */
    std::shared_ptr<const ShadowNode>
    makeBox(Tag tag, float left, float top, float width, float height,
            std::shared_ptr<const ChildList> children = std::make_shared<const ChildList>()) {
        folly::dynamic props = folly::dynamic::object("position", "absolute")("left", left)("top", top)("width", width)(
            "height", height)("onPointerEnter", true)("onPointerMove", true)("onPointerLeave", true)(
            "onPointerOver", true)("onPointerOut", true);

        return makeConfiguredShadowNode(fieldDescriptor_, tag, kSurfaceId, contextContainer_, std::move(props),
                                        std::move(children));
    }

    std::shared_ptr<const ShadowNode> makeSibling(Tag tag, float left) { return makeBox(tag, left, 0, 100, 100); }

    std::shared_ptr<const ShadowNode> makeSiblingWithNestedChild(Tag tag, float left, Tag childTag) {
        return makeBox(tag, left, 0, 100, 100,
                       std::make_shared<const ChildList>(ChildList{makeBox(childTag, 10, 10, 30, 30)}));
    }

    react_native_linux::PassThroughShadowTreeDelegate shadowTreeDelegate_;
    std::shared_ptr<const ContextContainer> contextContainer_{std::make_shared<ContextContainer>()};
    std::shared_ptr<std::vector<std::string>> recorded_{std::make_shared<std::vector<std::string>>()};
    std::shared_ptr<const facebook::react::EventDispatcher> eventDispatcher_{
        makeTaggedRecordingEventDispatcher(recorded_)};
    ViewComponentDescriptor fieldDescriptor_{facebook::react::ComponentDescriptorParameters{
        .eventDispatcher = eventDispatcher_, .contextContainer = contextContainer_, .flavor = nullptr}};
};

// Issue #36, case 1: crossing from one sibling straight into the one beside it is one motion event, and the raw
// trace `InputDispatcher::dispatch` produces for it is a single `topPointerMove` at whichever sibling the point
// now lands on — hit-testing does not itself synthesize a leave on the one the pointer left, because
// `PointerEventsProcessor` is what notices the target changed between two raw moves and is what this router's own
// docblock says owns that job.
TEST_F(HoverChainTest, CrossingIntoTheAdjacentSiblingIsOneRawMoveAtTheNewTarget) {
    dispatcher_->dispatch({react_native_linux::InputEvent{.kind = react_native_linux::InputEventKind::PointerMotion,
                                                          .surfacePoint = {.x = 50, .y = 50}}});

    EXPECT_EQ(drain(), (std::vector<RecordedPointerEvent>{{kSiblingLeftTag, "topPointerMove"}}));

    dispatcher_->dispatch({react_native_linux::InputEvent{.kind = react_native_linux::InputEventKind::PointerMotion,
                                                          .surfacePoint = {.x = 150, .y = 50}}});

    EXPECT_EQ(drain(), (std::vector<RecordedPointerEvent>{{kSiblingRightTag, "topPointerMove"}}));
}

// Issue #36, case 2: moving into the nested child inside the right sibling raw-targets the child, not its parent
// — the parent's own `pointerOver`/`pointerEnter` (bubbled or synthesized by `PointerEventsProcessor` from the
// ancestor chain) is that processor's job, not a second raw move this dispatcher would have to invent.
TEST_F(HoverChainTest, MovingIntoTheNestedChildRawTargetsTheChildItself) {
    dispatcher_->dispatch({react_native_linux::InputEvent{.kind = react_native_linux::InputEventKind::PointerMotion,
                                                          .surfacePoint = {.x = 120, .y = 20}}});

    EXPECT_EQ(drain(), (std::vector<RecordedPointerEvent>{{kNestedChildTag, "topPointerMove"}}));
}

// Issue #36, case 3 (the documented, open gap) is not provable at this layer: this fixture registers its
// `ShadowTree` the ordinary way, so its root has a working emitter and the gap does not reproduce here at all —
// `UIManager::startEmptySurface`, the path `FabricHost` actually starts a surface through, is what gives the root
// shadow node no instance handle (docs/cpp-toolchain.md, *A root instance handle*), and that is a property of
// surface bootstrap this dispatcher-and-shadow-tree-only fixture never touches. `e2e/hover-chain.json`'s
// `onto-empty-root` steps are where the real gap is pinned, against the real `hello_react` surface.

/**
 * The upstream `PointerEventsProcessor` half of the table: given the raw targets a real hit test resolves (proved
 * above), the vendored, unmodified processor is what is under test — reused exactly as production does, per this
 * file's own header comment. `dispatchPointerEvent` mirrors upstream's own excluded `PointerEventsProcessorTest`
 * helper of the same name.
 */
class HoverChainProcessorTest : public HoverChainTest {
protected:
    std::vector<RecordedPointerEvent> dispatchPointerEventOnTag(Tag targetTag, const std::string& type) {
        std::shared_ptr<const ShadowNode> root;

        uiManager_->getShadowTreeRegistry().visit(kSurfaceId, [&root](const facebook::react::ShadowTree& tree) {
            root = tree.getCurrentRevision().rootShadowNode;
        });

        const std::shared_ptr<const ShadowNode> target = findByTag(root, targetTag);
        std::vector<RecordedPointerEvent> trace;
        PointerEvent payload{};

        payload.pointerId = 1;

        processor_.interceptPointerEvent(
            target, type, ReactEventPriority::Default, payload,
            [&trace](const ShadowNode& targetNode, const std::string& eventType, ReactEventPriority /*priority*/,
                     const facebook::react::EventPayload& /*payload*/) {
                trace.push_back(RecordedPointerEvent{.tag = targetNode.getTag(), .type = eventType});
            },
            *uiManager_);

        return trace;
    }

    static std::shared_ptr<const ShadowNode> findByTag(const std::shared_ptr<const ShadowNode>& node, Tag tag) {
        if (node == nullptr) {
            return nullptr;
        }

        if (node->getTag() == tag) {
            return node;
        }

        for (const std::shared_ptr<const ShadowNode>& child : node->getChildren()) {
            const std::shared_ptr<const ShadowNode> found = findByTag(child, tag);

            if (found != nullptr) {
                return found;
            }
        }

        return nullptr;
    }

    PointerEventsProcessor processor_;
};

// Issue #36, case 1 and case 2 together, through the real processor: a move onto the left sibling, then onto the
// nested child inside the right sibling, produces `pointerLeave` on the old target ahead of `pointerEnter` on the
// new one, and the child's own `pointerOver`/`pointerOut` bubble while its `pointerEnter`/`pointerLeave` — and its
// parent's — do not go further than each node's own call.
TEST_F(HoverChainProcessorTest, LeaveOnTheOldSiblingOrdersAheadOfEnterOnTheNew) {
    const std::vector<RecordedPointerEvent> intoLeft = dispatchPointerEventOnTag(kSiblingLeftTag, "topPointerMove");

    // The first ever target: `pointerEnter` fires for every ancestor between the root and the sibling that is
    // itself listening for it, or capture-listened to from above — this fixture's root is a plain, unconfigured
    // `RootProps` the way a real app's usually is, so it does not appear here at all.
    ASSERT_EQ(intoLeft.size(), 3U);
    EXPECT_EQ(intoLeft[0], (RecordedPointerEvent{kSiblingLeftTag, "topPointerOver"}));
    EXPECT_EQ(intoLeft[1], (RecordedPointerEvent{kSiblingLeftTag, "topPointerEnter"}));
    EXPECT_EQ(intoLeft[2], (RecordedPointerEvent{kSiblingLeftTag, "topPointerMove"}));

    const std::vector<RecordedPointerEvent> intoNestedChild =
        dispatchPointerEventOnTag(kNestedChildTag, "topPointerMove");

    ASSERT_EQ(intoNestedChild.size(), 6U);
    EXPECT_EQ(intoNestedChild[0], (RecordedPointerEvent{kSiblingLeftTag, "topPointerOut"}));
    EXPECT_EQ(intoNestedChild[1], (RecordedPointerEvent{kSiblingLeftTag, "topPointerLeave"}));
    EXPECT_EQ(intoNestedChild[2], (RecordedPointerEvent{kNestedChildTag, "topPointerOver"}));
    EXPECT_EQ(intoNestedChild[3], (RecordedPointerEvent{kSiblingRightTag, "topPointerEnter"}));
    EXPECT_EQ(intoNestedChild[4], (RecordedPointerEvent{kNestedChildTag, "topPointerEnter"}));
    EXPECT_EQ(intoNestedChild[5], (RecordedPointerEvent{kNestedChildTag, "topPointerMove"}));
}

// A second move at the same target produces no derivative enter/leave/over/out at all — the chain has not
// changed, so the processor has nothing new to say beyond the move itself.
TEST_F(HoverChainProcessorTest, ASecondMoveAtTheSameTargetProducesOnlyTheMove) {
    dispatchPointerEventOnTag(kSiblingLeftTag, "topPointerMove");

    const std::vector<RecordedPointerEvent> secondMove = dispatchPointerEventOnTag(kSiblingLeftTag, "topPointerMove");

    EXPECT_EQ(secondMove, (std::vector<RecordedPointerEvent>{{kSiblingLeftTag, "topPointerMove"}}));
}

} // namespace
