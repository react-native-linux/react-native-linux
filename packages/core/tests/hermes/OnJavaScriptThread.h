#pragma once

#include "ReactHost.h"

#include <functional>
#include <future>
#include <jsi/jsi.h>

namespace react_native_linux {

/** Runs `work` on `reactHost`'s JavaScript thread, through its buffered executor, and returns what it returned. */
template <typename Result>
Result onJavaScriptThread(ReactHost& reactHost, std::function<Result(facebook::jsi::Runtime&)> work) {
    std::promise<Result> result;

    reactHost.reactInstance().getBufferedRuntimeExecutor()(
        [&result, &work](facebook::jsi::Runtime& runtime) { result.set_value(work(runtime)); });

    return result.get_future().get();
}

} // namespace react_native_linux
