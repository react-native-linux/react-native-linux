#include "SerialWorker.h"

#include <utility>

namespace react_native_linux {

SerialWorker::~SerialWorker() noexcept {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }

    wake_.notify_one();

    if (thread_.joinable()) {
        thread_.join();
    }
}

void SerialWorker::post(Task task) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push_back(std::move(task));

        if (!thread_.joinable()) {
            thread_ = std::thread([this] { runTasks(); });
        }
    }

    wake_.notify_one();
}

void SerialWorker::runTasks() {
    std::unique_lock<std::mutex> lock(mutex_);

    while (true) {
        wake_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });

        if (tasks_.empty()) {
            return;
        }

        const Task task = std::move(tasks_.front());

        tasks_.pop_front();
        lock.unlock();
        task();
        lock.lock();
    }
}

} // namespace react_native_linux
