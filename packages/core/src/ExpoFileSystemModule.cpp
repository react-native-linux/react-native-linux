#include "AsyncStorage.h"
#include "ExpoFileSystem.h"
#include "ExpoSharedObject.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
#include <ReactCommon/TurboModuleUtils.h>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <jsi/jsi.h>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

using Runtime = facebook::jsi::Runtime;
using Value = facebook::jsi::Value;

/** What a `FileSystemFile` or `FileSystemDirectory` instance carries: its path, which `move` changes. */
struct EntryState final : facebook::jsi::NativeState {
    EntryState(std::filesystem::path path, FileSystemEntryKind kind) : path(std::move(path)), kind(kind) {}

    std::filesystem::path path;
    FileSystemEntryKind kind;
};

/** The bytes `bytes()` hands JavaScript, owned by the `ArrayBuffer`. */
class ByteBuffer final : public facebook::jsi::MutableBuffer {
public:
    explicit ByteBuffer(std::string bytes) : bytes_(std::move(bytes)) {}

    [[nodiscard]] size_t size() const override { return bytes_.size(); }

    uint8_t* data() override { return reinterpret_cast<uint8_t*>(bytes_.data()); }

private:
    std::string bytes_;
};

using Method = std::function<Value(Runtime&, EntryState&, const Value*, size_t)>;

EntryState& entryOf(Runtime& runtime, const Value& value) { return *sharedObjectState<EntryState>(runtime, value); }

const Value& argument(const Value* arguments, size_t count, size_t index) {
    static const Value kUndefined;

    return index < count ? arguments[index] : kUndefined;
}

bool option(Runtime& runtime, const Value* arguments, size_t count, size_t index, const char* name) {
    const Value& options = argument(arguments, count, index);
    const Value value = options.isObject() ? options.getObject(runtime).getProperty(runtime, name) : Value();

    return value.isBool() && value.getBool();
}

/** `write(content, options)`: a string (UTF-8, or base64 with `encoding: 'base64'`), an `ArrayBuffer` or a view. */
std::string contentOf(Runtime& runtime, const Value* arguments, size_t count) {
    const Value& content = argument(arguments, count, 0);
    const Value& options = argument(arguments, count, 1);

    if (content.isString()) {
        const std::string text = content.getString(runtime).utf8(runtime);
        const Value encoding =
            options.isObject() ? options.getObject(runtime).getProperty(runtime, "encoding") : Value();

        return encoding.isString() && encoding.getString(runtime).utf8(runtime) == "base64" ? decodeBase64(text) : text;
    }

    const facebook::jsi::Object object = content.asObject(runtime);

    if (object.isArrayBuffer(runtime)) {
        facebook::jsi::ArrayBuffer buffer = object.getArrayBuffer(runtime);

        return {reinterpret_cast<const char*>(buffer.data(runtime)), buffer.size(runtime)};
    }

    facebook::jsi::ArrayBuffer buffer = object.getPropertyAsObject(runtime, "buffer").getArrayBuffer(runtime);
    const auto offset = static_cast<size_t>(object.getProperty(runtime, "byteOffset").asNumber());
    const auto length = static_cast<size_t>(object.getProperty(runtime, "byteLength").asNumber());

    return {reinterpret_cast<const char*>(buffer.data(runtime)) + offset, length};
}

Value relocate(Runtime& runtime, EntryState& entry, const Value* arguments, size_t count, bool move) {
    const EntryState& destination = entryOf(runtime, argument(arguments, count, 0));
    std::filesystem::path target = relocateEntry(entry.path, entry.kind, destination.path, destination.kind,
                                                 option(runtime, arguments, count, 1, "overwrite"), move);

    if (move) {
        entry.path = std::move(target);
    }

    return Value::undefined();
}

Value bytesOf(Runtime& runtime, const EntryState& entry) {
    const facebook::jsi::ArrayBuffer buffer(runtime, std::make_shared<ByteBuffer>(readFileBytes(entry.path)));

    return runtime.global().getPropertyAsFunction(runtime, "Uint8Array").callAsConstructor(runtime, buffer);
}

void addMethod(SharedObjectMembers& members, const std::string& name, const Method& method) {
    members.emplace_back(name,
                         [method](Runtime& runtime, const Value& thisValue, const Value* arguments, size_t count) {
                             return method(runtime, entryOf(runtime, thisValue), arguments, count);
                         });
}

