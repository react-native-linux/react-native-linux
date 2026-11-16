#include "ExpoFetch.h"

#include "CurlHttpClient.h"
#include "ExpoFetchProbeServer.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <utility>
#include <vector>

namespace react_native_linux {
namespace {

constexpr std::chrono::seconds kBudget{10};

/**
 * The JavaScript thread, played by the test's own thread: the client's worker posts work here, and only the test
 * thread runs it, so every `ExpoFetchResponse` member runs on one thread as its contract asks.
 */
class JavaScriptQueue final {
public:
    ExpoFetchResponse::Post post() {
        return [this](std::function<void()> work) {
            const std::lock_guard<std::mutex> guard(mutex_);
            tasks_.push_back(std::move(work));
            posted_.notify_all();
        };
    }

    /** Runs posted work, in order, until `done` holds; false if it did not within the budget. */
    bool runUntil(const std::function<bool()>& done) {
        const auto deadline = std::chrono::steady_clock::now() + kBudget;

        while (!done()) {
            std::unique_lock<std::mutex> lock(mutex_);

            if (!posted_.wait_until(lock, deadline, [this] { return !tasks_.empty(); })) {
                return false;
            }

            std::function<void()> task = std::move(tasks_.front());
            tasks_.pop_front();
            lock.unlock();
            task();
        }

        return true;
    }

    /** Waits, running nothing, until `count` tasks are queued. */
    bool waitForQueued(size_t count) {
        std::unique_lock<std::mutex> lock(mutex_);

        return posted_.wait_for(lock, kBudget, [this, count] { return tasks_.size() >= count; });
    }

    /** Runs everything queued now. */
    void runQueued() {
        std::deque<std::function<void()>> tasks;

        {
            const std::lock_guard<std::mutex> guard(mutex_);
            tasks.swap(tasks_);
        }

        for (std::function<void()>& task : tasks) {
            task();
        }
    }

private:
    std::mutex mutex_;
    std::condition_variable posted_;
    std::deque<std::function<void()>> tasks_;
};

/** The test thread as the JavaScript thread, against `expoFetchProbeRoutes`, whose gate the test opens. */
class ExpoFetchTest : public ::testing::Test {
protected:
    void TearDown() override {
        gate_.set_value();
        std::filesystem::remove_all(directory_);
    }

    std::shared_ptr<ExpoFetchResponse> makeResponse() {
        auto response = std::make_shared<ExpoFetchResponse>(queue_.post());

        response->setEvents({
            .didReceiveResponseData = [this](std::string bytes) { log_.push_back("data:" + bytes); },
            .didComplete = [this] { log_.emplace_back("complete"); },
            .didFailWithError = [this](const std::string& message) { log_.push_back("fail:" + message); },
            .readyForJSFinalization = [this] { log_.emplace_back("ready"); },
        });

        return response;
    }

    void start(ExpoFetchResponse& response, const FetchRequest& request) {
        response.start(
            client_, request, [this] { log_.emplace_back("resolved"); },
            [this](const std::string& message) { log_.push_back("rejected:" + message); });
    }

    void readBody(ExpoFetchResponse& response) {
        response.readBody([this](const std::string& body) { log_.push_back("body:" + body); },
                          [this](const std::string& message) { log_.push_back("body rejected:" + message); });
    }

    /** Runs the JavaScript thread until `entry` is logged. */
    bool logs(const std::string& entry) {
        return queue_.runUntil([this, &entry] { return std::ranges::find(log_, entry) != log_.end(); });
    }

    /** Lets the gated route send the rest and runs the JavaScript thread until the response is finalized. */
    std::vector<std::string> finishGatedRoute() {
        gate_.set_value();
        gate_ = {};
        EXPECT_TRUE(logs("ready"));

        return log_;
    }

