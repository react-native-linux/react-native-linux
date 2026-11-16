#include "../ExpoFetchProbeServer.h"
#include "OnJavaScriptThread.h"

#include <cxxreact/JSBigString.h>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace react_native_linux {
namespace {

// What upstream's expo/fetch does with ExpoFetchModule (fetch.ts, FetchResponse.ts), over a stand-in for
// expo-modules-core's polyfilled EventEmitter and SharedObject; the expo-entry e2e runs the real package.
constexpr char kPrelude[] = R"JAVASCRIPT(
class EventEmitter {
  addListener(name, listener) {
    this.listeners ??= {};
    (this.listeners[name] ??= []).push(listener);
  }
  removeAllListeners(name) { delete this.listeners?.[name]; }
  emit(name, ...args) { for (const listener of [...(this.listeners?.[name] ?? [])]) listener.apply(this, args); }
}
globalThis.expo = { SharedObject: class SharedObject extends EventEmitter {} };
const { NativeRequest, NativeResponse } = globalThis.nativeModuleProxy.ExpoFetchModule.getConstants();
const text = (bytes) => String.fromCharCode(...bytes);
class FetchResponse extends NativeResponse {
  constructor() {
    super();
    this.trace = [];
    this.addListener('readyForJSFinalization', () => this.trace.push('ready'));
  }
  stream() {
    this.addListener('didReceiveResponseData', (data) => this.trace.push(`data:${data instanceof Uint8Array && text(data)}`));
    this.addListener('didComplete', () => this.trace.push('complete'));
    this.addListener('didFailWithError', (error) => this.trace.push(`fail:${error}`));
    return this.startStreaming();
  }
}
// No parameter defaults: an async arrow function with one drops what it awaits in this Hermes (#728).
const send = async (url, options) => {
  const { method = 'GET', headers = [], redirect = 'follow', body = null } = options ?? {};
  const response = new FetchResponse();
  const request = new NativeRequest(response);
  await request.start(url, { credentials: 'include', headers, method, redirect }, body);
  return { request, response };
};
const failure = (work) => work().then(() => 'resolved', (error) => `rejected:${error.message}`);
const until = async (done) => {
  while (!done()) await new Promise((resolve) => setTimeout(resolve, 1));
};
)JAVASCRIPT";

/** The probes run against `expoFetchProbeRoutes`, whose gate the test opens. */
class ExpoFetchModuleTest : public ::testing::Test {
protected:
    void TearDown() override { gate_.set_value(); }

    /** Runs `probe` after the prelude; it ends by setting `globalThis.expoFetchTrace`. */
    static void load(ReactHost& reactHost, const std::string& probe) {
        reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(std::string(kPrelude) + probe),
                             "ExpoFetchModuleTest.js");
    }

    std::string url(const std::string& path) const { return server_.url(path); }

    std::promise<void> gate_;
    std::shared_future<void> opened_ = gate_.get_future().share();
    LoopbackHttpServer server_{expoFetchProbeRoutes(opened_, [] {})};
};

/**
 * #673: the request and response classes, constructed and extended as upstream's `fetch` and `FetchResponse` do,
 * answer the status, headers, URL and body, follow or refuse a redirect, send a body and reject with the transfer's
 * error.
 */
