#include "AsyncStorage.h"
#include "ReactHost.h"

#include <chrono>
#include <cstdlib>
#include <cxxreact/JSBigString.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <sys/stat.h>
#include <thread>

namespace react_native_linux {
namespace {

namespace fs = std::filesystem;

constexpr std::chrono::milliseconds kQuiescenceBudget{5000};
constexpr std::chrono::milliseconds kReaderDelay{200};
constexpr std::chrono::milliseconds kPollInterval{10};
constexpr std::chrono::seconds kRescueBudget{30};

/** Sets an environment variable, or unsets it for `nullptr`, for the object's lifetime. */
class EnvironmentOverride final {
public:
    EnvironmentOverride(const char* name, const char* value) : name_(name) {
        const char* previous = std::getenv(name);

        if (previous != nullptr) {
            previous_ = previous;
        }

        if (value == nullptr) {
            unsetenv(name);
        } else {
            setenv(name, value, 1);
        }
    }

    EnvironmentOverride(const EnvironmentOverride&) = delete;
    EnvironmentOverride(EnvironmentOverride&&) = delete;
    EnvironmentOverride& operator=(const EnvironmentOverride&) = delete;
    EnvironmentOverride& operator=(EnvironmentOverride&&) = delete;

    ~EnvironmentOverride() {
        if (previous_.has_value()) {
            setenv(name_, previous_->c_str(), 1);
        } else {
            unsetenv(name_);
        }
    }

private:
    const char* name_;
    std::optional<std::string> previous_;
};

/** Everything a writer puts into the FIFO at `path`, read once a writer has opened and closed it. */
std::string drainFifo(const fs::path& path) {
    std::ifstream fifo(path, std::ios::binary);

    return {std::istreambuf_iterator<char>(fifo), std::istreambuf_iterator<char>()};
}

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

/**
 * The trace once it reads `expected`, or what it read when the budget ran out: asynchronous methods settle from the
 * worker, so the JavaScript thread can be idle before the probe has finished.
 */
std::string waitForTrace(ReactHost& reactHost, const std::string& expected) {
    const std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::now() + kQuiescenceBudget;
    std::string trace = readTrace(reactHost);

    while (trace != expected && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(kPollInterval);
        trace = readTrace(reactHost);
    }

    return trace;
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
    return error.message.match(/cannot \w+|not a live native shared object|is a directory, not a file|do not lie inside its buffer/)?.[0] ?? error.message;
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
  const reentrant = new File(documents, 'reentrant.txt');
  reentrant.writeSync('kept', { get append() { reentrant.release(); return false; } });
  const reentrantAsync = new File(documents, 'reentrant-async.txt');
  await reentrantAsync.write('kept', { get append() { reentrantAsync.release(); return false; } });
  const relocated = new File(documents, 'relocated.txt');
  new File(documents, 'reentrant.txt').copySync(relocated, { get overwrite() { relocated.release(); return false; } });
  const reentrantCreate = new File(documents, 'reentrant-create.txt');
  reentrantCreate.create({ get overwrite() { reentrantCreate.release(); return false; } });
  trace.reentrant = [
    new File(documents, 'relocated.txt').textSync(),
    new File(documents, 'reentrant-async.txt').textSync(),
    new File(documents, 'reentrant-create.txt').exists,
  ];
  const view = (byteOffset, byteLength) => ({ buffer: new ArrayBuffer(4), byteOffset, byteLength });
  trace.badViews = [
    await failure(() => fromView.writeSync(view(2, 3))),
    ...(await Promise.all([[5, 0], [-1, 1], [1.5, 1], [NaN, 1], [0, Infinity]].map(([offset, length]) => failure(() => fromView.write(view(offset, length)))))),
  ];
  const stale = new File(documents, 'stale');
  new Directory(documents, 'stale').create();
  trace.staleDelete = await failure(() => stale.delete());
  trace.staleKept = new Directory(documents, 'stale').exists;
  await outbox.copy(new Directory(documents, 'outbox-copy'));
  outbox.moveSync(new Directory(documents, 'outbox-moved'));
  trace.directoryRelocation = [new File(new Directory(documents, 'outbox-copy'), 'binary.bin').exists, outbox.uri.endsWith('/outbox-moved/')];
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
    const EnvironmentOverride cacheHome("XDG_CACHE_HOME", (root / "cache").c_str());

    fs::remove_all(root);
    {
        ReactHost reactHost;

        reactHost.keyValueStore().setDatabasePath(
            (root / "data" / "org.example.files" / "async-storage.sqlite").string());
        reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kFileSystemProbe),
                             "ExpoFileSystemModuleTest.js");
        EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
        const std::string expected =
            R"({"documents":true,"cache":true,"outboxExists":true,"text":"hello world","textSync":"hello world",)"
            R"("size":11,"missingSize":null,"base64":"AAEC/w==","base64Sync":"AAEC/w==",)"
            R"("bytes":[true,0,1,2,255],"bytesSync":[0,1,2,255],"view":[8,7],"buffer":[5,4],)"
            R"("listed":["file:binary.bin","file:buffer.bin","file:note.txt","file:view.bin"],)"
            R"("movedUri":"hello world","copyAgain":"cannot relocate","overwrite":"resolved",)"
            R"("noteAfterMove":true,"missingRead":"cannot read","wrongKind":"is a directory, not a file",)"
            R"("notAnInstance":"not a live native shared object","reentrant":["kept","kept",true],)"
            R"("badViews":["do not lie inside its buffer","do not lie inside its buffer",)"
            R"("do not lie inside its buffer","do not lie inside its buffer","do not lie inside its buffer",)"
            R"("do not lie inside its buffer"],"staleDelete":"is a directory, not a file","staleKept":true,)"
            R"("directoryRelocation":[true,true],"deleted":true,"released":"not a live native shared object"})";

