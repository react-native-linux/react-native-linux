#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <react/http/IHttpClient.h>

namespace facebook::react {
class CallInvoker;
class TurboModule;
} // namespace facebook::react

namespace react_native_linux {

class CurlHttpClient;

/** The `redirect` of upstream's `NativeRequestInit`. */
enum class FetchRedirect : uint8_t { Follow, Error, Manual };

/** What upstream's `NativeRequest.start(url, requestInit, requestBody)` asks for. */
struct FetchRequest {
    std::string url;
    std::string method;
    facebook::react::http::Headers headers;
    std::optional<std::string> body;
    FetchRedirect redirect{FetchRedirect::Follow};
};

/** What upstream's `NativeResponse` getters read; `status` is -1 until a response arrives, as upstream's is. */
struct FetchResponseHead {
    int status{-1};
    std::string statusText;
    std::string url;
    bool redirected{false};
    facebook::react::http::Headers headers;
};

/** The events upstream's `FetchResponse` listens for on its `NativeResponse`. */
struct FetchResponseEvents {
    std::function<void(std::string bytes)> didReceiveResponseData;
    std::function<void()> didComplete;
    std::function<void(const std::string& message)> didFailWithError;
    std::function<void()> readyForJSFinalization;
};

/**
 * One `expo/fetch` request and its response: the state machine of upstream's iOS and Android `NativeResponse`
 * (#673, ADR-0004). Body bytes that arrive before JavaScript reads the body are kept; `startStreaming` hands them
 * over and then delivers every later chunk as it arrives, or hands over the whole body once it has completed.
 * `cancelStreaming` drops what arrives later; `cancel` stops the transfer and fails the response. A redirect with
 * `redirect: 'error'` fails with upstream's message, and with `'manual'` it is the response. A `file://` URL answers
 * 200, or 404 when it cannot be read, as upstream's Android module answers it. A request has no timeout, and
 * `credentials` is not read: the client's requests share one cookie store.
 *
 * Threading contract: every member runs on the JavaScript thread, and so does every callback and event. `start` hands
 * the request to `client`, whose worker thread hands each of its callbacks to `post`, which must run it later on the
 * JavaScript thread; what the worker holds of this object is a weak reference, so the object is only ever touched,
 * and destroyed, on the JavaScript thread. A callback must not call back into this object before it returns.
 */
class ExpoFetchResponse final : public std::enable_shared_from_this<ExpoFetchResponse> {
public:
    using Post = std::function<void(std::function<void()> work)>;
    using Resolve = std::function<void()>;
    using Reject = std::function<void(const std::string& message)>;
    using ResolveBody = std::function<void(std::string body)>;

    explicit ExpoFetchResponse(Post post);

    void setEvents(FetchResponseEvents events);

    /** Sends `request`; `resolve` runs once the response has arrived, and `reject` once it never will. */
    void start(CurlHttpClient& client, const FetchRequest& request, Resolve resolve, Reject reject);
    void cancel();

    /** The whole body when it has already completed, which only the first call takes; otherwise empty. */
    std::optional<std::string> startStreaming();
    void cancelStreaming();

    /** Settles once the body has completed with what `startStreaming` has not taken, or once it failed. */
    void readBody(ResolveBody resolve, Reject reject);

    [[nodiscard]] const FetchResponseHead& head() const noexcept { return head_; }

private:
    enum class Phase : uint8_t {
        Initialized,
        Started,
        ResponseReceived,
        BodyCompleted,
        BodyStreamingStarted,
        BodyStreamingCanceled,
        ErrorReceived,
    };

    /** Settles something waiting on a phase; true once it has. */
    using Waiter = std::function<bool()>;

    void waitFor(Waiter waiter);
    void moveTo(Phase phase);
    void receiveResponse(uint16_t status, facebook::react::http::Headers headers);
    void receiveData(std::string bytes);
    void complete(const std::string& error);
    void fail(const std::string& message);

    Post post_;
    FetchResponseEvents events_;
    Phase phase_{Phase::Initialized};
    FetchRedirect redirect_{FetchRedirect::Follow};
    bool isFile_{false};
    FetchResponseHead head_;
    std::string body_;
    std::string error_;
    std::vector<Waiter> waiters_;
    std::unique_ptr<facebook::react::http::IRequestToken> token_;
};

/**
 * The `ExpoFetchModule` TurboModule upstream's unmodified `expo/fetch` requires (`src/ExpoFetchModule.cpp`). Its
 * constants are the `NativeRequest` and `NativeResponse` classes, built by `makeSharedObjectClass`.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoFetchModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

} // namespace react_native_linux