TEST_F(ExpoFetchModuleTest, UpstreamsRequestAndResponseClassesFetch) {
    ReactHost reactHost;

    load(reactHost, R"JAVASCRIPT(
(async () => {
  const { response: redirected } = await send(')JAVASCRIPT" +
                        url("/redirect") + R"JAVASCRIPT(');
  const { response: buffered } = await send(')JAVASCRIPT" +
                        url("/text") + R"JAVASCRIPT(');
  const bytes = await buffered.arrayBuffer();
  const { response: echoed } = await send(')JAVASCRIPT" +
                        url("/echo") + R"JAVASCRIPT(', {
    method: 'POST',
    headers: [['Content-Type', 'text/plain']],
    body: new Uint8Array([0, 98, 111, 100, 121, 0]).subarray(1, 5),
  });
  const trace = {
    status: redirected.status,
    statusText: redirected.statusText,
    url: redirected.url.endsWith('/text'),
    redirected: redirected.redirected,
    header: redirected._rawHeaders.find(([name]) => name === 'X-Probe')?.[1],
    text: await redirected.text(),
    arrayBuffer: bytes instanceof ArrayBuffer && text(new Uint8Array(bytes)),
    echoed: (await echoed.text()).endsWith('\r\n\r\nbody'),
    refused: await failure(() => send(')JAVASCRIPT" +
                        url("/redirect") + R"JAVASCRIPT(', { redirect: 'error' })),
    unreachable: await failure(() => send('http://127.0.0.1:9/')),
    outsideItsBuffer: (await failure(() => send('http://127.0.0.1:9/', {
      method: 'POST',
      body: { buffer: new ArrayBuffer(4), byteOffset: 2, byteLength: 9 },
    }))).includes('do not lie inside its buffer'),
    finalized: [redirected.trace, buffered.trace],
  };
  globalThis.expoFetchTrace = JSON.stringify(trace);
})().catch((error) => { globalThis.expoFetchTrace = `failed: ${error.message}`; });
)JAVASCRIPT");

    EXPECT_EQ(awaitGlobalString(reactHost, "expoFetchTrace"),
              R"({"status":200,"statusText":"","url":true,"redirected":true,"header":"yes","text":"hello",)"
              R"("arrayBuffer":"hello","echoed":true,"refused":"rejected:Redirect is not allowed when redirect mode )"
              R"(is 'error'","unreachable":"rejected:Couldn't connect to server",)"
              R"("outsideItsBuffer":true,"finalized":[["ready"],["ready"]]})");
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

/**
 * #673, #679: a streamed body reaches JavaScript chunk by chunk as the server sends it, and the JavaScript thread
 * stays free while the rest is in flight: the server sends its second half only once a timer set at the first chunk
 * has fired, so the trace is in that order or the probe never finishes.
 */
TEST_F(ExpoFetchModuleTest, StreamsChunksAsTheyArriveWithoutBlockingTheJavaScriptThread) {
    ReactHost reactHost;

    load(reactHost, R"JAVASCRIPT(
(async () => {
  const { response } = await send(')JAVASCRIPT" +
                        url("/trickle") + R"JAVASCRIPT(');
  let timerSet = false;
  response.addListener('didReceiveResponseData', () => {
    if (!timerSet) {
      timerSet = true;
      setTimeout(() => {
        response.trace.push('timer');
        globalThis.expoFetchFirstChunk = 'seen';
      }, 0);
    }
  });
  const initial = await response.stream();
  response.addListener('readyForJSFinalization', () => {
    globalThis.expoFetchTrace = JSON.stringify({ initial, trace: response.trace });
  });
})().catch((error) => { globalThis.expoFetchTrace = `failed: ${error.message}`; });
)JAVASCRIPT");

    ASSERT_EQ(awaitGlobalString(reactHost, "expoFetchFirstChunk"), "seen");
    gate_.set_value();
    gate_ = {};

    EXPECT_EQ(awaitGlobalString(reactHost, "expoFetchTrace"),
              R"({"initial":null,"trace":["data:first","timer","data:second","complete","ready"]})");
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

/**
 * #673: `await fetch()` resolves once the final response's headers have arrived, before any body byte: the server
 * sends the body only once JavaScript has read the status, so the probe never finishes otherwise.
 */
TEST_F(ExpoFetchModuleTest, StartResolvesBeforeTheBodyArrives) {
    ReactHost reactHost;

    load(reactHost, R"JAVASCRIPT(
(async () => {
  const { response } = await send(')JAVASCRIPT" +
                        url("/headers") + R"JAVASCRIPT(');
  globalThis.expoFetchStatus = String(response.status);
  globalThis.expoFetchTrace = JSON.stringify({ text: await response.text(), trace: response.trace });
})().catch((error) => { globalThis.expoFetchTrace = `failed: ${error.message}`; });
)JAVASCRIPT");

    ASSERT_EQ(awaitGlobalString(reactHost, "expoFetchStatus"), "200");
    gate_.set_value();
    gate_ = {};

    EXPECT_EQ(awaitGlobalString(reactHost, "expoFetchTrace"), R"({"text":"body","trace":["ready"]})");
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

/** #673: a first body read after the transfer failed rejects with the failure, which upstream's stream pull awaits. */
TEST_F(ExpoFetchModuleTest, AFailureBeforeTheFirstReadRejectsIt) {
    ReactHost reactHost;

    load(reactHost, R"JAVASCRIPT(
(async () => {
  const { response } = await send(')JAVASCRIPT" +
                        url("/truncated") + R"JAVASCRIPT(');
  await until(() => response.trace.includes('ready'));
  const read = await failure(async () => response.stream());
  globalThis.expoFetchTrace = JSON.stringify({ read, trace: response.trace });
})().catch((error) => { globalThis.expoFetchTrace = `failed: ${error.message}`; });
)JAVASCRIPT");

    EXPECT_EQ(awaitGlobalString(reactHost, "expoFetchTrace"),
              R"({"read":"rejected:Transferred a partial file","trace":["ready"]})");
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

/** `cancel()`, which an `AbortSignal` calls, fails the stream with upstream's message and rejects a body read. */
TEST_F(ExpoFetchModuleTest, CancelFailsTheStreamAndTheBodyRead) {
    ReactHost reactHost;

    load(reactHost, R"JAVASCRIPT(
(async () => {
  const { request, response } = await send(')JAVASCRIPT" +
                        url("/trickle") + R"JAVASCRIPT(');
  await response.stream();
  await until(() => response.trace.includes('data:first'));
  request.cancel();
  globalThis.expoFetchTrace = JSON.stringify({ trace: response.trace, text: await failure(() => response.text()) });
})().catch((error) => { globalThis.expoFetchTrace = `failed: ${error.message}`; });
)JAVASCRIPT");

    EXPECT_EQ(awaitGlobalString(reactHost, "expoFetchTrace"),
              R"({"trace":["data:first","fail:Fetch request has been canceled","ready"],)"
              R"("text":"rejected:Fetch request has been canceled"})");
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

/** A host torn down with a transfer in flight and its response alive joins the worker and frees everything. */
TEST_F(ExpoFetchModuleTest, TeardownWithATransferInFlight) {
    {
        ReactHost reactHost;

        load(reactHost, R"JAVASCRIPT(
send(')JAVASCRIPT" + url("/trickle") +
                            R"JAVASCRIPT(').then(({ response }) => {
  globalThis.pending = response;
  globalThis.expoFetchTrace = 'started';
});
)JAVASCRIPT");

        ASSERT_EQ(awaitGlobalString(reactHost, "expoFetchTrace"), "started");
    }

    gate_.set_value();
    gate_ = {};
}

} // namespace
} // namespace react_native_linux
