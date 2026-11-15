#pragma once

#include "SerialWorker.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace facebook::react {
class CallInvoker;
class TurboModule;
} // namespace facebook::react

namespace react_native_linux {

class CurlHttpClient;

/**
 * Writes `contents` to a temporary file beside `localPath`, unique to this process and call, and renames it over
 * `localPath`, so neither a reader nor a concurrent writer in any process ever sees a partial file. A failure
 * removes the temporary file and throws upstream's `Unable to save asset to directory` message.
 */
void publishAsset(const std::filesystem::path& localPath, const std::string& contents);

/**
 * What `ExpoAsset.downloadAsync(url, md5Hash, type)` does (ADR-0004), as upstream's iOS and Android modules answer
 * it: a `file://` URL is already local and resolves unchanged; a `data:…;base64,` URL is decoded, and any other URL
 * is downloaded into `directory` as `ExponentAsset-<md5Hash>.<type>`, and resolves that file's URL. That file is
 * reused while its contents still match `md5Hash`, and downloaded again once they do not; without an MD5 the file is
 * named after a hash of the URL and reused as it is. Metro's asset hash is the content's MD5, so a hashed file is
 * content-addressed. `WindowSession` sets `directory` to `expo-asset` inside the application's `FileSystem` cache
 * directory, `$XDG_CACHE_HOME/<app-id>`, so two applications serving different bytes at one unhashed URL never share
 * a file; a host that sets none rejects.
 *
 * Threading contract: `setDirectory` runs before the bundle does, on the thread that then hands the runtime to the
 * JavaScript thread, which is the only thread that calls `download`. `download` never touches the disk or the
 * network: a `file://` URL settles on the calling thread before it returns, and every other one on this object's
 * `SerialWorker`, which checks the cache, hashes and writes files, or on the worker thread of the `CurlHttpClient`
 * created at the first remote download, which writes the file and then settles. Both callbacks must therefore be
 * safe to call from any thread. The destructor drains the `SerialWorker` and then joins the HTTP worker, so no
 * callback runs after it returns, and a remote download still in flight then settles with neither.
 */
class ExpoAssetDownloads final {
public:
    using Resolve = std::function<void(const std::string& fileUrl)>;
    using Reject = std::function<void(const std::string& message)>;

    ExpoAssetDownloads();
    ExpoAssetDownloads(const ExpoAssetDownloads&) = delete;
    ExpoAssetDownloads(ExpoAssetDownloads&&) = delete;
    ExpoAssetDownloads& operator=(const ExpoAssetDownloads&) = delete;
    ExpoAssetDownloads& operator=(ExpoAssetDownloads&&) = delete;
    ~ExpoAssetDownloads();

    void setDirectory(std::filesystem::path directory);
    void download(const std::string& url, const std::optional<std::string>& md5Hash, const std::string& type,
                  Resolve resolve, Reject reject);

private:
    void downloadOnWorker(const std::string& url, const std::optional<std::string>& md5Hash, const std::string& type,
                          const Resolve& resolve, const Reject& reject);

    std::filesystem::path directory_;
    std::unique_ptr<CurlHttpClient> httpClient_;
    SerialWorker worker_;
};

/**
 * The `ExpoAsset` TurboModule upstream's unmodified `expo-asset` requires as `expo` loads, over `downloads`, which
 * must outlive it (`src/ExpoAssetModule.cpp`). Its `downloadAsync` settles a promise from whichever thread
 * `downloads` settles on.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoAssetModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, ExpoAssetDownloads& downloads);

} // namespace react_native_linux
