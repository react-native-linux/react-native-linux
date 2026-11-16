#include "CurlHttpClient.h"
#include "ExpoFetch.h"
#include "ExpoFileSystem.h"
#include "ExpoSharedObject.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <react/bridging/Bridging.h>
#include <react/bridging/Promise.h>

namespace react_native_linux {

namespace {

namespace jsi = facebook::jsi;

/** What a `NativeRequest` and the `NativeResponse` it was constructed with both carry. */
struct FetchState final : jsi::NativeState {
    explicit FetchState(std::shared_ptr<ExpoFetchResponse> response) : response(std::move(response)) {}

    std::shared_ptr<ExpoFetchResponse> response;
};

/** What `arrayBuffer()` settles with. */
struct BodyBuffer {
    std::shared_ptr<ByteBuffer> buffer;
};

jsi::Value uint8ArrayOf(jsi::Runtime& runtime, std::string bytes) {
    return runtime.global()
        .getPropertyAsFunction(runtime, "Uint8Array")
        .callAsConstructor(runtime, jsi::ArrayBuffer(runtime, std::make_shared<ByteBuffer>(std::move(bytes))));
}

} // namespace

} // namespace react_native_linux

template <> struct facebook::react::Bridging<react_native_linux::BodyBuffer> {
    static jsi::Value toJs(jsi::Runtime& runtime, const react_native_linux::BodyBuffer& body) {
        return jsi::ArrayBuffer(runtime, body.buffer);
    }
};

