#include "ExpoFetch.h"

#include "CurlHttpClient.h"

#include <algorithm>
#include <array>
#include <folly/io/IOBuf.h>
#include <string_view>
#include <utility>

namespace react_native_linux {

namespace http = facebook::react::http;

namespace {

constexpr std::string_view kFileScheme = "file://";
constexpr char kCanceledMessage[] = "Fetch request has been canceled";
constexpr char kRedirectMessage[] = "Redirect is not allowed when redirect mode is 'error'";
constexpr int kFileFoundStatus = 200;
constexpr int kFileNotFoundStatus = 404;
// OkHttp's `Response.isRedirect`, which upstream's Android module tests a response against.
constexpr std::array<int, 6> kRedirectStatuses{300, 301, 302, 303, 307, 308};
// Upstream's Android module sends these with an empty body rather than none, as OkHttp requires one.
constexpr std::array<std::string_view, 3> kMethodsRequiringBody{"POST", "PUT", "PATCH"};

} // namespace

ExpoFetchResponse::ExpoFetchResponse(Post post) : post_(std::move(post)) {}

void ExpoFetchResponse::setEvents(FetchResponseEvents events) { events_ = std::move(events); }

void ExpoFetchResponse::start(CurlHttpClient& client, const FetchRequest& request, Resolve resolve, Reject reject) {
    redirect_ = request.redirect;
    isFile_ = request.url.starts_with(kFileScheme);
    head_.url = request.url;
    waitFor([this, resolve = std::move(resolve), reject = std::move(reject)] {
        if (phase_ == Phase::ResponseReceived) {
            resolve();
        } else if (phase_ == Phase::ErrorReceived) {
            reject(error_);
        }

        return phase_ == Phase::ResponseReceived || phase_ == Phase::ErrorReceived;
    });
    moveTo(Phase::Started);

    const auto onJavaScriptThread = [self = weak_from_this(),
                                     post = post_](std::function<void(ExpoFetchResponse&)> work) {
        post([self, work = std::move(work)] {
            if (const std::shared_ptr<ExpoFetchResponse> response = self.lock()) {
                work(*response);
            }
        });
    };
    http::NetworkCallbacks callbacks;

    callbacks.sendIncrementalUpdates = true;
    callbacks.onResponse = [onJavaScriptThread](uint16_t status, http::Headers headers) {
        onJavaScriptThread([status, headers = std::move(headers)](ExpoFetchResponse& response) mutable {
            response.receiveResponse(status, std::move(headers));
        });
    };
    callbacks.onBodyIncremental = [onJavaScriptThread](int64_t progress, int64_t /*total*/,
                                                       std::unique_ptr<folly::IOBuf> chunk) {
        onJavaScriptThread([bytes = chunk->moveToFbString().toStdString()](ExpoFetchResponse& response) mutable {
            response.receiveData(std::move(bytes));
        });

        return progress;
    };
    callbacks.onResponseComplete = [onJavaScriptThread](std::string error, bool /*timeoutError*/) {
        onJavaScriptThread([error = std::move(error)](ExpoFetchResponse& response) { response.complete(error); });
    };

    http::Body body;
    body.string = request.body.has_value() ||
                          std::ranges::find(kMethodsRequiringBody, request.method) == kMethodsRequiringBody.end()
                      ? request.body
                      : std::optional<std::string>("");
    token_ = client.sendFetchRequest(
        std::move(callbacks),
        [onJavaScriptThread](std::string url, bool redirected) {
            onJavaScriptThread([url = std::move(url), redirected](ExpoFetchResponse& response) {
                response.head_.url = url;
                response.head_.redirected = redirected;
            });
        },
        request.redirect == FetchRedirect::Follow, request.method, request.url, request.headers, body);
}

void ExpoFetchResponse::cancel() {
    if (phase_ == Phase::Initialized || phase_ == Phase::BodyCompleted || phase_ == Phase::ErrorReceived) {
        return;
    }

    token_->cancel();
    fail(kCanceledMessage);
}

std::optional<std::string> ExpoFetchResponse::startStreaming() {
    if (phase_ == Phase::BodyCompleted) {
        return std::exchange(body_, {});
    }

    if (phase_ == Phase::ResponseReceived) {
        moveTo(Phase::BodyStreamingStarted);

        if (!body_.empty()) {
            events_.didReceiveResponseData(std::exchange(body_, {}));
        }
    }

    return std::nullopt;
}

void ExpoFetchResponse::cancelStreaming() {
    if (phase_ == Phase::BodyStreamingStarted) {
        moveTo(Phase::BodyStreamingCanceled);
    }
}

void ExpoFetchResponse::readBody(ResolveBody resolve, Reject reject) {
    waitFor([this, resolve = std::move(resolve), reject = std::move(reject)] {
        if (phase_ == Phase::BodyCompleted) {
            resolve(std::exchange(body_, {}));
        } else if (phase_ == Phase::ErrorReceived) {
            reject(error_);
        }

        return phase_ == Phase::BodyCompleted || phase_ == Phase::ErrorReceived;
    });
}

void ExpoFetchResponse::waitFor(Waiter waiter) {
    if (!waiter()) {
        waiters_.push_back(std::move(waiter));
    }
}

void ExpoFetchResponse::moveTo(Phase phase) {
    phase_ = phase;
    std::erase_if(waiters_, [](const Waiter& waiter) { return waiter(); });
}

void ExpoFetchResponse::receiveResponse(uint16_t status, http::Headers headers) {
    if (phase_ != Phase::Started) {
        return;
    }

    if (redirect_ == FetchRedirect::Error && std::ranges::find(kRedirectStatuses, status) != kRedirectStatuses.end()) {
        token_->cancel();
        fail(kRedirectMessage);
        return;
    }

    head_.status = isFile_ ? kFileFoundStatus : status;
    head_.statusText = isFile_ ? "OK" : "";
    head_.headers = std::move(headers);
    moveTo(Phase::ResponseReceived);
}

void ExpoFetchResponse::receiveData(std::string bytes) {
    if (phase_ == Phase::ResponseReceived) {
        body_ += bytes;
    } else if (phase_ == Phase::BodyStreamingStarted) {
        events_.didReceiveResponseData(std::move(bytes));
    }
}

void ExpoFetchResponse::complete(const std::string& error) {
    if (phase_ == Phase::ErrorReceived) {
        return;
    }

    if (phase_ == Phase::Started && isFile_) {
        head_.status = kFileNotFoundStatus;
        head_.statusText = "File not found";
        moveTo(Phase::ResponseReceived);
    } else if (!error.empty()) {
        fail(error);
        return;
    }

    if (phase_ == Phase::BodyStreamingStarted) {
        events_.didComplete();
    }

    moveTo(Phase::BodyCompleted);
    events_.readyForJSFinalization();
}

void ExpoFetchResponse::fail(const std::string& message) {
    if (phase_ == Phase::BodyStreamingStarted) {
        events_.didFailWithError(message);
    }

    error_ = message;
    moveTo(Phase::ErrorReceived);
    events_.readyForJSFinalization();
}

} // namespace react_native_linux
