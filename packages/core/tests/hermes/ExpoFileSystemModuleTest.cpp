#include "AsyncStorage.h"
#include "ReactHost.h"

#include <chrono>
#include <cstdlib>
#include <cxxreact/JSBigString.h>
#include <filesystem>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace react_native_linux {
namespace {

namespace fs = std::filesystem;

constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

/** The probe's `fileSystemTrace`, or why there is none; a script that threw never sets it. */
std::string readTrace(ReactHost& reactHost) {
    const auto trace = std::make_shared<std::promise<std::string>>();
    std::future<std::string> result = trace->get_future();

    reactHost.reactInstance().getBufferedRuntimeExecutor()([trace](facebook::jsi::Runtime& runtime) {
        const facebook::jsi::Value value = runtime.global().getProperty(runtime, "fileSystemTrace");

        trace->set_value(value.isString() ? value.getString(runtime).utf8(runtime) : "the probe never finished");
    });

    return result.wait_for(kQuiescenceBudget) == std::future_status::ready ? result.get()
                                                                           : "the runtime never answered";
}

// The calls upstream expo-file-system's File.ts, Directory.ts and Paths.ts make, over a stand-in for the
// polyfill's SharedObject and for Paths.join; the e2e runs the real package.
constexpr char kFileSystemProbe[] = R"JAVASCRIPT(
class SharedObject { release() {} }
globalThis.expo = { SharedObject };
const ExpoFileSystem = globalThis.nativeModuleProxy.FileSystem;
const constants = ExpoFileSystem.getConstants();
const join = (base, name) => `${typeof base === 'string' ? base : base.uri}${base.uri?.endsWith('/') ? '' : '/'}${name}`;
class File extends constants.FileSystemFile {
  constructor(...uris) { super(uris.length === 1 ? uris[0] : join(uris[0], uris[1])); this.validatePath(); }
}
class Directory extends constants.FileSystemDirectory {
  constructor(...uris) { super(uris.length === 1 ? uris[0] : join(uris[0], uris[1])); this.validatePath(); }
  list() { return this.listAsRecords().map(({ isDirectory, uri }) => isDirectory ? new Directory(uri) : new File(uri)); }
}
const failure = async (work) => {
  try {
    await work();
    return 'resolved';
  } catch (error) {
    return error.message.match(/cannot \w+|not a live native shared object|is a directory, not a file/)?.[0] ?? error.message;
  }
};

(async () => {
  const documents = new Directory(constants.documentDirectory);
  const outbox = new Directory(documents, 'outbox');
  outbox.create({ intermediates: true, idempotent: true });
  outbox.create({ idempotent: true });
  const note = new File(outbox, 'note.txt');
  await note.write('hello');
  await note.write(' world', { append: true });
  const binary = new File(outbox, 'binary.bin');
  binary.writeSync('AAEC/w==', { encoding: 'base64' });
  const bytes = await binary.bytes();
  const fromView = new File(outbox, 'view.bin');
  fromView.writeSync(new Uint8Array([9, 8, 7, 6]).subarray(1, 3));
  const fromBuffer = new File(outbox, 'buffer.bin');
  await fromBuffer.write(new Uint8Array([5, 4]).buffer);
  const copied = new File(documents, 'copy.txt');
  await note.copy(copied);
  const moved = new File(documents, 'moved.txt');
  copied.moveSync(moved);
  const trace = {
    documents: constants.documentDirectory.endsWith('/org.example.files/Documents/'),
    cache: constants.cacheDirectory.endsWith('/cache/org.example.files/'),
    outboxExists: outbox.exists,
    text: await note.text(),
    textSync: note.textSync(),
    size: note.size,
    missingSize: new File(outbox, 'missing.txt').size,
    base64: await binary.base64(),
    base64Sync: binary.base64Sync(),
    bytes: [bytes instanceof Uint8Array, ...bytes],
    bytesSync: [...binary.bytesSync()],
    view: [...fromView.bytesSync()],
    buffer: [...fromBuffer.bytesSync()],
    listed: outbox.list().map((entry) => `${entry instanceof File ? 'file' : 'directory'}:${entry.uri.split('/').pop()}`),
    movedUri: copied.uri === moved.uri && copied.textSync(),
    copyAgain: await failure(() => note.copy(moved)),
    overwrite: await failure(() => note.move(moved, { overwrite: true })),
    noteAfterMove: note.uri.endsWith('/moved.txt'),
    missingRead: await failure(() => new File(outbox, 'missing.txt').text()),
    wrongKind: await failure(() => new File(constants.documentDirectory)),
    notAnInstance: await failure(() => note.copySync({})),
  };
  outbox.delete();
  trace.deleted = !outbox.exists && !new File(outbox, 'binary.bin').exists;
  const released = new File(documents, 'moved.txt');
  released.release();
  trace.released = await failure(() => released.exists);
  globalThis.fileSystemTrace = JSON.stringify(trace);
})().catch((error) => { globalThis.fileSystemTrace = `failed: ${error.message}`; });
)JAVASCRIPT";

