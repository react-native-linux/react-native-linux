#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <react/renderer/core/EventBeat.h>
#include <react/renderer/core/EventDispatcher.h>
#include <react/renderer/core/EventListener.h>
#include <react/renderer/core/EventQueueProcessor.h>
#include <react/renderer/core/RawEvent.h>
#include <react/renderer/runtimescheduler/RuntimeScheduler.h>

namespace react_native_linux {

/**
 * A `RuntimeScheduler` that never runs anything: `EventBeat`'s constructor needs a live one by reference, and no
 * test that records through the dispatcher below ever asks for a beat — the listener answers `dispatchEvent`
 * synchronously and reports the event handled, which returns before the queue behind it is touched. One instance
 * for every caller, for the same reason it only has to outlive the dispatchers: it never does anything.
 */
inline facebook::react::RuntimeScheduler& noopRuntimeScheduler() {
    static facebook::react::RuntimeScheduler runtimeScheduler{
        [](const std::function<void(facebook::jsi::Runtime&)>& /*callback*/) {}};

    return runtimeScheduler;
}

/**
 * A live `EventDispatcher` whose queue goes nowhere, carrying one listener that sees every `RawEvent` the
 * emitters under test produce. Returning `true` from the listener reports the event handled, which stops it
 * before the queue; returning `false` lets it through to a queue that drops it either way.
 *
 * The construction is here rather than in each test file because the processor, the beat and the listener
 * together are a jscpd clone at threshold 0 — the same reason `makeRecordingEventDispatcher` below is shared.
 * What a test varies is only what it reads off the event, which is the listener it passes in.
 *
 * The caller keeps the returned dispatcher alive: an `EventEmitter` holds only a weak reference to it.
 */
inline std::shared_ptr<const facebook::react::EventDispatcher>
makeListeningEventDispatcher(std::function<bool(const facebook::react::RawEvent&)> listener) {
    const facebook::react::EventQueueProcessor eventQueueProcessor{
        [](facebook::jsi::Runtime& /*runtime*/, facebook::react::EventTarget* /*eventTarget*/,
           const std::string& /*type*/, facebook::react::ReactEventPriority /*priority*/,
           const facebook::react::EventPayload& /*payload*/, facebook::react::HighResTimeStamp /*eventTimestamp*/) {},
        [](facebook::jsi::Runtime& /*runtime*/) {}, [](const facebook::react::StateUpdate& /*stateUpdate*/) {},
        std::weak_ptr<facebook::react::EventLogger>{}};
    const std::shared_ptr<const facebook::react::EventDispatcher> eventDispatcher =
        std::make_shared<const facebook::react::EventDispatcher>(
            eventQueueProcessor,
            std::make_unique<facebook::react::EventBeat>(std::make_shared<facebook::react::EventBeat::OwnerBox>(),
                                                         noopRuntimeScheduler()),
            [](const facebook::react::StateUpdate& /*stateUpdate*/) {}, std::weak_ptr<facebook::react::EventLogger>{});

    eventDispatcher->addListener(std::make_shared<const facebook::react::EventListener>(std::move(listener)));

    return eventDispatcher;
}

/**
 * A live `EventDispatcher` that appends the normalized type of every event dispatched through it to
 * `recordedTypes` and carries it no further. The type is the whole of what a test can read without a JS runtime —
 * every payload upstream builds is a `jsi::Value` closure — and it is what distinguishes "the emitter fired" from
 * "the emitter was never wired at all", which a null dispatcher cannot.
 */
inline std::shared_ptr<const facebook::react::EventDispatcher>
makeRecordingEventDispatcher(const std::shared_ptr<std::vector<std::string>>& recordedTypes) {
    return makeListeningEventDispatcher([recordedTypes](const facebook::react::RawEvent& event) {
        recordedTypes->push_back(event.type);

        return true;
    });
}

} // namespace react_native_linux