    /** What `/echo` answered `request` with: the request the server read. */
    std::string echo(const FetchRequest& request) {
        const std::shared_ptr<ExpoFetchResponse> response = makeResponse();
        std::string echoed;

        start(*response, request);
        response->readBody([&echoed](const std::string& body) { echoed = body; }, [](const std::string&) {});
        EXPECT_TRUE(queue_.runUntil([&echoed] { return !echoed.empty(); }));

        return echoed;
    }

    FetchRequest get(const std::string& path, FetchRedirect redirect = FetchRedirect::Follow) {
        return {.url = server_.url(path), .method = "GET", .headers = {}, .body = std::nullopt, .redirect = redirect};
    }

    std::filesystem::path directory_ =
        std::filesystem::temp_directory_path() / ("rnl-expo-fetch-" + std::to_string(::getpid()));
    std::promise<void> gate_;
    std::shared_future<void> opened_ = gate_.get_future().share();
    std::promise<void> peerClosed_;
    LoopbackHttpServer server_{expoFetchProbeRoutes(opened_, [this] { peerClosed_.set_value(); })};
    JavaScriptQueue queue_;
    CurlHttpClient client_;
    std::vector<std::string> log_;
};

/** The first read takes what arrived before it, and every later chunk is delivered as it arrives, never buffered. */
TEST_F(ExpoFetchTest, StreamsEachChunkAsItArrives) {
    const std::shared_ptr<ExpoFetchResponse> response = makeResponse();

    start(*response, get("/trickle"));
    ASSERT_TRUE(logs("resolved"));
    EXPECT_EQ(response->startStreaming(), std::nullopt);
    ASSERT_TRUE(logs("data:first"));
    EXPECT_EQ(response->startStreaming(), std::nullopt);
    EXPECT_EQ(std::ranges::find(log_, "complete"), log_.end());

    EXPECT_EQ(finishGatedRoute(),
              (std::vector<std::string>{"resolved", "data:first", "data:second", "complete", "ready"}));
}

/**
 * The request resolves once the final response's headers end, before any body byte, skipping the interim 103
 * response: a stream whose server flushes its headers and then waits must not leave `await fetch()` pending.
 */
TEST_F(ExpoFetchTest, ResolvesOnceTheFinalResponsesHeadersEnd) {
    const std::shared_ptr<ExpoFetchResponse> response = makeResponse();

    start(*response, get("/headers"));
    ASSERT_TRUE(logs("resolved"));
    EXPECT_EQ(log_, std::vector<std::string>{"resolved"});
    EXPECT_EQ(response->head().status, 200);
    EXPECT_EQ(response->head().headers,
              (facebook::react::http::Headers{{"Content-Length", "4"}, {"Connection", "close"}, {"X-Final", "yes"}}));
    readBody(*response);

    EXPECT_EQ(finishGatedRoute(), (std::vector<std::string>{"resolved", "body:body", "ready"}));
}

/** A 302 without a Location is not followed, so it is the final response and resolves once its headers end. */
TEST_F(ExpoFetchTest, ResolvesOnARedirectWithoutALocationOnceItsHeadersEnd) {
    const std::shared_ptr<ExpoFetchResponse> response = makeResponse();

    start(*response, get("/unlocated"));
    ASSERT_TRUE(logs("resolved"));
    EXPECT_EQ(log_, std::vector<std::string>{"resolved"});
    EXPECT_EQ(response->head().status, 302);
    EXPECT_FALSE(response->head().redirected);
    readBody(*response);

    EXPECT_EQ(finishGatedRoute(), (std::vector<std::string>{"resolved", "body:body", "ready"}));
}

/** A folded header line does not end the headers, so the headers after it are delivered with the response. */
TEST_F(ExpoFetchTest, AFoldedHeaderLineDoesNotEndTheHeaders) {
    const std::shared_ptr<ExpoFetchResponse> response = makeResponse();

    start(*response, get("/folded"));
    ASSERT_TRUE(logs("resolved"));
    EXPECT_NE(
        std::ranges::find(response->head().headers, std::pair<std::string, std::string>("Content-Type", "text/plain")),
        response->head().headers.end());
    readBody(*response);

    EXPECT_EQ(finishGatedRoute(), (std::vector<std::string>{"resolved", "body:body", "ready"}));
}

/** A failure after the response but before the first read makes that read throw its message, not wait forever. */
TEST_F(ExpoFetchTest, AFailureBeforeTheFirstReadFailsThatRead) {
    const std::shared_ptr<ExpoFetchResponse> response = makeResponse();
    const auto failureOfFirstRead = [&response]() -> std::string {
        try {
            response->startStreaming();
        } catch (const std::runtime_error& failure) {
            return failure.what();
        }

        return "no failure";
    };

    start(*response, get("/truncated"));
    ASSERT_TRUE(logs("ready"));
    EXPECT_EQ(log_, (std::vector<std::string>{"resolved", "ready"}));
    EXPECT_EQ(failureOfFirstRead(), "Transferred a partial file");
}

/** A body read once it has completed is handed over whole, by `startStreaming` or by `readBody`, and only once. */
TEST_F(ExpoFetchTest, HandsOverACompletedBodyWholeAndOnlyOnce) {
    const std::shared_ptr<ExpoFetchResponse> streamed = makeResponse();
    const std::shared_ptr<ExpoFetchResponse> read = makeResponse();

    start(*streamed, get("/text"));
    ASSERT_TRUE(logs("ready"));
    EXPECT_EQ(streamed->startStreaming(), "hello");
    EXPECT_EQ(streamed->startStreaming(), "");
    EXPECT_EQ(streamed->head().status, 200);
    EXPECT_EQ(streamed->head().statusText, "");
    EXPECT_EQ(streamed->head().url, server_.url("/text"));
    EXPECT_FALSE(streamed->head().redirected);
    EXPECT_NE(std::ranges::find(streamed->head().headers, std::pair<std::string, std::string>("X-Probe", "yes")),
              streamed->head().headers.end());

    log_.clear();
    start(*read, get("/text"));
    readBody(*read);
    ASSERT_TRUE(logs("ready"));
    EXPECT_EQ(log_, (std::vector<std::string>{"resolved", "body:hello", "ready"}));
}

/** Body bytes that arrived before the first read are kept, and that read emits them before it returns. */
TEST_F(ExpoFetchTest, TheFirstReadEmitsWhatArrivedBeforeIt) {
    const std::shared_ptr<ExpoFetchResponse> response = makeResponse();

    start(*response, get("/trickle"));
    ASSERT_TRUE(queue_.waitForQueued(3));
    queue_.runQueued();
    EXPECT_EQ(log_, std::vector<std::string>{"resolved"});
    EXPECT_EQ(response->startStreaming(), std::nullopt);
    EXPECT_EQ(log_, (std::vector<std::string>{"resolved", "data:first"}));

    EXPECT_EQ(finishGatedRoute(),
              (std::vector<std::string>{"resolved", "data:first", "data:second", "complete", "ready"}));
}

/** `redirect` follows a redirect and says so, answers it as the response, or fails as upstream does. */
TEST_F(ExpoFetchTest, FollowsAnswersOrRejectsARedirect) {
    const std::shared_ptr<ExpoFetchResponse> followed = makeResponse();
    const std::shared_ptr<ExpoFetchResponse> manual = makeResponse();
    const std::shared_ptr<ExpoFetchResponse> refused = makeResponse();
    const std::shared_ptr<ExpoFetchResponse> notRedirected = makeResponse();

    start(*followed, get("/redirect"));
    ASSERT_TRUE(logs("ready"));
    EXPECT_EQ(followed->head().status, 200);
    EXPECT_EQ(followed->head().url, server_.url("/text"));
    EXPECT_TRUE(followed->head().redirected);

    log_.clear();
    start(*manual, get("/redirect", FetchRedirect::Manual));
    readBody(*manual);
    ASSERT_TRUE(logs("body:moved"));
    EXPECT_EQ(manual->head().status, 302);
    EXPECT_EQ(manual->head().url, server_.url("/redirect"));
    EXPECT_FALSE(manual->head().redirected);

    log_.clear();
    start(*refused, get("/redirect", FetchRedirect::Error));
    readBody(*refused);
    ASSERT_TRUE(logs("body rejected:Redirect is not allowed when redirect mode is 'error'"));
    EXPECT_EQ(log_, (std::vector<std::string>{"rejected:Redirect is not allowed when redirect mode is 'error'",
                                              "body rejected:Redirect is not allowed when redirect mode is 'error'",
                                              "ready"}));

    log_.clear();
    start(*notRedirected, get("/text", FetchRedirect::Error));
    ASSERT_TRUE(logs("ready"));
    EXPECT_EQ(notRedirected->head().status, 200);
}

/** The method, headers and body reach the server; a POST without a body sends an empty one, as upstream's does. */
TEST_F(ExpoFetchTest, SendsTheMethodHeadersAndBody) {
    const std::string put = echo({.url = server_.url("/echo"),
                                  .method = "PUT",
                                  .headers = {{"X-Custom", "value"}},
                                  .body = std::string("payload\0bytes", 13),
                                  .redirect = FetchRedirect::Follow});
    const std::string post = echo({.url = server_.url("/echo"),
                                   .method = "POST",
                                   .headers = {},
                                   .body = std::nullopt,
                                   .redirect = FetchRedirect::Follow});

    EXPECT_TRUE(put.starts_with("PUT /echo "));
    EXPECT_NE(put.find("X-Custom: value\r\n"), std::string::npos);
    EXPECT_TRUE(put.ends_with(std::string("\r\n\r\npayload\0bytes", 17)));
    EXPECT_TRUE(post.starts_with("POST /echo "));
    EXPECT_NE(post.find("Content-Length: 0\r\n"), std::string::npos);
    EXPECT_EQ(echo(get("/echo")).find("Content-Length"), std::string::npos);
}

/** A file answers 200 with its bytes, and a file that cannot be read 404 with none, as upstream's Android module. */
TEST_F(ExpoFetchTest, AnswersAFileOrItsAbsence) {
    const std::shared_ptr<ExpoFetchResponse> found = makeResponse();
    const std::shared_ptr<ExpoFetchResponse> missing = makeResponse();
    const std::string fileUrl = "file://" + (directory_ / "note.txt").string();

    std::filesystem::create_directories(directory_);
    std::ofstream(directory_ / "note.txt") << "on disk";

    start(*found,
          {.url = fileUrl, .method = "GET", .headers = {}, .body = std::nullopt, .redirect = FetchRedirect::Follow});
    readBody(*found);
    ASSERT_TRUE(logs("body:on disk"));
    EXPECT_EQ(found->head().status, 200);
    EXPECT_EQ(found->head().statusText, "OK");
    EXPECT_EQ(found->head().url, fileUrl);

    log_.clear();
    start(*missing, {.url = fileUrl + ".missing",
                     .method = "GET",
                     .headers = {},
                     .body = std::nullopt,
                     .redirect = FetchRedirect::Follow});
    readBody(*missing);
    ASSERT_TRUE(logs("ready"));
    EXPECT_EQ(log_, (std::vector<std::string>{"resolved", "body:", "ready"}));
    EXPECT_EQ(missing->head().status, 404);
    EXPECT_EQ(missing->head().statusText, "File not found");
}

/** A transfer error rejects the request with libcurl's message, and the body read with it too. */
TEST_F(ExpoFetchTest, RejectsWithTheTransferError) {
    const std::shared_ptr<ExpoFetchResponse> response = makeResponse();

    start(*response, {.url = "http://127.0.0.1:9/",
                      .method = "GET",
                      .headers = {},
                      .body = std::nullopt,
                      .redirect = FetchRedirect::Follow});
    readBody(*response);
    ASSERT_TRUE(logs("ready"));
    EXPECT_EQ(log_, (std::vector<std::string>{"rejected:Couldn't connect to server",
                                              "body rejected:Couldn't connect to server", "ready"}));
    EXPECT_EQ(response->head().status, -1);
}

/**
 * `cancel` fails a stream with upstream's message and finalizes once, and a cancel before start, after completion
 * or twice does nothing.
 */
TEST_F(ExpoFetchTest, CancelFailsTheStreamAndDropsLateWork) {
    const std::shared_ptr<ExpoFetchResponse> streaming = makeResponse();
    const std::shared_ptr<ExpoFetchResponse> completed = makeResponse();

    streaming->cancel();
    EXPECT_TRUE(log_.empty());
    start(*streaming, get("/trickle"));
    ASSERT_TRUE(logs("resolved"));
    streaming->startStreaming();
    ASSERT_TRUE(logs("data:first"));
    streaming->cancel();
    streaming->cancel();
    gate_.set_value();
    gate_ = {};
    readBody(*streaming);
    EXPECT_EQ(log_, (std::vector<std::string>{"resolved", "data:first", "fail:Fetch request has been canceled", "ready",
                                              "body rejected:Fetch request has been canceled"}));

    log_.clear();
    start(*completed, get("/text"));
    ASSERT_TRUE(logs("ready"));
    completed->cancel();
    readBody(*completed);
    EXPECT_EQ(log_, (std::vector<std::string>{"resolved", "ready", "body:hello"}));
}

/** What the worker had already posted when `cancel` ran is dropped. */
TEST_F(ExpoFetchTest, CancelDropsWhatTheWorkerHadAlreadyPosted) {
    const std::shared_ptr<ExpoFetchResponse> late = makeResponse();

    start(*late, get("/text"));
    ASSERT_TRUE(queue_.waitForQueued(4));
    late->cancel();
    queue_.runQueued();
    EXPECT_EQ(log_, (std::vector<std::string>{"rejected:Fetch request has been canceled", "ready"}));
    EXPECT_EQ(late->head().status, -1);
}

/** `cancelStreaming` drops what arrives later but lets the transfer complete. */
TEST_F(ExpoFetchTest, CancelStreamingDropsLaterChunks) {
    const std::shared_ptr<ExpoFetchResponse> response = makeResponse();

    response->cancelStreaming();
    start(*response, get("/trickle"));
    ASSERT_TRUE(logs("resolved"));
    response->startStreaming();
    ASSERT_TRUE(logs("data:first"));
    response->cancelStreaming();
    gate_.set_value();
    gate_ = {};
    readBody(*response);
    ASSERT_TRUE(logs("ready"));
    EXPECT_EQ(log_, (std::vector<std::string>{"resolved", "data:first", "body:", "ready"}));
}

/**
 * A response JavaScript has dropped settles nothing, as what the worker posts finds it gone, and cancels its transfer,
 * so an abandoned stream closes its connection rather than holding it open and posting chunks forever; one dropped
 * before it started has nothing to cancel.
 */
TEST_F(ExpoFetchTest, ADroppedResponseSettlesNothingAndClosesItsConnection) {
    std::shared_ptr<ExpoFetchResponse> response = makeResponse();
    std::shared_ptr<ExpoFetchResponse> unstarted = makeResponse();

    unstarted.reset();
    start(*response, get("/held"));
    ASSERT_TRUE(queue_.waitForQueued(3));
    response.reset();
    EXPECT_EQ(peerClosed_.get_future().wait_for(kBudget), std::future_status::ready);
    queue_.runQueued();
    EXPECT_TRUE(log_.empty());
}

} // namespace
} // namespace react_native_linux