/**
 * #663: the `FileSystem` module's classes, extended as upstream's `File` and `Directory` extend them, place
 * `Paths.document` beside AsyncStorage's file and `Paths.cache` under `$XDG_CACHE_HOME`, and read, write, copy, move,
 * list and delete there; an instance passed back to a native method is resolved by its native state.
 */
TEST(ExpoFileSystemModuleTest, UpstreamFileAndDirectorySubclassesWorkInTheApplicationDirectories) {
    const fs::path root = fs::temp_directory_path() / "rnl-expo-file-system-module";
    const char* previousCacheHome = std::getenv("XDG_CACHE_HOME");
    const std::string savedCacheHome = previousCacheHome == nullptr ? "" : previousCacheHome;

    fs::remove_all(root);
    setenv("XDG_CACHE_HOME", (root / "cache").c_str(), 1);
    {
        ReactHost reactHost;

        reactHost.keyValueStore().setDatabasePath(
            (root / "data" / "org.example.files" / "async-storage.sqlite").string());
        reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kFileSystemProbe),
                             "ExpoFileSystemModuleTest.js");
        EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
        EXPECT_EQ(readTrace(reactHost),
                  R"({"documents":true,"cache":true,"outboxExists":true,"text":"hello world","textSync":"hello world",)"
                  R"("size":11,"missingSize":null,"base64":"AAEC/w==","base64Sync":"AAEC/w==",)"
                  R"("bytes":[true,0,1,2,255],"bytesSync":[0,1,2,255],"view":[8,7],"buffer":[5,4],)"
                  R"("listed":["file:binary.bin","file:buffer.bin","file:note.txt","file:view.bin"],)"
                  R"("movedUri":"hello world","copyAgain":"cannot relocate","overwrite":"resolved",)"
                  R"("noteAfterMove":true,"missingRead":"cannot read","wrongKind":"is a directory, not a file",)"
                  R"("notAnInstance":"not a live native shared object","deleted":true,)"
                  R"("released":"not a live native shared object"})");
        EXPECT_TRUE(fs::is_directory(root / "cache" / "org.example.files"));
        EXPECT_FALSE(reactHost.hasReportedFatalError());
    }
    if (previousCacheHome == nullptr) {
        unsetenv("XDG_CACHE_HOME");
    } else {
        setenv("XDG_CACHE_HOME", savedCacheHome.c_str(), 1);
    }
    fs::remove_all(root);
}

/** A headless runtime's AsyncStorage has no file, so the module has the classes but no directories. */
TEST(ExpoFileSystemModuleTest, AHeadlessRuntimeHasTheClassesButNoDirectories) {
    ReactHost reactHost;

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(R"JAVASCRIPT(
globalThis.expo = { SharedObject: class SharedObject {} };
const constants = globalThis.nativeModuleProxy.FileSystem.getConstants();
globalThis.fileSystemTrace = JSON.stringify(Object.keys(constants).map((key) => `${key}:${typeof constants[key]}`));
)JAVASCRIPT"),
                         "ExpoFileSystemHeadlessTest.js");
    EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
    EXPECT_EQ(readTrace(reactHost), R"(["FileSystemFile:function","FileSystemDirectory:function"])");
}

} // namespace
} // namespace react_native_linux
