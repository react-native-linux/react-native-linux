#include "LinuxAnimationChoreographer.h"

#include <chrono>

namespace react_native_linux {

void LinuxAnimationChoreographer::resume() { isActive_.store(true); }

void LinuxAnimationChoreographer::pause() { isActive_.store(false); }

bool LinuxAnimationChoreographer::isActive() const noexcept { return isActive_.load(); }

void LinuxAnimationChoreographer::tick(std::chrono::steady_clock::time_point now) {
    if (!isActive_.load()) {
        return;
    }

    onAnimationFrame(facebook::react::AnimationTimestamp(now.time_since_epoch()));
}

} // namespace react_native_linux
