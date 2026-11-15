#include "ExpoFileSystem.h"
#include "ExpoSharedObject.h"

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>
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

#include <react/bridging/Bridging.h>
#include <react/bridging/Promise.h>

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

/** What an operation that answers nothing settles with. */
struct Completed {};

/** What `bytes()` settles with: a `Uint8Array` over the bytes read, without copying them. */
struct FileBytes {
    std::shared_ptr<ByteBuffer> buffer;
};

/** What `move()` settles with: settling it points the moved object at its new path, on the JavaScript thread. */
struct MovedEntry {
    std::shared_ptr<EntryState> entry;
    std::filesystem::path target;
};

} // namespace

} // namespace react_native_linux

template <> struct facebook::react::Bridging<react_native_linux::Completed> {
    static facebook::jsi::Value toJs(facebook::jsi::Runtime&, const react_native_linux::Completed&) {
        return facebook::jsi::Value::undefined();
    }
};

template <> struct facebook::react::Bridging<react_native_linux::FileBytes> {
    static facebook::jsi::Value toJs(facebook::jsi::Runtime& runtime, const react_native_linux::FileBytes& bytes) {
        return runtime.global()
            .getPropertyAsFunction(runtime, "Uint8Array")
            .callAsConstructor(runtime, facebook::jsi::ArrayBuffer(runtime, bytes.buffer));
    }
};

template <> struct facebook::react::Bridging<react_native_linux::MovedEntry> {
    static facebook::jsi::Value toJs(facebook::jsi::Runtime&, const react_native_linux::MovedEntry& moved) {
        moved.entry->path = moved.target;

        return facebook::jsi::Value::undefined();
    }
};

namespace react_native_linux {

namespace {

/** The work an asynchronous method hands the worker: native values only, never a JSI one. */
template <typename Result> using Operation = std::function<Result()>;

/** Reads a call's arguments on the JavaScript thread into the operation that does its file work. */
template <typename Result>
using Prepare = std::function<Operation<Result>(Runtime&, const std::shared_ptr<EntryState>&, const Value*, size_t)>;

using Method = std::function<Value(Runtime&, EntryState&, const Value*, size_t)>;

struct Context {
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker;
    FileSystemHost& host;
};

/** The instance's state, which the caller holds for the whole call: a getter it runs may `release()` the instance. */
std::shared_ptr<EntryState> entryOf(Runtime& runtime, const Value& value) {
    return sharedObjectState<EntryState>(runtime, value);
}

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

    const double byteOffset = object.getProperty(runtime, "byteOffset").asNumber();
    const double byteLength = object.getProperty(runtime, "byteLength").asNumber();
    facebook::jsi::ArrayBuffer buffer = object.getPropertyAsObject(runtime, "buffer").getArrayBuffer(runtime);
    const auto [offset, length] = byteViewRange(byteOffset, byteLength, buffer.size(runtime));

    return {reinterpret_cast<const char*>(buffer.data(runtime)) + offset, length};
}

/** `copy(destination, options)` and `move(destination, options)`, read before a getter can release the destination. */
struct Relocation {
    std::filesystem::path source;
    FileSystemEntryKind sourceKind;
    std::filesystem::path destination;
    FileSystemEntryKind destinationKind;
    bool overwrite;