namespace react_native_linux {

namespace {

struct Context {
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker;
    std::shared_ptr<CurlHttpClient> client;
};

std::shared_ptr<ExpoFetchResponse> responseOf(jsi::Runtime& runtime, const jsi::Value& value) {
    return sharedObjectState<FetchState>(runtime, value)->response;
}

/** Emits upstream's events on the `NativeResponse` instance, while JavaScript still holds it. */
FetchResponseEvents eventsOf(jsi::Runtime& runtime, const jsi::Object& instance) {
    const auto target = std::make_shared<jsi::WeakObject>(runtime, instance);
    const auto emit = [&runtime, target](const char* event, const std::function<jsi::Value()>& payload) {
        const jsi::Value strongInstance = target->lock(runtime);

        if (strongInstance.isObject()) {
            const jsi::Object object = strongInstance.getObject(runtime);

            object.getPropertyAsFunction(runtime, "emit")
                .callWithThis(runtime, object, jsi::String::createFromAscii(runtime, event), payload());
        }
    };

    return {
        .didReceiveResponseData =
            [&runtime, emit](std::string bytes) {
                emit("didReceiveResponseData", [&runtime, &bytes] { return uint8ArrayOf(runtime, std::move(bytes)); });
            },
        .didComplete = [emit] { emit("didComplete", [] { return jsi::Value::undefined(); }); },
        .didFailWithError =
            [&runtime, emit](const std::string& message) {
                emit("didFailWithError",
                     [&runtime, &message] { return jsi::Value(jsi::String::createFromUtf8(runtime, message)); });
            },
        .readyForJSFinalization = [emit] { emit("readyForJSFinalization", [] { return jsi::Value::undefined(); }); },
    };
}

FetchRedirect redirectOf(const std::string& redirect) {
    if (redirect == "error") {
        return FetchRedirect::Error;
    }

    return redirect == "manual" ? FetchRedirect::Manual : FetchRedirect::Follow;
}

/** `start(url, requestInit, requestBody)`'s arguments: `requestInit` as upstream's `fetch` always builds it. */
FetchRequest requestOf(jsi::Runtime& runtime, const jsi::Value* arguments) {
    const jsi::Object init = arguments[1].asObject(runtime);
    const jsi::Array headerPairs = init.getPropertyAsObject(runtime, "headers").asArray(runtime);
    FetchRequest request{.url = arguments[0].asString(runtime).utf8(runtime),
                         .method = init.getProperty(runtime, "method").asString(runtime).utf8(runtime),
                         .headers = {},
                         .body = std::nullopt,
                         .redirect = redirectOf(init.getProperty(runtime, "redirect").asString(runtime).utf8(runtime))};

    for (size_t index = 0; index < headerPairs.size(runtime); ++index) {
        const jsi::Array pair = headerPairs.getValueAtIndex(runtime, index).asObject(runtime).asArray(runtime);

        request.headers.emplace_back(pair.getValueAtIndex(runtime, 0).asString(runtime).utf8(runtime),
                                     pair.getValueAtIndex(runtime, 1).asString(runtime).utf8(runtime));
    }

    if (arguments[2].isObject()) {
        const jsi::Object view = arguments[2].getObject(runtime);
        jsi::ArrayBuffer buffer = view.getPropertyAsObject(runtime, "buffer").getArrayBuffer(runtime);
        const auto [offset, length] =
            byteViewRange(view.getProperty(runtime, "byteOffset").asNumber(),
                          view.getProperty(runtime, "byteLength").asNumber(), buffer.size(runtime));

        request.body.emplace(reinterpret_cast<const char*>(buffer.data(runtime)) + offset, length);
    }

    return request;
}

template <typename... Result> ExpoFetchResponse::Reject rejecting(facebook::react::AsyncPromise<Result...> promise) {
    return [promise](const std::string& message) mutable { promise.reject(facebook::react::Error(message)); };
}

/** A promise of the completed body, as `convert` makes it of the body's bytes. */
template <typename Result, typename Convert>
jsi::Value settleBody(jsi::Runtime& runtime, const Context& context, ExpoFetchResponse& response, Convert convert) {
    facebook::react::AsyncPromise<Result> promise(runtime, context.jsInvoker);

    response.readBody([promise, convert](std::string body) mutable { promise.resolve(convert(std::move(body))); },
                      rejecting(promise));

    return {runtime, promise.get(runtime)};
}

using Method = std::function<jsi::Value(jsi::Runtime&, ExpoFetchResponse&, const jsi::Value*, size_t)>;

void addMember(SharedObjectMembers& members, const std::string& name, const Method& method) {
    members.emplace_back(
        name, [method](jsi::Runtime& runtime, const jsi::Value& thisValue, const jsi::Value* arguments, size_t count) {
            const std::shared_ptr<ExpoFetchResponse> response = responseOf(runtime, thisValue);

            return method(runtime, *response, arguments, count);
        });
}

SharedObjectMembers requestMethods(const std::shared_ptr<Context>& context) {
    SharedObjectMembers methods;

    addMember(methods, "start",
              [context](jsi::Runtime& runtime, ExpoFetchResponse& response, const jsi::Value* arguments, size_t) {
                  facebook::react::AsyncPromise<> promise(runtime, context->jsInvoker);

                  response.start(
                      *context->client, requestOf(runtime, arguments), [promise]() mutable { promise.resolve(); },
                      rejecting(promise));

                  return jsi::Value(runtime, promise.get(runtime));
              });
    addMember(methods, "cancel", [](jsi::Runtime&, ExpoFetchResponse& response, const jsi::Value*, size_t) {
        response.cancel();
        return jsi::Value::undefined();
    });

    return methods;
}

SharedObjectMembers responseMethods(const std::shared_ptr<Context>& context) {
    SharedObjectMembers methods;

    addMember(methods, "startStreaming",
              [](jsi::Runtime& runtime, ExpoFetchResponse& response, const jsi::Value*, size_t) {
                  std::optional<std::string> body;

                  try {
                      body = response.startStreaming();
                  } catch (const std::runtime_error& failure) {
                      throw jsi::JSError(runtime, failure.what());
                  }

                  return body.has_value() ? uint8ArrayOf(runtime, std::move(body.value())) : jsi::Value::null();
              });
    addMember(methods, "cancelStreaming", [](jsi::Runtime&, ExpoFetchResponse& response, const jsi::Value*, size_t) {
        response.cancelStreaming();
        return jsi::Value::undefined();
    });
    addMember(methods, "arrayBuffer",
              [context](jsi::Runtime& runtime, ExpoFetchResponse& response, const jsi::Value*, size_t) {
                  return settleBody<BodyBuffer>(runtime, *context, response, [](std::string body) {
                      return BodyBuffer{.buffer = std::make_shared<ByteBuffer>(std::move(body))};
                  });
              });
    addMember(methods, "text",
              [context](jsi::Runtime& runtime, ExpoFetchResponse& response, const jsi::Value*, size_t) {
                  return settleBody<std::string>(runtime, *context, response, [](std::string body) { return body; });
              });

    return methods;
}

SharedObjectMembers responseGetters() {
    SharedObjectMembers getters;

    addMember(
        getters, "_rawHeaders", [](jsi::Runtime& runtime, ExpoFetchResponse& response, const jsi::Value*, size_t) {
            const facebook::react::http::Headers& headers = response.head().headers;
            jsi::Array pairs(runtime, headers.size());

            for (size_t index = 0; index < headers.size(); ++index) {
                pairs.setValueAtIndex(
                    runtime, index,
                    jsi::Array::createWithElements(runtime, jsi::String::createFromUtf8(runtime, headers[index].first),
                                                   jsi::String::createFromUtf8(runtime, headers[index].second)));
            }

            return jsi::Value(runtime, pairs);
        });
    addMember(getters, "status", [](jsi::Runtime&, ExpoFetchResponse& response, const jsi::Value*, size_t) {
        return jsi::Value(response.head().status);
    });
    addMember(getters, "statusText", [](jsi::Runtime& runtime, ExpoFetchResponse& response, const jsi::Value*, size_t) {
        return jsi::Value(jsi::String::createFromUtf8(runtime, response.head().statusText));
    });
    addMember(getters, "url", [](jsi::Runtime& runtime, ExpoFetchResponse& response, const jsi::Value*, size_t) {
        return jsi::Value(jsi::String::createFromUtf8(runtime, response.head().url));
    });
    addMember(getters, "redirected", [](jsi::Runtime&, ExpoFetchResponse& response, const jsi::Value*, size_t) {
        return jsi::Value(response.head().redirected);
    });

    return getters;
}

/**
 * `ExpoFetchModule`, the native module upstream `expo/fetch` reaches through `requireNativeModule` (#673,
 * ADR-0004), and with it the global `fetch` `Expo.fx` installs. Its constants are the classes `fetch` constructs and
 * extends: `NativeResponse`, which `FetchResponse` extends, and `NativeRequest`, constructed with that response, which
 * shares its `ExpoFetchResponse` and wires its events to the response instance. The surface is what upstream's
 * JavaScript calls; `unstable_createBlobData`, which `Response.blob()` calls to put bytes in React Native's blob store,
 * is absent because this platform has no `BlobModule`.
 *
 * Threading contract: every method and getter runs on the JavaScript thread, and so does everything that touches an
 * `ExpoFetchResponse`. The module's `CurlHttpClient` runs the transfers on its worker, which hands each callback to the
 * call invoker; the response emits its events on the JavaScript thread from there, and settles its promises through
 * the call invoker. A transfer holds the response weakly, so one whose response JavaScript has dropped settles nothing,
 * and that response cancels the transfer as it is destroyed. The client lives as long as the classes' methods, which
 * the runtime destroys on the JavaScript thread; its destructor joins the worker, so no callback is posted after it
 * returns.
 */
class LinuxExpoFetchModule final : public facebook::react::TurboModule {
public:
    explicit LinuxExpoFetchModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
        : TurboModule("ExpoFetchModule", jsInvoker),
          context_(std::make_shared<Context>(
              Context{.jsInvoker = std::move(jsInvoker), .client = std::make_shared<CurlHttpClient>()})) {
        methodMap_["getConstants"] = {0,
                                      [](jsi::Runtime& runtime, TurboModule& turboModule, const jsi::Value*, size_t) {
                                          return static_cast<LinuxExpoFetchModule&>(turboModule).constants(runtime);
                                      }};
    }

private:
    jsi::Value constants(jsi::Runtime& runtime) {
        const std::shared_ptr<facebook::react::CallInvoker> jsInvoker = context_->jsInvoker;
        const ExpoFetchResponse::Post post = [jsInvoker](std::function<void()> work) {
            jsInvoker->invokeAsync(std::move(work));
        };
        jsi::Object constants(runtime);

        constants.setProperty(runtime, "NativeResponse",
                              makeSharedObjectClass(
                                  runtime,
                                  [post](jsi::Runtime&, const jsi::Value*, size_t) {
                                      return std::make_shared<FetchState>(std::make_shared<ExpoFetchResponse>(post));
                                  },
                                  responseMethods(context_), responseGetters()));
        constants.setProperty(
            runtime, "NativeRequest",
            makeSharedObjectClass(
                runtime,
                [](jsi::Runtime& initializeRuntime, const jsi::Value* arguments, size_t) {
                    const std::shared_ptr<ExpoFetchResponse> response = responseOf(initializeRuntime, arguments[0]);

                    response->setEvents(eventsOf(initializeRuntime, arguments[0].getObject(initializeRuntime)));

                    return std::make_shared<FetchState>(response);
                },
                requestMethods(context_), {}));

        return constants;
    }

    std::shared_ptr<Context> context_;
};

} // namespace

std::shared_ptr<facebook::react::TurboModule>
makeExpoFetchModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker) {
    return std::make_shared<LinuxExpoFetchModule>(std::move(jsInvoker));
}

} // namespace react_native_linux
