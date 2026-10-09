#pragma once

#include <atomic>
#include <cstdint>
#include <curl/curl.h>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <react/http/IHttpClient.h>

namespace react_native_linux {

/**
 * `ReactCxxPlatform`'s `IHttpClient` over libcurl, which is what makes upstream's `NetworkingModule` — and so
 * `fetch`, `XMLHttpRequest` and everything built on them — work on this platform (#79). TLS is libcurl's, against
 * the distribution's trust store: a Linux distribution owns its CA bundle, not us.
 *
 * The Networking module is a streaming contract, which is the lesson of react-native-windows#9510, #10036,
 * #11439 and #2460. A request with `sendIncrementalUpdates` delivers every received chunk to `onBodyIncremental`
 * as it arrives and is never buffered; any other request is buffered without a ceiling and delivered once to
 * `onBody`, with `onBodyProgress` per chunk when `sendProgressUpdates` asks for it.
 *
 * Threading contract: `sendRequest`, and `cancel` on the token it returns, may be called from any thread and never
 * block. Every callback runs on the one worker thread this client owns, which drives libcurl's multi interface. A
 * cancelled request, and every request still in flight when the client is destroyed, completes with no further
 * callback. The destructor joins the worker, so no callback runs after it returns; a token may outlive the client.
 * Cookies are shared by this client's requests, only on the worker thread. Completed handles detach before a
 * callback; teardown detaches remaining handles after joining, so tokens may outlive the cookie store as well.
 */
class CurlHttpClient final : public facebook::react::IHttpClient {
public:
    CurlHttpClient();
    CurlHttpClient(const CurlHttpClient&) = delete;
    CurlHttpClient(CurlHttpClient&&) = delete;
    CurlHttpClient& operator=(const CurlHttpClient&) = delete;
    CurlHttpClient& operator=(CurlHttpClient&&) = delete;
    ~CurlHttpClient() override;

    std::unique_ptr<facebook::react::http::IRequestToken>
    sendRequest(facebook::react::http::NetworkCallbacks&& callbacks, const std::string& method, const std::string& url,
                const facebook::react::http::Headers& headers, const facebook::react::http::Body& body,
                uint32_t timeout, std::optional<std::string> loggingId) override;

    struct Transfer;

private:
    void run();
    void adoptPendingTransfers();

    std::mutex mutex_;
    std::vector<std::shared_ptr<Transfer>> pending_;
    std::atomic<bool> stopping_{false};
    std::unique_ptr<CURLSH, decltype(&curl_share_cleanup)> cookieStore_;
    std::vector<std::shared_ptr<Transfer>> active_;
    std::unique_ptr<CURLM, decltype(&curl_multi_cleanup)> multi_;
    std::thread worker_;
};

/**
 * The body of a GET to `url`, which is how a JavaScript bundle is loaded from a Metro dev server (#79). A blocking
 * libcurl easy transfer rather than this client's multi interface: it follows no redirect and has a deadline,
 * neither of which the Networking contract above wants. An HTTP status other than 2xx throws first, naming the URL
 * and carrying the body, because a bundle Metro failed to build arrives as an HTTP 500 whose body is the build
 * error as JSON; evaluating it as script would bury that error under a syntax error. A transfer error throws next.
 */
std::string fetchBundle(const std::string& url);

/** Decodes standard base64, ignoring padding; `std::nullopt` for any character outside the alphabet. */
std::optional<std::string> decodeBase64(const std::string& encoded);

} // namespace react_native_linux
