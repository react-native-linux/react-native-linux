#include "ReanimatedPlatform.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace react_native_linux {

namespace {

[[noreturn]] void throwUnsupported(std::string_view member) {
    throw std::runtime_error("[Reanimated] " + std::string(member) + " is not supported on Linux yet.");
}

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
        .registerSensor = [](int, int, int, std::function<void(double[], int)>) -> int {
            throwUnsupported("registerSensor");
        },
        .unregisterSensor = [](int) { throwUnsupported("unregisterSensor"); },
        .setGestureStateFunction = [](int, int) { throwUnsupported("setGestureState"); },
        .subscribeForKeyboardEvents = [](std::function<void(int, int)>, bool, bool) -> int {
            throwUnsupported("subscribeForKeyboardEvents");
        },
        .unsubscribeFromKeyboardEvents = [](int) { throwUnsupported("unsubscribeFromKeyboardEvents"); },
        .maybeFlushUIUpdatesQueueFunction = [&frames] { frames.request([](double) {}); },
        .attachPseudoSelector = [](facebook::react::Tag, reanimated::PseudoSelector,
                                   std::function<void(bool)>) { throwUnsupported("attachPseudoSelector"); },
        .detachPseudoSelector = [](facebook::react::Tag,
                                   reanimated::PseudoSelector) { throwUnsupported("detachPseudoSelector"); },
        .cssCanRouteProperty = {},
        .cssApplyTransition = {},
        .cssRemoveTransition = {},
        .cssGetPlatformValue = {},
        .platformAnimationFactory = nullptr,
    };
}

} // namespace react_native_linux
