#include "StubMessageQueue.h"

#include <functional>
#include <mutex>
#include <optional>
#include <utility>

namespace react_native_linux {

void StubMessageQueue::runOnQueue(std::function<void()>&& task) {
    const std::lock_guard<std::mutex> guard(mutex_);

    if (!hasQuit_) {
        tasks_.push_back(std::move(task));
    }
}

void StubMessageQueue::runOnQueueSync(std::function<void()>&& task) {
    flush();
    task();
}

void StubMessageQueue::quitSynchronous() {
    flush();

    const std::lock_guard<std::mutex> guard(mutex_);

    hasQuit_ = true;
}

void StubMessageQueue::flush() {
    while (std::optional<std::function<void()>> task = takeNextTask()) {
        (*task)();
    }
}

std::optional<std::function<void()>> StubMessageQueue::takeNextTask() {
    const std::lock_guard<std::mutex> guard(mutex_);

    if (tasks_.empty()) {
        return std::nullopt;
    }

    std::function<void()> task = std::move(tasks_.front());

    tasks_.pop_front();

    return task;
}

} // namespace react_native_linux
