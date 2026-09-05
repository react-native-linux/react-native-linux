#include "AnimationFrameQueue.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

namespace react_native_linux {

uint64_t AnimationFrameQueue::request(Callback callback) {
    const std::lock_guard<std::mutex> guard(mutex_);
    const uint64_t requestHandle = nextHandle_;

    ++nextHandle_;
    pendingRequests_.push_back(Request{.handle = requestHandle, .callback = std::move(callback)});

    return requestHandle;
}

void AnimationFrameQueue::cancel(uint64_t requestHandle) {
    const std::lock_guard<std::mutex> guard(mutex_);

    std::erase_if(pendingRequests_,
                  [requestHandle](const Request& request) { return request.handle == requestHandle; });

    for (Request& request : dispatchingRequests_) {
        if (request.handle == requestHandle) {
            request.callback = nullptr;
        }
    }
}

bool AnimationFrameQueue::hasPendingRequests() const {
    const std::lock_guard<std::mutex> guard(mutex_);

    return !pendingRequests_.empty() || isDispatching_;
}

void AnimationFrameQueue::clear() {
    const std::lock_guard<std::mutex> guard(mutex_);

    pendingRequests_.clear();
    dispatchingRequests_.clear();
}

size_t AnimationFrameQueue::dispatchFrame(double frameTimestampMilliseconds) {
    std::unique_lock<std::mutex> lock(mutex_);

    dispatchingRequests_.swap(pendingRequests_);
    isDispatching_ = true;

    size_t dispatchedCount = 0;

    // The size is re-read every iteration under the lock, and a cancelled entry is emptied rather than erased, so
    // a callback that registers or cancels while this loop is unlocked can never move the entries behind it.
    for (size_t index = 0; index < dispatchingRequests_.size(); ++index) {
        const Callback callback = dispatchingRequests_[index].callback;

        if (!callback) {
            continue;
        }

        lock.unlock();
        callback(frameTimestampMilliseconds);
        lock.lock();

        ++dispatchedCount;
    }

    dispatchingRequests_.clear();
    isDispatching_ = false;

    return dispatchedCount;
}

} // namespace react_native_linux
