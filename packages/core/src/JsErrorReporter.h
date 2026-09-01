#pragma once

#include <jserrorhandler/JsErrorHandler.h>

#include <atomic>
#include <memory>

namespace react_native_linux {

/**
 * Threading contract: the handler returned by createHandler runs on the JS thread, while hasReportedFatalError is
 * read by the thread that owns the process run loop. The flag is shared by value so the handler stays valid after
 * the reporter goes out of scope, which is what JsErrorHandler requires of an OnJsError callback.
 */
class JsErrorReporter final {
public:
    facebook::react::JsErrorHandler::OnJsError createHandler() const;
    bool hasReportedFatalError() const;

private:
    std::shared_ptr<std::atomic<bool>> hasReportedFatalError_{std::make_shared<std::atomic<bool>>(false)};
};

} // namespace react_native_linux
