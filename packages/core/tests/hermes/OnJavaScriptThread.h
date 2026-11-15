#pragma once

#include "ReactHost.h"

#include <chrono>
#include <functional>
#include <future>
#include <jsi/jsi.h>
#include <string>
#include <thread>

namespace react_native_linux {

/** Runs `work` on `reactHost`'s JavaScript thread, through its buffered executor, and returns what it returned. */
template <typename Result>
Result onJavaScriptThread(ReactHost& reactHost, std::function<Result(facebook::jsi::Runtime&)> work) {
    std::promise<Result> result;

    reactHost.reactInstance().getBufferedRuntimeExecutor()(
        [&result, &work](facebook::jsi::Runtime& runtime) { result.set_value(work(runtime)); });

    return result.get_future().get();
}

/**
 * Pumps `reactHost` until the JavaScript global `name` is a string, for a probe whose work settles off the
 * JavaScript thread, and returns it, or `the probe never finished` after five seconds.
 */
inline std::string awaitGlobalString(ReactHost& reactHost, const std::string& name) {
    constexpr std::chrono::milliseconds kBudget{5000};
    constexpr std::chrono::milliseconds kPollInterval{10};
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kBudget;

    while (std::chrono::steady_clock::now() < deadline) {
        reactHost.runUntilQuiescent(kBudget);

        const std::string value = onJavaScriptThread<std::string>(reactHost, [&name](facebook::jsi::Runtime& runtime) {
            const facebook::jsi::Value global = runtime.global().getProperty(runtime, name.c_str());

            return global.isString() ? global.getString(runtime).utf8(runtime) : std::string();
        });

        if (!value.empty()) {
            return value;
        }

        std::this_thread::sleep_for(kPollInterval);
    }

    return "the probe never finished";
}

} // namespace react_native_linux
