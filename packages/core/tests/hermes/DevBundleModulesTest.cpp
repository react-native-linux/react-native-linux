#include "FantomTester.h"
#include "ReactHost.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <jsi/jsi.h>
#include <string>

// #79: what a `dev=true` bundle needs from the host before it can render — `SourceCode` naming the bundle's URL,
// a `DevSettings`, an `ImageLoader`, and the `hasComponent` global `UIManager.hasViewManagerConfig` answers
// through. metro-golden.spec.ts renders a real `dev=true` bundle from Metro; this proves each piece on its own.

namespace react_native_linux {
namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{5000};

constexpr char kDevModulesProbe[] = R"JAVASCRIPT(
const turboModuleProxy = globalThis.__turboModuleProxy;
const nativeModule = (name) =>
  typeof turboModuleProxy === 'function' ? turboModuleProxy(name) : globalThis.nativeModuleProxy[name];

nativeModule('DevSettings').addMenuItem('ignored');

globalThis.probe = { scriptURL: nativeModule('SourceCode').getConstants().scriptURL };

const keepAlive = setInterval(() => {}, 10);

nativeModule('ImageLoader')
  .getSize('file:///rnl-dev-bundle-modules.png')
  .then(
    () => { globalThis.probe.getSize = 'resolved'; },
    (error) => { globalThis.probe.getSize = String(error?.message ?? error); },
  )
  .finally(() => clearInterval(keepAlive));
)JAVASCRIPT";

std::string readProbe(ReactHost& reactHost) {
    std::promise<std::string> probe;

    reactHost.reactInstance().getBufferedRuntimeExecutor()([&probe](facebook::jsi::Runtime& runtime) {
        const facebook::jsi::Function stringify =
            runtime.global().getPropertyAsObject(runtime, "JSON").getPropertyAsFunction(runtime, "stringify");

        probe.set_value(
            stringify.call(runtime, runtime.global().getProperty(runtime, "probe")).getString(runtime).utf8(runtime));
    });

    return probe.get_future().get();
}

TEST(DevBundleModulesTest, SourceCodeNamesTheLoadedBundleAndTheOtherDevModulesAnswer) {
    const std::filesystem::path bundlePath = std::filesystem::temp_directory_path() / "rnl-dev-bundle-modules-test.js";

    std::ofstream{bundlePath} << kDevModulesProbe;

    ReactHost reactHost;

    reactHost.loadBundle(bundlePath.string());

    EXPECT_TRUE(reactHost.runUntilQuiescent(kQuiescenceBudget)) << "the getSize promise never settled";
    EXPECT_EQ(readProbe(reactHost), R"({"scriptURL":")" + bundlePath.string() +
                                        R"(","getSize":"Failed to get image size: image loader is not available."})");
    EXPECT_FALSE(reactHost.hasReportedFatalError());

    std::filesystem::remove(bundlePath);
}

TEST(DevBundleModulesTest, HasComponentAnswersFromTheRegisteredComponents) {
    FantomTester tester{{.width = 100, .height = 100}};

    tester.runTask(R"JAVASCRIPT(
const hasComponent = globalThis.__nativeComponentRegistry__hasComponent;
const instanceHandle = {};

globalThis.instanceHandle = instanceHandle;

const node = globalThis.nativeFabricUIManager.createNode(2, 'View', 1, {
  testID: `View=${hasComponent('View')} TextInput=${hasComponent('TextInput')} DebuggingOverlay=${hasComponent('DebuggingOverlay')}`,
  collapsable: false,
}, instanceHandle);
const childSet = globalThis.nativeFabricUIManager.createChildSet();

globalThis.nativeFabricUIManager.appendChildToSet(childSet, node);
globalThis.nativeFabricUIManager.completeRoot(1, childSet);
)JAVASCRIPT");

    EXPECT_NE(tester.mountTreeText().find(R"(testID="View=true TextInput=true DebuggingOverlay=false")"),
              std::string::npos)
        << tester.mountTreeText();
}

} // namespace
} // namespace react_native_linux
