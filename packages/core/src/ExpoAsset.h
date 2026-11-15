#pragma once

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
 * `<cacheDirectory>/<app-id>/expo-asset`, where `WindowMain` passes the XDG cache directory: one directory per
 * application, as AsyncStorage's file is one per application under the XDG data directory.
 */
std::filesystem::path expoAssetDirectory(const std::filesystem::path& cacheDirectory,
                                         const std::string& applicationIdentifier);

/**
 * Writes `contents` to a temporary file beside `localPath`, unique to this process and call, and renames it over
 * `localPath`, so neither a reader nor a concurrent writer in any process ever sees a partial file. A failure
 * removes the temporary file and throws upstream's `Unable to save asset to directory` message.
 */
void publishAsset(const std::filesystem::path& localPath, const std::string& contents);

/**
 * What `ExpoAsset.downloadAsync(url, md5Hash, type)` does (ADR-0004), as upstream's iOS module answers it: a
 * `file://` URL is already local and resolves unchanged; a `data:…;base64,` URL is decoded, and any other URL is
 * downloaded, once, into `directory` as `ExponentAsset-<md5Hash>.<type>`, and resolves that file's URL. Without an
 * MD5 the file is named after a hash of the URL, which keys the same reuse; Metro's asset hash is the content's
 * MD5, so a hashed file is content-addressed. `WindowSession` sets `directory` from `expoAssetDirectory`, so two
 * applications serving different bytes at one unhashed URL never share a file; a host that sets none rejects.
 *
 * Threading contract: `setDirectory` runs before the bundle does, on the thread that then hands the runtime to the
 * JavaScript thread, which is the only thread that calls `download`. `download` never waits on the network: a local,
 * cached or `data:` asset settles on the calling thread before it returns, and a remote one on the worker thread of
 * the `CurlHttpClient` created at the first remote download, which writes the file and then settles. Both callbacks
 * must therefore be safe to call from either thread. The destructor joins that worker, so no callback runs after
 * it returns, and a download still in flight then settles with neither.
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
    std::filesystem::path directory_;
    std::unique_ptr<CurlHttpClient> httpClient_;
};

/**
 * The `ExpoAsset` TurboModule upstream's unmodified `expo-asset` requires as `expo` loads, over `downloads`, which
 * must outlive it (`src/ExpoAssetModule.cpp`). Its `downloadAsync` settles a promise from whichever thread
 * `downloads` settles on.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoAssetModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, ExpoAssetDownloads& downloads);

} // namespace react_native_linux
