#include "ExpoAsset.h"

#include "CurlHttpClient.h"

#include <atomic>
#include <boost/uuid/detail/md5.hpp>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <folly/io/IOBuf.h>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iterator>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace react_native_linux {
namespace {

constexpr std::string_view kFileScheme = "file://";
constexpr std::string_view kDataScheme = "data:";
constexpr std::string_view kBase64Parameter = ";base64";
constexpr uint32_t kDownloadTimeoutMilliseconds = 60'000;
constexpr uint16_t kStatusClassDivisor = 100;
constexpr uint16_t kSuccessStatusClass = 2;

std::string downloadFailure(const std::string& url) { return "Unable to download asset from url: '" + url + "'"; }

std::optional<std::string> decodeDataUrl(const std::string& url) {
    const size_t comma = url.find(',');

    if (comma == std::string::npos || !std::string_view(url).substr(0, comma).ends_with(kBase64Parameter)) {
        return std::nullopt;
    }

    return decodeBase64(url.substr(comma + 1));
}

std::string contentMd5(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    const std::string contents{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
    boost::uuids::detail::md5 hash;
    boost::uuids::detail::md5::digest_type digest{};
    std::ostringstream hex;

    hash.process_bytes(contents.data(), contents.size());
    hash.get_digest(digest);

    hex << std::hex << std::setfill('0');

    for (const auto word : digest) {
        hex << std::setw(static_cast<int>(sizeof(word) * 2)) << static_cast<uint32_t>(word);
    }

    return hex.str();
}

} // namespace

void publishAsset(const std::filesystem::path& localPath, const std::string& contents) {
    static std::atomic<uint64_t> nextTemporaryFile{0};
    const std::filesystem::path temporaryPath = localPath.string() + "." + std::to_string(::getpid()) + "-" +
                                                std::to_string(nextTemporaryFile.fetch_add(1)) + ".download";
    std::error_code ignored;
    std::error_code renameError;

    std::filesystem::create_directories(localPath.parent_path(), ignored);

    std::ofstream stream(temporaryPath, std::ios::binary);

    stream << contents;
    stream.close();

    if (!stream.fail()) {
        std::filesystem::rename(temporaryPath, localPath, renameError);
    }

    if (stream.fail() || renameError) {
        std::filesystem::remove(temporaryPath, ignored);

        throw std::runtime_error("Unable to save asset to directory: '" + std::string(kFileScheme) +
                                 localPath.parent_path().string() + "'");
    }
}

ExpoAssetDownloads::ExpoAssetDownloads() = default;

ExpoAssetDownloads::~ExpoAssetDownloads() = default;

void ExpoAssetDownloads::setDirectory(std::filesystem::path directory) { directory_ = std::move(directory); }

void ExpoAssetDownloads::download(const std::string& url, const std::optional<std::string>& md5Hash,
                                  const std::string& type, Resolve resolve, Reject reject) {
    if (url.starts_with(kFileScheme)) {
        resolve(url);
        return;
    }

    worker_.post([this, url, md5Hash, type, resolve = std::move(resolve), reject = std::move(reject)]() {
        downloadOnWorker(url, md5Hash, type, resolve, reject);
    });
}

void ExpoAssetDownloads::downloadOnWorker(const std::string& url, const std::optional<std::string>& md5Hash,
                                          const std::string& type, const Resolve& resolve, const Reject& reject) {
    if (directory_.empty()) {
        reject("Unable to save asset to directory: ''");
        return;
    }

    const std::string cacheFileId = md5Hash.value_or(std::to_string(std::hash<std::string>{}(url)));
    const std::filesystem::path localPath = directory_ / ("ExponentAsset-" + cacheFileId + "." + type);
    const std::string fileUrl = std::string(kFileScheme) + localPath.string();

    std::error_code ignored;

    if (std::filesystem::is_regular_file(localPath, ignored) &&
        (!md5Hash.has_value() || contentMd5(localPath) == md5Hash.value())) {
        resolve(fileUrl);
        return;
    }

    const auto publish = [localPath, fileUrl, resolve, reject](const std::string& contents) {
        try {
            publishAsset(localPath, contents);
        } catch (const std::exception& error) {
            reject(error.what());
            return;
        }

        resolve(fileUrl);
    };

    if (url.starts_with(kDataScheme)) {
        const std::optional<std::string> decoded = decodeDataUrl(url);

        if (decoded.has_value()) {
            publish(decoded.value());
        } else {
            reject(downloadFailure(url));
        }

        return;
    }

    const auto successBody = std::make_shared<std::optional<std::string>>();
    facebook::react::http::NetworkCallbacks callbacks;

    callbacks.onResponse = [successBody](uint16_t status, const facebook::react::http::Headers& /*headers*/) {
        if (status / kStatusClassDivisor == kSuccessStatusClass) {
            successBody->emplace();
        }
    };
    callbacks.onBody = [successBody](std::unique_ptr<folly::IOBuf> body) {
        if (successBody->has_value()) {
            const folly::ByteRange bytes = body->coalesce();

            successBody->value().assign(bytes.begin(), bytes.end());
        }
    };
    callbacks.onResponseComplete = [successBody, url, publish, reject](const std::string& error,
                                                                       bool /*timeoutError*/) {
        if (error.empty() && successBody->has_value()) {
            publish(successBody->value());
        } else {
            reject(downloadFailure(url));
        }
    };

    if (httpClient_ == nullptr) {
        httpClient_ = std::make_unique<CurlHttpClient>();
    }

    httpClient_->sendRequest(std::move(callbacks), "GET", url, {}, {}, kDownloadTimeoutMilliseconds, std::nullopt);
}

} // namespace react_native_linux