    [[nodiscard]] std::filesystem::path apply(bool move) const {
        return relocateEntry(source, sourceKind, destination, destinationKind, overwrite, move);
    }
};

Relocation relocationOf(Runtime& runtime, const EntryState& entry, const Value* arguments, size_t count) {
    const std::shared_ptr<EntryState> destination = entryOf(runtime, argument(arguments, count, 0));

    return {.source = entry.path,
            .sourceKind = entry.kind,
            .destination = destination->path,
            .destinationKind = destination->kind,
            .overwrite = option(runtime, arguments, count, 1, "overwrite")};
}

void addMethod(SharedObjectMembers& members, const std::string& name, const Method& method) {
    members.emplace_back(name,
                         [method](Runtime& runtime, const Value& thisValue, const Value* arguments, size_t count) {
                             const std::shared_ptr<EntryState> entry = entryOf(runtime, thisValue);

                             return method(runtime, *entry, arguments, count);
                         });
}

/**
 * A method and its asynchronous twin. The twin reads its arguments on the JavaScript thread, does the file work on
 * the worker, and settles through an `AsyncPromise`, which hands the result back to the JavaScript thread (#77); an
 * argument it cannot read rejects the promise.
 */
template <typename Result>
void addSyncAndAsync(SharedObjectMembers& members, const Context& context, const std::string& asyncName,
                     const std::string& syncName, const Prepare<Result>& prepare) {
    members.emplace_back(syncName, [prepare](Runtime& runtime, const Value& thisValue, const Value* arguments,
                                             size_t count) {
        const std::shared_ptr<EntryState> entry = entryOf(runtime, thisValue);

        return Value(runtime, facebook::react::bridging::toJs(runtime, prepare(runtime, entry, arguments, count)()));
    });
    members.emplace_back(
        asyncName, [prepare, context](Runtime& runtime, const Value& thisValue, const Value* arguments, size_t count) {
            facebook::react::AsyncPromise<Result> promise(runtime, context.jsInvoker);
            Value promiseObject = promise.get(runtime);

            try {
                const std::shared_ptr<EntryState> entry = entryOf(runtime, thisValue);

                context.host.post([promise, operation = prepare(runtime, entry, arguments, count)]() mutable {
                    try {
                        promise.resolve(operation());
                    } catch (const std::exception& error) {
                        promise.reject(facebook::react::Error(error.what()));
                    }
                });
            } catch (const std::exception& error) {
                promise.reject(facebook::react::Error(error.what()));
            }

            return promiseObject;
        });
}

SharedObjectMembers commonMethods(const Context& context) {
    SharedObjectMembers methods;

    addMethod(methods, "validatePath", [](Runtime&, EntryState& entry, const Value*, size_t) {
        validateEntryPath(entry.path, entry.kind);
        return Value::undefined();
    });
    addMethod(methods, "delete", [](Runtime&, EntryState& entry, const Value*, size_t) {
        removeEntry(entry.path, entry.kind);
        return Value::undefined();
    });
    addSyncAndAsync<Completed>(
        methods, context, "copy", "copySync",
        [](Runtime& runtime, const std::shared_ptr<EntryState>& entry, const Value* arguments, size_t count) {
            return [relocation = relocationOf(runtime, *entry, arguments, count)] {
                static_cast<void>(relocation.apply(false));
                return Completed{};
            };
        });
    addSyncAndAsync<MovedEntry>(
        methods, context, "move", "moveSync",
        [](Runtime& runtime, const std::shared_ptr<EntryState>& entry, const Value* arguments, size_t count) {
            return [entry, relocation = relocationOf(runtime, *entry, arguments, count)] {
                return MovedEntry{.entry = entry, .target = relocation.apply(true)};
            };
        });

    return methods;
}

SharedObjectMembers fileMethods(const Context& context) {
    SharedObjectMembers methods = commonMethods(context);

    addMethod(methods, "create", [](Runtime& runtime, EntryState& entry, const Value* arguments, size_t count) {
        createFile(entry.path, option(runtime, arguments, count, 0, "intermediates"),
                   option(runtime, arguments, count, 0, "overwrite"));
        return Value::undefined();
    });
    addSyncAndAsync<std::string>(methods, context, "text", "textSync",
                                 [](Runtime&, const std::shared_ptr<EntryState>& entry, const Value*, size_t) {
                                     return [path = entry->path] { return readFileBytes(path); };
                                 });
    addSyncAndAsync<std::string>(methods, context, "base64", "base64Sync",
                                 [](Runtime&, const std::shared_ptr<EntryState>& entry, const Value*, size_t) {
                                     return [path = entry->path] { return encodeBase64(readFileBytes(path)); };
                                 });
    addSyncAndAsync<FileBytes>(methods, context, "bytes", "bytesSync",
                               [](Runtime&, const std::shared_ptr<EntryState>& entry, const Value*, size_t) {
                                   return [path = entry->path] {
                                       return FileBytes{.buffer = std::make_shared<ByteBuffer>(readFileBytes(path))};
                                   };
                               });
    addSyncAndAsync<Completed>(
        methods, context, "write", "writeSync",
        [](Runtime& runtime, const std::shared_ptr<EntryState>& entry, const Value* arguments, size_t count) {
            return [path = entry->path, bytes = contentOf(runtime, arguments, count),
                    append = option(runtime, arguments, count, 1, "append")] {
                writeFileBytes(path, bytes, append);
                return Completed{};
            };
        });

    return methods;
}

SharedObjectMembers directoryMethods(const Context& context) {
    SharedObjectMembers methods = commonMethods(context);

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
 * `makeSharedObjectClass`, and `documentDirectory` and `cacheDirectory`: `Documents` inside the host's application
 * directory, `$XDG_DATA_HOME/<app-id>`, and `$XDG_CACHE_HOME/<app-id>`, both created on first use. A host with no
 * application directory, as a headless runtime has none, offers neither, and a process with no absolute cache home
 * has no `cacheDirectory`.
 *
 * The surface is the part T3 Code calls: existence, size, create, delete, read and write as text, base64 and bytes,
 * copy, move and listing. File handles, downloads, uploads, watching, digests, pickers and previews are absent, so a
 * call to one fails as a call to an undefined function.
 *
 * Threading contract: every method runs on the JavaScript thread and reads its arguments there. A synchronous method
 * does its file work before it returns. An asynchronous one copies what it read into native values, posts the work
 * to the host's worker, and returns a pending promise; the worker settles it through the call invoker, and a `move`
 * updates the instance's path only then, on the JavaScript thread, so no `EntryState` is touched off it. The methods
 * hold the host by reference because `TurboModuleRegistry` owns it and destroys it only after the JavaScript thread
 * has quit, which runs the work still queued.
 */
class LinuxFileSystemModule final : public facebook::react::TurboModule {
public:
    LinuxFileSystemModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, FileSystemHost& host)
        : TurboModule("FileSystem", std::move(jsInvoker)), host_(host) {
        methodMap_["getConstants"] = {0, [](Runtime& runtime, TurboModule& turboModule, const Value*, size_t) {
                                          return static_cast<LinuxFileSystemModule&>(turboModule).constants(runtime);
                                      }};
    }

private:
    Value constants(Runtime& runtime) {
        facebook::jsi::Object constants(runtime);
        const std::filesystem::path& applicationDirectory = host_.applicationDirectory();
        const Context context{.jsInvoker = jsInvoker_, .host = host_};

        constants.setProperty(runtime, "FileSystemFile",
                              makeSharedObjectClass(runtime, initializer(FileSystemEntryKind::File),
                                                    fileMethods(context), getters(FileSystemEntryKind::File)));
        constants.setProperty(runtime, "FileSystemDirectory",
                              makeSharedObjectClass(runtime, initializer(FileSystemEntryKind::Directory),
                                                    directoryMethods(context),
                                                    getters(FileSystemEntryKind::Directory)));

        if (!applicationDirectory.empty()) {
            const char* home = std::getenv("HOME");
            const FileSystemDirectories directories =
                fileSystemDirectories(applicationDirectory, std::getenv("XDG_CACHE_HOME"), home == nullptr ? "" : home);

            std::filesystem::create_directories(directories.document);
            constants.setProperty(runtime, "documentDirectory",
                                  facebook::jsi::String::createFromUtf8(
                                      runtime, fileUriFromPath(directories.document, FileSystemEntryKind::Directory)));

            if (directories.cache.has_value()) {
                std::filesystem::create_directories(directories.cache.value());
                constants.setProperty(
                    runtime, "cacheDirectory",
                    facebook::jsi::String::createFromUtf8(
                        runtime, fileUriFromPath(directories.cache.value(), FileSystemEntryKind::Directory)));
            }
        }

        return constants;
    }

    FileSystemHost& host_;
};

} // namespace

std::shared_ptr<facebook::react::TurboModule>
makeExpoFileSystemModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, FileSystemHost& host) {
    return std::make_shared<LinuxFileSystemModule>(std::move(jsInvoker), host);
}

} // namespace react_native_linux