/** A method and its asynchronous twin, which settles with what the method returns or rejects with its error. */
void addSyncAndAsync(SharedObjectMembers& members, const std::string& asyncName, const std::string& syncName,
                     const Method& method) {
    addMethod(members, syncName, method);
    members.emplace_back(
        asyncName, [method](Runtime& runtime, const Value& thisValue, const Value* arguments, size_t count) {
            return facebook::react::createPromiseAsJSIValue(
                runtime, [&](Runtime& promiseRuntime, const std::shared_ptr<facebook::react::Promise>& promise) {
                    try {
                        promise->resolve(method(promiseRuntime, entryOf(promiseRuntime, thisValue), arguments, count));
                    } catch (const std::exception& error) {
                        promise->reject(error.what());
                    }
                });
        });
}

SharedObjectMembers commonMethods() {
    SharedObjectMembers methods;

    addMethod(methods, "validatePath", [](Runtime&, EntryState& entry, const Value*, size_t) {
        validateEntryPath(entry.path, entry.kind);
        return Value::undefined();
    });
    addMethod(methods, "delete", [](Runtime&, EntryState& entry, const Value*, size_t) {
        removeEntry(entry.path);
        return Value::undefined();
    });
    addSyncAndAsync(methods, "copy", "copySync",
                    [](Runtime& runtime, EntryState& entry, const Value* arguments, size_t count) {
                        return relocate(runtime, entry, arguments, count, false);
                    });
    addSyncAndAsync(methods, "move", "moveSync",
                    [](Runtime& runtime, EntryState& entry, const Value* arguments, size_t count) {
                        return relocate(runtime, entry, arguments, count, true);
                    });

    return methods;
}

SharedObjectMembers fileMethods() {
    SharedObjectMembers methods = commonMethods();

    addMethod(methods, "create", [](Runtime& runtime, EntryState& entry, const Value* arguments, size_t count) {
        createFile(entry.path, option(runtime, arguments, count, 0, "intermediates"),
                   option(runtime, arguments, count, 0, "overwrite"));
        return Value::undefined();
    });
    addSyncAndAsync(methods, "text", "textSync", [](Runtime& runtime, EntryState& entry, const Value*, size_t) {
        return Value(facebook::jsi::String::createFromUtf8(runtime, readFileBytes(entry.path)));
    });
    addSyncAndAsync(methods, "base64", "base64Sync", [](Runtime& runtime, EntryState& entry, const Value*, size_t) {
        return Value(facebook::jsi::String::createFromAscii(runtime, encodeBase64(readFileBytes(entry.path))));
    });
    addSyncAndAsync(methods, "bytes", "bytesSync",
                    [](Runtime& runtime, EntryState& entry, const Value*, size_t) { return bytesOf(runtime, entry); });
    addSyncAndAsync(methods, "write", "writeSync",
                    [](Runtime& runtime, EntryState& entry, const Value* arguments, size_t count) {
                        writeFileBytes(entry.path, contentOf(runtime, arguments, count),
                                       option(runtime, arguments, count, 1, "append"));
                        return Value::undefined();
                    });

    return methods;
}

SharedObjectMembers directoryMethods() {
    SharedObjectMembers methods = commonMethods();

    addMethod(methods, "create", [](Runtime& runtime, EntryState& entry, const Value* arguments, size_t count) {
        createDirectory(entry.path, option(runtime, arguments, count, 0, "intermediates"),
                        option(runtime, arguments, count, 0, "overwrite"),
                        option(runtime, arguments, count, 0, "idempotent"));
        return Value::undefined();
    });
    addMethod(methods, "listAsRecords", [](Runtime& runtime, EntryState& entry, const Value*, size_t) {
        const std::vector<FileSystemRecord> records = listDirectory(entry.path);
        facebook::jsi::Array array(runtime, records.size());

        for (size_t index = 0; index < records.size(); ++index) {
            facebook::jsi::Object record(runtime);
            const FileSystemRecord& entryRecord = records[index];

            record.setProperty(runtime, "isDirectory", entryRecord.isDirectory);
            record.setProperty(
                runtime, "uri",
                facebook::jsi::String::createFromUtf8(
                    runtime, fileUriFromPath(entryRecord.path, entryRecord.isDirectory ? FileSystemEntryKind::Directory
                                                                                       : FileSystemEntryKind::File)));
            array.setValueAtIndex(runtime, index, record);
        }

        return Value(runtime, array);
    });

    return methods;
}