        EXPECT_EQ(waitForTrace(reactHost, expected), expected);
        EXPECT_TRUE(fs::is_directory(root / "cache" / "org.example.files"));
        EXPECT_FALSE(reactHost.hasReportedFatalError());
    }
    fs::remove_all(root);
}

// A write to a FIFO blocks in `open` until a reader opens it, which holds the operation pending for as long as the
// test likes.
constexpr char kPendingWriteProbe[] = R"JAVASCRIPT(
globalThis.expo = { SharedObject: class SharedObject {} };
const constants = globalThis.nativeModuleProxy.FileSystem.getConstants();
const order = [];
const record = (step) => { order.push(step); globalThis.fileSystemTrace = JSON.stringify(order); };
const fifo = new constants.FileSystemFile(constants.documentDirectory + 'fifo');
fifo.write('payload').then(() => record('settled'), (error) => record(error.message));
record('returned');
setTimeout(() => record('timer'), 0);
for (let index = 0; index < 8; index++) {
  new constants.FileSystemFile(`${constants.documentDirectory}queued-${index}.txt`).write(`queued ${index}`);
}
)JAVASCRIPT";

/** The application directory the pending-write probe runs in, with the FIFO it writes to already made. */
fs::path makeFifoApplication(const fs::path& root) {
    const fs::path application = root / "data" / "org.example.fifo";

    fs::remove_all(root);
    fs::create_directories(application / "Documents");
    EXPECT_EQ(mkfifo((application / "Documents" / "fifo").c_str(), S_IRUSR | S_IWUSR), 0);

    return application;
}

/** #663: an asynchronous method does its file work on the worker, so JavaScript runs on while that work is pending. */
TEST(ExpoFileSystemModuleTest, JavaScriptRunsOnWhileAnAsynchronousWriteIsPending) {
    const fs::path root = fs::temp_directory_path() / "rnl-expo-file-system-pending";
    const fs::path application = makeFifoApplication(root);
    const fs::path fifo = application / "Documents" / "fifo";
    std::promise<void> drained;
    // A write that wrongly blocks the JavaScript thread would hang the test; this unblocks it so the test fails.
    const std::future<void> rescue = std::async(std::launch::async, [&fifo, wasDrained = drained.get_future()] {
        if (wasDrained.wait_for(kRescueBudget) == std::future_status::timeout) {
            static_cast<void>(drainFifo(fifo));
        }
    });
    {
        ReactHost reactHost;

        reactHost.keyValueStore().setDatabasePath((application / "async-storage.sqlite").string());
        reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kPendingWriteProbe),
                             "ExpoFileSystemPendingTest.js");
        EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
        ASSERT_EQ(readTrace(reactHost), R"(["returned","timer"])");
        EXPECT_EQ(drainFifo(fifo), "payload");
        drained.set_value();
        EXPECT_EQ(waitForTrace(reactHost, R"(["returned","timer","settled"])"), R"(["returned","timer","settled"])");
    }
    fs::remove_all(root);
}

/**
 * Destroying the host while the worker is mid-write waits for it, runs the work queued behind it, and settles
 * their promises into a JavaScript thread that has already quit, which drops them.
 */
TEST(ExpoFileSystemModuleTest, TeardownWaitsForPendingWorkAndRunsWhatIsQueued) {
    const fs::path root = fs::temp_directory_path() / "rnl-expo-file-system-teardown";
    const fs::path application = makeFifoApplication(root);
    std::future<std::string> written;
    {
        ReactHost reactHost;

        reactHost.keyValueStore().setDatabasePath((application / "async-storage.sqlite").string());
        reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kPendingWriteProbe),
                             "ExpoFileSystemTeardownTest.js");
        written = std::async(std::launch::async, [fifo = application / "Documents" / "fifo"] {
            std::this_thread::sleep_for(kReaderDelay);
            return drainFifo(fifo);
        });
        EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
    }

    EXPECT_EQ(written.get(), "payload");
    for (int index = 0; index < 8; ++index) {
        std::ifstream queued(application / "Documents" / ("queued-" + std::to_string(index) + ".txt"));

        EXPECT_EQ(std::string(std::istreambuf_iterator<char>(queued), std::istreambuf_iterator<char>()),
                  "queued " + std::to_string(index));
    }
    fs::remove_all(root);
}

/** With neither `XDG_CACHE_HOME` nor `HOME` there is no absolute cache directory, so the module offers none. */
TEST(ExpoFileSystemModuleTest, AProcessWithNoHomeHasADocumentDirectoryButNoCacheDirectory) {
    const fs::path root = fs::temp_directory_path() / "rnl-expo-file-system-homeless";
    const EnvironmentOverride cacheHome("XDG_CACHE_HOME", nullptr);
    const EnvironmentOverride home("HOME", nullptr);

    fs::remove_all(root);
    {
        ReactHost reactHost;

        reactHost.keyValueStore().setDatabasePath((root / "org.example.homeless" / "async-storage.sqlite").string());
        reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(R"JAVASCRIPT(
globalThis.expo = { SharedObject: class SharedObject {} };
const constants = globalThis.nativeModuleProxy.FileSystem.getConstants();
globalThis.fileSystemTrace = JSON.stringify(Object.keys(constants));
)JAVASCRIPT"),
                             "ExpoFileSystemHomelessTest.js");
        EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget));
        EXPECT_EQ(readTrace(reactHost), R"(["FileSystemFile","FileSystemDirectory","documentDirectory"])");
    }
    EXPECT_FALSE(fs::exists(fs::current_path() / ".cache" / "org.example.homeless"));
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
