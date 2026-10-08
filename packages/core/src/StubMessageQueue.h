#pragma once

#include <cxxreact/MessageQueueThread.h>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>

namespace react_native_linux {

/**
 * The JavaScript queue of an itest run (`hello_react --fantom`, #210): a `MessageQueueThread` with no thread of its
 * own, whose tasks run only when the thread that owns it calls `flush`. It is upstream Fantom's `StubQueue`
 * (`private/react-native-fantom/tester/src/stubs/StubQueue.h`), and it is what lets
 * `NativeFantom.flushMessageQueue` run every task queued so far from inside a JavaScript call: the call is already
 * on the owning thread, so the tasks run there, re-entrantly, before it returns.
 *
 * Threading contract: `flush`, `runOnQueueSync` and `quitSynchronous` are called on the owning thread, the one the
 * runtime runs on. `runOnQueue` may be called from any thread, because the timer registry's dispatch thread and the
 * image decode worker post to it, so the queue is guarded by a mutex that is never held while a task runs. A task
 * posted after `quitSynchronous` is dropped at once rather than kept, so nothing that holds a JSI value can outlive
 * the runtime inside the queue. `StubMessageQueueTest` proves it, the cross-thread post under TSan.
 */
class StubMessageQueue final : public facebook::react::MessageQueueThread {
public:
    void runOnQueue(std::function<void()>&& task) override;
    void runOnQueueSync(std::function<void()>&& task) override;
    void quitSynchronous() override;

    /** Runs queued tasks, including the ones they queue, until none is left. */
    void flush();

private:
    /** The oldest queued task, taken under the mutex so it runs without it; nothing once the queue is empty. */
    std::optional<std::function<void()>> takeNextTask();

    std::mutex mutex_;
    std::deque<std::function<void()>> tasks_;
    bool hasQuit_{false};
};

} // namespace react_native_linux