SharedObjectMembers getters(FileSystemEntryKind kind) {
    SharedObjectMembers members;

    addMethod(members, "uri", [](Runtime& runtime, EntryState& entry, const Value*, size_t) {
        return Value(facebook::jsi::String::createFromUtf8(runtime, fileUriFromPath(entry.path, entry.kind)));
    });
    addMethod(members, "exists", [](Runtime&, EntryState& entry, const Value*, size_t) {
        return Value(entryExists(entry.path, entry.kind));
    });

    if (kind == FileSystemEntryKind::File) {
        addMethod(members, "size", [](Runtime&, EntryState& entry, const Value*, size_t) {
            const std::optional<std::uintmax_t> size = fileSize(entry.path);

            return size.has_value() ? Value(static_cast<double>(size.value())) : Value::null();
        });
    }

    return members;
}

SharedObjectInitializer initializer(FileSystemEntryKind kind) {
    return [kind](Runtime& runtime, const Value* arguments, size_t count) {
        return std::make_shared<EntryState>(
            pathFromFileUri(argument(arguments, count, 0).asString(runtime).utf8(runtime)), kind);
    };
}

/**
 * `FileSystem`, the native module upstream `expo-file-system`'s `File`, `Directory` and `Paths` reach through
 * `requireNativeModule` (#663, ADR-0004). Its constants are the classes `File` and `Directory` extend, built by
 * `makeSharedObjectClass`, and `documentDirectory` and `cacheDirectory`: `Documents` inside the application directory
 * that holds AsyncStorage's file, `$XDG_DATA_HOME/<app-id>`, and `$XDG_CACHE_HOME/<app-id>`, both created on first
 * use. A headless runtime, whose AsyncStorage is `:memory:`, has neither.
 *
 * The surface is the part T3 Code calls: existence, size, create, delete, read and write as text, base64 and bytes,
 * copy, move and listing. File handles, downloads, uploads, watching, digests, pickers and previews are absent, so a
 * call to one fails as a call to an undefined function.
 *
 * Threading contract: the JavaScript thread. Every method, the asynchronous ones included, does its file work
 * before it returns; an asynchronous method returns a promise already settled with the result.
 */
class LinuxFileSystemModule final : public facebook::react::TurboModule {
public:
    LinuxFileSystemModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                          std::shared_ptr<KeyValueStore> keyValueStore)
        : TurboModule("FileSystem", std::move(jsInvoker)), keyValueStore_(std::move(keyValueStore)) {
        methodMap_["getConstants"] = {0, [](Runtime& runtime, TurboModule& turboModule, const Value*, size_t) {
                                          return static_cast<LinuxFileSystemModule&>(turboModule).constants(runtime);
                                      }};
    }

private:
    Value constants(Runtime& runtime) {
        facebook::jsi::Object constants(runtime);
        const std::filesystem::path databasePath(keyValueStore_->databasePath());

        constants.setProperty(runtime, "FileSystemFile",
                              makeSharedObjectClass(runtime, initializer(FileSystemEntryKind::File), fileMethods(),
                                                    getters(FileSystemEntryKind::File)));
        constants.setProperty(runtime, "FileSystemDirectory",
                              makeSharedObjectClass(runtime, initializer(FileSystemEntryKind::Directory),
                                                    directoryMethods(), getters(FileSystemEntryKind::Directory)));

        if (databasePath.is_absolute()) {
            const char* home = std::getenv("HOME");
            const FileSystemDirectories directories = fileSystemDirectories(
                databasePath.parent_path(), std::getenv("XDG_CACHE_HOME"), home == nullptr ? "" : home);

            std::filesystem::create_directories(directories.document);
            std::filesystem::create_directories(directories.cache);
            constants.setProperty(runtime, "documentDirectory",
                                  facebook::jsi::String::createFromUtf8(
                                      runtime, fileUriFromPath(directories.document, FileSystemEntryKind::Directory)));
            constants.setProperty(runtime, "cacheDirectory",
                                  facebook::jsi::String::createFromUtf8(
                                      runtime, fileUriFromPath(directories.cache, FileSystemEntryKind::Directory)));
        }

        return constants;
    }

    std::shared_ptr<KeyValueStore> keyValueStore_;
};

} // namespace

std::shared_ptr<facebook::react::TurboModule>
makeExpoFileSystemModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker,
                         std::shared_ptr<KeyValueStore> keyValueStore) {
    return std::make_shared<LinuxFileSystemModule>(std::move(jsInvoker), std::move(keyValueStore));
}

} // namespace react_native_linux
