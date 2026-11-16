#include "ReanimatedPlatform.h"

#include <chrono>
#include <stdexcept>
#include <utility>

namespace react_native_linux {

namespace {

constexpr int kUnavailable = -1;

} // namespace

reanimated::PlatformDepMethodsHolder
makePlatformDepMethodsHolder(AnimationFrameQueue& frames,
                             std::shared_ptr<facebook::react::IMountingManager> mountingManager) {
    return reanimated::PlatformDepMethodsHolder{
        .requestRender = [&frames](std::function<void(const double)> onRender) { frames.request(std::move(onRender)); },
        .synchronouslyUpdateUIPropsFunction =
            [mountingManager = std::move(mountingManager)](const int tag, const folly::dynamic& props) {
                mountingManager->synchronouslyUpdateViewOnUIThread(tag, props);
            },
        .getAnimationTimestamp =
            [] {
                return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch())
                    .count();
            },
        .registerSensor = [](int, int, int, std::function<void(double[], int)>) { return kUnavailable; },
        .unregisterSensor = [](int) {},
        .setGestureStateFunction =
            [](int, int) { throw std::runtime_error("[Reanimated] setGestureState is not supported on Linux yet."); },
        .subscribeForKeyboardEvents = [](std::function<void(int, int)>, bool, bool) { return kUnavailable; },
        .unsubscribeFromKeyboardEvents = [](int) {},
        .maybeFlushUIUpdatesQueueFunction = [&frames] { frames.request([](double) {}); },
        .attachPseudoSelector = [](facebook::react::Tag, reanimated::PseudoSelector, std::function<void(bool)>) {},
        .detachPseudoSelector = [](facebook::react::Tag, reanimated::PseudoSelector) {},
        .platformTransitionBackend = nullptr,
        .platformAnimationFactory = nullptr,
    };
}

} // namespace react_native_linux
