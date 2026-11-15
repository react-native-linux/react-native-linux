#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace facebook::react {
class CallInvoker;
class TurboModule;
} // namespace facebook::react

namespace react_native_linux {

class KeyValueStore;

/**
 * The file operations behind `expo-file-system`'s `File` and `Directory` (#663), on `std::filesystem`, with the
 * semantics upstream's Android module gives them: a missing parent, an existing destination without `overwrite`, or
 * a path that exists as the other kind of entry is an error, never a silent no-op. Errors are exceptions whose
 * message names the path; the module turns them into JavaScript errors.
 *
 * Threading contract: none of its own. Every caller is a `FileSystem` method on the JavaScript thread.
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
    std::filesystem::path cache;
};

/**
 * For the application directory `$XDG_DATA_HOME/<app-id>`, which holds `async-storage.sqlite` (#23) and `SQLite`
 * (#638): `Documents` inside it, and `$XDG_CACHE_HOME/<app-id>`, or `~/.cache/<app-id>` when the variable is unset.
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

/** `validatePath()`: throws when `path` exists as the other kind of entry. */
void validateEntryPath(const std::filesystem::path& path, FileSystemEntryKind kind);

[[nodiscard]] bool entryExists(const std::filesystem::path& path, FileSystemEntryKind kind);

/** `File.size`: `nullopt` when there is no file. */
[[nodiscard]] std::optional<std::uintmax_t> fileSize(const std::filesystem::path& path);

void createFile(const std::filesystem::path& path, bool intermediates, bool overwrite);

void createDirectory(const std::filesystem::path& path, bool intermediates, bool overwrite, bool idempotent);

/** `delete()`: the entry and everything under it; throws when there is nothing to delete. */
void removeEntry(const std::filesystem::path& path);

[[nodiscard]] std::string readFileBytes(const std::filesystem::path& path);

void writeFileBytes(const std::filesystem::path& path, std::string_view bytes, bool append);

/**
 * `copy` and `move`: into `destination` when it is a directory, keeping the name, or onto it when it is a file. An
 * existing target is replaced only with `overwrite`, and a directory cannot land on a file. Returns the target, which
 * a move makes the moved object's new path.
 */
std::filesystem::path relocateEntry(const std::filesystem::path& source, FileSystemEntryKind sourceKind,
                                    const std::filesystem::path& destination, FileSystemEntryKind destinationKind,
                                    bool overwrite, bool move);

/** `listAsRecords()`, sorted by path so the order does not depend on the file system. */
[[nodiscard]] std::vector<FileSystemRecord> listDirectory(const std::filesystem::path& path);

/** The `FileSystem` TurboModule over these operations; `ExpoFileSystemModule.cpp` documents it. */
std::shared_ptr<facebook::react::TurboModule>
makeExpoFileSystemModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                         std::shared_ptr<KeyValueStore> keyValueStore);

} // namespace react_native_linux
