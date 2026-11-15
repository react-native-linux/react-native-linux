#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace react_native_linux {

/**
 * One thread that runs posted tasks in order, for a module whose asynchronous methods must not do their work on the
 * JavaScript thread: `ExpoSQLite`'s `SQLiteDatabases` and `FileSystem`'s `FileSystemHost`.
 *
 * Threading contract: `post` may be called from any thread and never waits on a task. Tasks run on the one thread
 * this object owns, which the first `post` starts, in the order they were posted, and must not throw. The destructor
 * runs every task already posted, so a queued write still lands, then joins the thread if one started.
 */
class SerialWorker final {
public:
    using Task = std::function<void()>;

    SerialWorker() = default;
    SerialWorker(const SerialWorker&) = delete;
    SerialWorker(SerialWorker&&) = delete;
    SerialWorker& operator=(const SerialWorker&) = delete;
    SerialWorker& operator=(SerialWorker&&) = delete;
    ~SerialWorker() noexcept;

    void post(Task task);

private:
    void runTasks();

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Task> tasks_;
    bool stopping_{false};
    std::thread thread_;
};

} // namespace react_native_linux
