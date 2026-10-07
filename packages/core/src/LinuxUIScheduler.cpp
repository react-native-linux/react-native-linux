#include "LinuxUIScheduler.h"

#include <utility>

namespace react_native_linux {

void LinuxUIScheduler::scheduleOnUI(std::function<void()> job) {
    if (isOnUIThread()) {
        job();

        return;
    }

    worklets::UIScheduler::scheduleOnUI(std::move(job));
}

bool LinuxUIScheduler::queryIsOnUIThread() const { return std::this_thread::get_id() == frameThread_; }

} // namespace react_native_linux
