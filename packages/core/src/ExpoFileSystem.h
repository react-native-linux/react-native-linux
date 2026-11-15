#pragma once

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace facebook::react {
class CallInvoker;
class TurboModule;
} // namespace facebook::react

namespace react_native_linux {

/**
 * The file operations behind `expo-file-system`'s `File` and `Directory` (#663), on `std::filesystem`, with the
 * semantics upstream's Android module gives them: a missing parent, an existing destination without `overwrite`, or
 * a path that exists as the other kind of entry is an error, never a silent no-op. Errors are exceptions whose
 * message names the path; the module turns them into JavaScript errors.
 *
 * Threading contract: none of its own; the functions share no state. `FileSystem`'s synchronous methods call them on
 * the JavaScript thread and its asynchronous ones on `FileSystemHost`'s thread.
 */
enum class FileSystemEntryKind : std::uint8_t { File, Directory };

/** What `Directory.listAsRecords()` answers per entry. */
struct FileSystemRecord {
    bool isDirectory;
    std::filesystem::path path;
};

/** `Paths.document` and `Paths.cache`. */
struct FileSystemDirectories {
    std::filesystem::path document;
    std::optional<std::filesystem::path> cache;
};

/**
 * For the application directory `$XDG_DATA_HOME/<app-id>`, which holds `async-storage.sqlite` (#23) and `SQLite`
 * (#638): `Documents` inside it, and `$XDG_CACHE_HOME/<app-id>`, or `~/.cache/<app-id>` when the variable is unset.
 * With neither a cache home nor an absolute home there is no cache directory, rather than one relative to wherever
 * the process happens to run.
 */
[[nodiscard]] FileSystemDirectories fileSystemDirectories(const std::filesystem::path& applicationDirectory,
                                                          const char* cacheHome, const std::filesystem::path& home);

/**
 * A `file:///` URI, the `file:/` form upstream's `Paths.join` makes when no WHATWG `URL` is installed, or an
 * absolute path, as the normalized path it names, without a trailing separator. Anything else throws
 * `std::invalid_argument`.
 */
[[nodiscard]] std::filesystem::path pathFromFileUri(std::string_view uri);

/** The percent-encoded `file://` URI of an absolute path; a directory's ends in `/`, as upstream's do. */
[[nodiscard]] std::string fileUriFromPath(const std::filesystem::path& path, FileSystemEntryKind kind);

[[nodiscard]] std::string encodeBase64(std::string_view bytes);

/** Throws `std::invalid_argument` for text that is not padded standard base64. */
[[nodiscard]] std::string decodeBase64(std::string_view text);

/**
 * The bytes a typed array or `DataView` covers inside its `ArrayBuffer` of `bufferSize` bytes. Throws
 * `std::invalid_argument` unless both are nonnegative integers and the range lies inside the buffer.
 */
[[nodiscard]] std::pair<size_t, size_t> byteViewRange(double byteOffset, double byteLength, size_t bufferSize);

/**
 * `validatePath()`: throws when `path` exists as the other kind of entry. Every operation that changes an entry checks
 * this first, because an instance can outlive what was at its path when it was made.
 */
void validateEntryPath(const std::filesystem::path& path, FileSystemEntryKind kind);

[[nodiscard]] bool entryExists(const std::filesystem::path& path, FileSystemEntryKind kind);

/** `File.size`: `nullopt` when there is no file. */
[[nodiscard]] std::optional<std::uintmax_t> fileSize(const std::filesystem::path& path);

void createFile(const std::filesystem::path& path, bool intermediates, bool overwrite);

void createDirectory(const std::filesystem::path& path, bool intermediates, bool overwrite, bool idempotent);

/** `delete()`: the entry and everything under it; throws when there is nothing to delete. */
void removeEntry(const std::filesystem::path& path, FileSystemEntryKind kind);

[[nodiscard]] std::string readFileBytes(const std::filesystem::path& path);

/** Throws when the bytes did not all reach the file, including when closing it fails. */
void writeFileBytes(const std::filesystem::path& path, std::string_view bytes, bool append);

/**
 * `copy` and `move`, as upstream's Android `getMoveOrCopyPath` places them: a file goes into a destination directory,
 * keeping its name; a directory goes into one that exists and otherwise becomes it; and a file destination is the
 * target itself. A directory cannot land on a file, and nothing relocates onto itself. An existing target is replaced
 * only with `overwrite`: a file by an atomic rename or an overwriting copy, so a failure leaves it as it was, and a
 * directory by removing it first. Returns the target, which a move makes the moved object's new path.
 */
std::filesystem::path relocateEntry(const std::filesystem::path& source, FileSystemEntryKind sourceKind,
                                    const std::filesystem::path& destination, FileSystemEntryKind destinationKind,
                                    bool overwrite, bool move);

/** `listAsRecords()`, sorted by path so the order does not depend on the file system. */
[[nodiscard]] std::vector<FileSystemRecord> listDirectory(const std::filesystem::path& path);

/**
 * What the `FileSystem` module runs over: the application directory, which `WindowSession` sets to
 * `$XDG_DATA_HOME/<app-id>`, beside AsyncStorage's file and `ExpoSQLite`'s `SQLite`, and the thread its asynchronous
 * methods do their file work on, so a large read or write never stalls the JavaScript thread. A host that sets no
 * directory, as a headless runtime does not, has no `Paths.document` or `Paths.cache`.
 *
 * Threading contract: `setApplicationDirectory` runs before the bundle does, on the thread that then hands the
 * runtime to the JavaScript thread, which is the only one that reads `applicationDirectory`. `post` may be called
 * from any thread and never waits on a task. Tasks run on the one thread this object owns, which the first `post`
 * starts, in the order they were posted, and must not throw. The destructor runs every task already posted, so a
 * queued write still lands, then joins the thread if one started.
 */
class FileSystemHost final {
public:
    using Task = std::function<void()>;

    FileSystemHost() = default;
    FileSystemHost(const FileSystemHost&) = delete;
    FileSystemHost(FileSystemHost&&) = delete;
    FileSystemHost& operator=(const FileSystemHost&) = delete;
    FileSystemHost& operator=(FileSystemHost&&) = delete;
    ~FileSystemHost() noexcept;

    void setApplicationDirectory(std::filesystem::path applicationDirectory);
    [[nodiscard]] const std::filesystem::path& applicationDirectory() const noexcept;
    void post(Task task);

private:
    void runTasks();

    std::filesystem::path applicationDirectory_;

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Task> tasks_;
    bool stopping_{false};
    std::thread thread_;
};

/**
 * The `FileSystem` TurboModule over these operations and `host`, which must outlive every JavaScript call;
 * `ExpoFileSystemModule.cpp` documents it.
 */
std::shared_ptr<facebook::react::TurboModule>
makeExpoFileSystemModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, FileSystemHost& host);

} // namespace react_native_linux
