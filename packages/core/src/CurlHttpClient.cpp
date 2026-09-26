#include "CurlHttpClient.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstddef>
#include <folly/io/IOBuf.h>
#include <string_view>
#include <utility>

namespace react_native_linux {

namespace http = facebook::react::http;

namespace {

constexpr long kMaximumRedirects = 20;
constexpr int kPollTimeoutMilliseconds = 100;
constexpr std::string_view kFileScheme = "file://";
constexpr std::string_view kStatusLinePrefix = "HTTP/";

using EasyHandle = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;
using HeaderList = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;
using MimeHandle = std::unique_ptr<curl_mime, decltype(&curl_mime_free)>;

std::string trim(std::string_view text) {
    const auto isSpace = [](unsigned char character) { return std::isspace(character) != 0; };
    const auto first = std::find_if_not(text.begin(), text.end(), isSpace);
    const auto last = std::find_if_not(text.rbegin(), text.rend(), isSpace).base();

    return first < last ? std::string(first, last) : std::string();
}

HeaderList toHeaderList(const http::Headers& headers) {
    HeaderList list{nullptr, &curl_slist_free_all};

    for (const auto& [name, value] : headers) {
        list.reset(curl_slist_append(list.release(), (name + ": " + value).c_str()));
    }

    return list;
}

std::optional<std::string> contentTypeOf(const http::Headers& headers) {
    for (const auto& [name, value] : headers) {
        if (std::ranges::equal(name, std::string_view("content-type"),
                               [](char left, char right) { return std::tolower(left) == right; })) {
            return value;
        }
    }

    return std::nullopt;
}

} // namespace

/**
 * One request. The worker thread owns its libcurl state; `cancelled` is the only field another thread touches.
 */
struct CurlHttpClient::Transfer {
    Transfer() = default;

    http::NetworkCallbacks callbacks;
    HeaderList requestHeaders{nullptr, &curl_slist_free_all};
    MimeHandle mime{nullptr, &curl_mime_free};
    std::string requestBody;
    std::optional<std::string> setupError;
    // Declared after everything it references, so it is cleaned up first.
    EasyHandle easy{curl_easy_init(), &curl_easy_cleanup};
    http::Headers responseHeaders;
    bool responseDelivered{false};
    std::unique_ptr<folly::IOBuf> bufferedBody;
    int64_t received{0};
    curl_off_t uploaded{-1};
    std::atomic<bool> cancelled{false};

    void deliverResponseOnce() {
        if (responseDelivered) {
            return;
        }

        responseDelivered = true;
        long statusCode = 0;
        curl_easy_getinfo(easy.get(), CURLINFO_RESPONSE_CODE, &statusCode);

        if (callbacks.onResponse) {
            callbacks.onResponse(static_cast<uint16_t>(statusCode), responseHeaders);
        }
    }

    int64_t expectedLength() const {
        curl_off_t length = -1;
        curl_easy_getinfo(easy.get(), CURLINFO_CONTENT_LENGTH_DOWNLOAD_T, &length);

        return length;
    }

    void receive(const char* data, size_t size) {
        deliverResponseOnce();
        received += static_cast<int64_t>(size);

        if (callbacks.sendIncrementalUpdates) {
            callbacks.onBodyIncremental(received, expectedLength(), folly::IOBuf::copyBuffer(data, size));
            return;
        }

        std::unique_ptr<folly::IOBuf> chunk = folly::IOBuf::copyBuffer(data, size);

        if (bufferedBody) {
            bufferedBody->appendToChain(std::move(chunk));
        } else {
            bufferedBody = std::move(chunk);
        }

        if (callbacks.sendProgressUpdates) {
            callbacks.onBodyProgress(received, expectedLength());
        }
    }

    void complete(CURLcode result) {
        if (result != CURLE_OK) {
            callbacks.onResponseComplete(curl_easy_strerror(result), result == CURLE_OPERATION_TIMEDOUT);
            return;
        }

        deliverResponseOnce();

        if (!callbacks.sendIncrementalUpdates) {
            callbacks.onBody(bufferedBody ? std::move(bufferedBody) : folly::IOBuf::create(0));
        }

        callbacks.onResponseComplete("", false);
    }
};

namespace {

size_t onHeaderLine(char* data, size_t size, size_t count, void* userData) {
    auto& transfer = *static_cast<CurlHttpClient::Transfer*>(userData);
    const std::string_view line(data, size * count);

    if (line.starts_with(kStatusLinePrefix)) {
        transfer.responseHeaders.clear();
    } else if (const size_t colon = line.find(':'); colon != std::string_view::npos) {
        transfer.responseHeaders.emplace_back(trim(line.substr(0, colon)), trim(line.substr(colon + 1)));
    }

    return size * count;
}

size_t onBodyData(char* data, size_t size, size_t count, void* userData) {
    static_cast<CurlHttpClient::Transfer*>(userData)->receive(data, size * count);

    return size * count;
}

int onTransferProgress(void* userData, curl_off_t /*downloadTotal*/, curl_off_t /*downloaded*/, curl_off_t uploadTotal,
                       curl_off_t uploaded) {
    auto& transfer = *static_cast<CurlHttpClient::Transfer*>(userData);

    if (transfer.cancelled.load()) {
        return 1;
    }

    if (uploadTotal > 0 && uploaded != transfer.uploaded && transfer.callbacks.onUploadProgress) {
        transfer.uploaded = uploaded;
        transfer.callbacks.onUploadProgress(uploaded, uploadTotal);
    }

    return 0;
}

std::optional<std::string> attachFormData(CurlHttpClient::Transfer& transfer, const http::FormData& formData) {
    transfer.mime.reset(curl_mime_init(transfer.easy.get()));

    for (const http::FormDataField& field : formData) {
        curl_mimepart* part = curl_mime_addpart(transfer.mime.get());
        curl_mime_name(part, field.fieldName.c_str());

        if (field.string.has_value()) {
            curl_mime_data(part, field.string->c_str(), field.string->size());
        } else if (field.uri.has_value()) {
            const std::string_view uri = field.uri.value();
            const std::string filePath(uri.starts_with(kFileScheme) ? uri.substr(kFileScheme.size()) : uri);

            if (curl_mime_filedata(part, filePath.c_str()) != CURLE_OK) {
                return "the form-data file " + filePath + " cannot be read";
            }
        }

        if (const std::optional<std::string> contentType = contentTypeOf(field.headers); contentType.has_value()) {
            curl_mime_type(part, contentType->c_str());
        }
    }

    curl_easy_setopt(transfer.easy.get(), CURLOPT_MIMEPOST, transfer.mime.get());

    return std::nullopt;
}

std::optional<std::string> attachBody(CurlHttpClient::Transfer& transfer, const http::Body& body) {
    if (body.blob.has_value()) {
        return "a blob request body needs the Blob module, which this platform does not ship yet";
    }

    if (body.formData.has_value()) {
        return attachFormData(transfer, body.formData.value());
    }

    if (body.base64.has_value()) {
        std::optional<std::string> decoded = decodeBase64(body.base64.value());

        if (!decoded.has_value()) {
            return "the base64 request body is not valid base64";
        }

        transfer.requestBody = std::move(decoded.value());
    } else if (body.string.has_value()) {
        transfer.requestBody = body.string.value();
    } else {
        return std::nullopt;
    }

    curl_easy_setopt(transfer.easy.get(), CURLOPT_POSTFIELDS, transfer.requestBody.data());
    curl_easy_setopt(transfer.easy.get(), CURLOPT_POSTFIELDSIZE_LARGE,
                     static_cast<curl_off_t>(transfer.requestBody.size()));

    return std::nullopt;
}

void configure(CurlHttpClient::Transfer& transfer, const std::string& method, const std::string& url,
               const http::Headers& headers, uint32_t timeout) {
    CURL* easy = transfer.easy.get();

    curl_easy_setopt(easy, CURLOPT_URL, url.c_str());
    curl_easy_setopt(easy, CURLOPT_CUSTOMREQUEST, method.c_str());
    curl_easy_setopt(easy, CURLOPT_NOBODY, method == "HEAD" ? 1L : 0L);
    curl_easy_setopt(easy, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(easy, CURLOPT_MAXREDIRS, kMaximumRedirects);
    curl_easy_setopt(easy, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(easy, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout));
    curl_easy_setopt(easy, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(easy, CURLOPT_PRIVATE, &transfer);
    curl_easy_setopt(easy, CURLOPT_HEADERFUNCTION, &onHeaderLine);
    curl_easy_setopt(easy, CURLOPT_HEADERDATA, &transfer);
    curl_easy_setopt(easy, CURLOPT_WRITEFUNCTION, &onBodyData);
    curl_easy_setopt(easy, CURLOPT_WRITEDATA, &transfer);
    curl_easy_setopt(easy, CURLOPT_XFERINFOFUNCTION, &onTransferProgress);
    curl_easy_setopt(easy, CURLOPT_XFERINFODATA, &transfer);
    curl_easy_setopt(easy, CURLOPT_NOPROGRESS, 0L);
    transfer.requestHeaders = toHeaderList(headers);
    curl_easy_setopt(easy, CURLOPT_HTTPHEADER, transfer.requestHeaders.get());
}

class TransferToken final : public http::IRequestToken {
public:
    explicit TransferToken(std::shared_ptr<CurlHttpClient::Transfer> transfer) : transfer_(std::move(transfer)) {}

    void cancel() noexcept override { transfer_->cancelled.store(true); }

private:
    std::shared_ptr<CurlHttpClient::Transfer> transfer_;
};

} // namespace

CurlHttpClient::CurlHttpClient()
    : multi_((curl_global_init(CURL_GLOBAL_DEFAULT), curl_multi_init()), &curl_multi_cleanup),
      worker_([this]() { run(); }) {}

CurlHttpClient::~CurlHttpClient() {
    stopping_.store(true);
    curl_multi_wakeup(multi_.get());
    worker_.join();

    for (const std::shared_ptr<Transfer>& transfer : active_) {
        curl_multi_remove_handle(multi_.get(), transfer->easy.get());
    }
}

std::unique_ptr<http::IRequestToken> CurlHttpClient::sendRequest(http::NetworkCallbacks&& callbacks,
                                                                 const std::string& method, const std::string& url,
                                                                 const http::Headers& headers, const http::Body& body,
                                                                 uint32_t timeout,
                                                                 std::optional<std::string> /*loggingId*/) {
    std::shared_ptr<Transfer> transfer = std::make_shared<Transfer>();
    transfer->callbacks = std::move(callbacks);
    configure(*transfer, method, url, headers, timeout);

    transfer->setupError = attachBody(*transfer, body);

    {
        const std::lock_guard<std::mutex> guard(mutex_);
        pending_.push_back(transfer);
    }

    curl_multi_wakeup(multi_.get());

    return std::make_unique<TransferToken>(std::move(transfer));
}

void CurlHttpClient::adoptPendingTransfers() {
    std::vector<std::shared_ptr<Transfer>> adopted;

    {
        const std::lock_guard<std::mutex> guard(mutex_);
        adopted.swap(pending_);
    }

    for (std::shared_ptr<Transfer>& transfer : adopted) {
        if (transfer->setupError.has_value()) {
            transfer->callbacks.onResponseComplete(transfer->setupError.value(), false);
            continue;
        }

        curl_multi_add_handle(multi_.get(), transfer->easy.get());
        active_.push_back(std::move(transfer));
    }
}

void CurlHttpClient::run() {
    while (!stopping_.load()) {
        adoptPendingTransfers();

        int running = 0;
        curl_multi_perform(multi_.get(), &running);

        int queued = 0;

        while (CURLMsg* message = curl_multi_info_read(multi_.get(), &queued)) {
            Transfer* finished = nullptr;
            curl_easy_getinfo(message->easy_handle, CURLINFO_PRIVATE, &finished);
            const auto owner = std::ranges::find_if(
                active_, [finished](const std::shared_ptr<Transfer>& transfer) { return transfer.get() == finished; });
            const std::shared_ptr<Transfer> transfer = *owner;
            const CURLcode result = message->data.result;

            active_.erase(owner);
            curl_multi_remove_handle(multi_.get(), transfer->easy.get());

            if (!transfer->cancelled.load()) {
                transfer->complete(result);
            }
        }

        curl_multi_poll(multi_.get(), nullptr, 0, kPollTimeoutMilliseconds, nullptr);
    }
}

std::optional<std::string> decodeBase64(const std::string& encoded) {
    constexpr std::string_view kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    constexpr int kBitsPerCharacter = 6;
    constexpr int kBitsPerByte = 8;
    constexpr unsigned int kByteMask = 0xFFU;
    std::string decoded;
    unsigned int accumulator = 0;
    int bits = 0;

    for (const char character : encoded) {
        if (character == '=') {
            break;
        }

        const size_t value = kAlphabet.find(character);

        if (value == std::string_view::npos) {
            return std::nullopt;
        }

        accumulator = (accumulator << kBitsPerCharacter) | static_cast<unsigned int>(value);
        bits += kBitsPerCharacter;

        if (bits >= kBitsPerByte) {
            bits -= kBitsPerByte;
            decoded.push_back(static_cast<char>((accumulator >> bits) & kByteMask));
        }
    }

    return decoded;
}

} // namespace react_native_linux
