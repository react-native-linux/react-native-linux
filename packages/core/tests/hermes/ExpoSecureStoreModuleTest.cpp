#include "ExpoSecureStore.h"
#include "OnJavaScriptThread.h"
#include "ReactHost.h"

#include <cxxreact/JSBigString.h>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace react_native_linux {
namespace {

/**
 * What expo-secure-store 58's `SecureStore.ts` passes to its native module, in its argument orders: `getItemAsync(key,
 * options)` is `getValueWithKeyAsync(key, options)`, `setItemAsync(key, value, options)` is
 * `setValueWithKeyAsync(value, key, options)`, and `getItem` and `setItem` are the `Sync` variants.
 */
constexpr char kUpstreamCalls[] = R"JAVASCRIPT(
var ExpoSecureStore = globalThis.nativeModuleProxy.ExpoSecureStore;
var lines = [];
var deviceOnly = { keychainAccessible: ExpoSecureStore.getConstants().WHEN_UNLOCKED_THIS_DEVICE_ONLY };
var settled = async (work) => {
  try {
    await work();
    lines.push('resolved');
  } catch (error) {
    lines.push(error.message);
  }
};
var run = async (probe) => {
  try {
    await probe();
  } catch (error) {
    lines.push(`FAILED ${error.message}`);
  }
  globalThis.secureStoreTrace = lines.join(' | ');
};
)JAVASCRIPT";

constexpr char kWriteProbe[] = R"JAVASCRIPT(
run(async () => {
  lines.push(JSON.stringify(ExpoSecureStore.getConstants()));
  lines.push(`biometrics ${ExpoSecureStore.canUseBiometricAuthentication()}`);
  lines.push(`missing ${await ExpoSecureStore.getValueWithKeyAsync('dpop-proof-key', {})}`);
  lines.push(`set ${await ExpoSecureStore.setValueWithKeyAsync('{"kty":"EC"}', 'dpop-proof-key', {})}`);
  lines.push(`set ${ExpoSecureStore.setValueWithKeySync('null', 'voice-grant', deviceOnly)}`);
  ExpoSecureStore.setValueWithKeySync('{"grant":1}', 'voice-grant', deviceOnly);
  await ExpoSecureStore.setValueWithKeyAsync('other service', 'dpop-proof-key', { keychainService: 'other' });
  await ExpoSecureStore.setValueWithKeyAsync('deleted', 'relay-token', {});
  await ExpoSecureStore.deleteValueWithKeyAsync('relay-token', {});
  lines.push(`read ${await ExpoSecureStore.getValueWithKeyAsync('dpop-proof-key', {})}`);
  lines.push(`sync ${ExpoSecureStore.getValueWithKeySync('voice-grant', deviceOnly)}`);
  lines.push(`deleted ${ExpoSecureStore.getValueWithKeySync('relay-token', {})}`);
  await settled(() => ExpoSecureStore.setValueWithKeyAsync('v', 'k', { requireAuthentication: true }));
  await settled(() => ExpoSecureStore.setValueWithKeyAsync('v', 'k', { requireAuthentication: false }));
  await settled(() => ExpoSecureStore.getValueWithKeyAsync());
});
)JAVASCRIPT";

constexpr char kReadProbe[] = R"JAVASCRIPT(
run(async () => {
  lines.push(await ExpoSecureStore.getValueWithKeyAsync('dpop-proof-key'));
  lines.push(await ExpoSecureStore.getValueWithKeyAsync('dpop-proof-key', { keychainService: 'other' }));
  lines.push(ExpoSecureStore.getValueWithKeySync('voice-grant', null));
});
)JAVASCRIPT";

constexpr char kUnconfiguredProbe[] = R"JAVASCRIPT(
run(async () => {
  await settled(() => ExpoSecureStore.getValueWithKeyAsync('dpop-proof-key', {}));
  await settled(() => ExpoSecureStore.deleteValueWithKeyAsync('dpop-proof-key', {}));
  await settled(() => ExpoSecureStore.getValueWithKeySync('dpop-proof-key', {}));
});
)JAVASCRIPT";

void loadProbe(ReactHost& reactHost, const char* probe, const char* name) {
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kUpstreamCalls), "SecureStoreCalls.js");
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(probe), name);
}

class ExpoSecureStoreModuleTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ =
            std::filesystem::temp_directory_path() /
            ("rnl-secure-store-module-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        std::filesystem::remove_all(root_);
        std::filesystem::create_directories(root_);
        std::ofstream(root_ / "machine-id") << "0123456789abcdef0123456789abcdef\n";
    }

    void TearDown() override { std::filesystem::remove_all(root_); }

    void configure(ReactHost& reactHost) const {
        reactHost.secureStore().configure(
            root_ / "org.example.app",
            [](const std::string&, const std::stop_token&) { return std::unique_ptr<SecretBackend>(); },
            root_ / "machine-id");
    }

    std::filesystem::path root_;
};

/**
 * #270: `ExpoSecureStore` driven as upstream expo-secure-store's JavaScript drives it, with no keyring on the session:
 * iOS's accessibility constants, no biometrics, a missing key, asynchronous and synchronous sets and reads, a
 * separate `keychainService`, a delete, and rejections for `requireAuthentication` and a missing key. A second host
 * on the same application directory, which is what a reload is, reads the values back from the encrypted files.
 */
TEST_F(ExpoSecureStoreModuleTest, UpstreamsCallsStoreReadAndDeleteAndTheNextInstanceReadsThemBack) {
    {
        ReactHost reactHost;

        configure(reactHost);
        loadProbe(reactHost, kWriteProbe, "SecureStoreWrite.js");

        EXPECT_EQ(awaitGlobalString(reactHost, "secureStoreTrace"),
                  R"({"AFTER_FIRST_UNLOCK":0,"AFTER_FIRST_UNLOCK_THIS_DEVICE_ONLY":1,"ALWAYS":2,)"
                  R"("WHEN_PASSCODE_SET_THIS_DEVICE_ONLY":3,"ALWAYS_THIS_DEVICE_ONLY":4,"WHEN_UNLOCKED":5,)"
                  R"("WHEN_UNLOCKED_THIS_DEVICE_ONLY":6} | biometrics false | missing null | set true | set true | )"
                  R"(read {"kty":"EC"} | sync {"grant":1} | deleted null | )"
                  "Exception in HostFunction: An error occurred when writing to key: 'k' under keychain: 'app'. "
                  "Caused by: requireAuthentication is not supported on Linux | resolved | "
                  "Exception in HostFunction: ExpoSecureStore: argument 0 is missing");
        EXPECT_FALSE(reactHost.hasReportedFatalError());
    }

    ReactHost reloaded;

    configure(reloaded);
    loadProbe(reloaded, kReadProbe, "SecureStoreRead.js");

    EXPECT_EQ(awaitGlobalString(reloaded, "secureStoreTrace"), R"({"kty":"EC"} | other service | {"grant":1})");
    EXPECT_FALSE(reloaded.hasReportedFatalError());
}

/** A host with no application directory rejects every call with upstream's Android messages, synchronous ones too. */
TEST_F(ExpoSecureStoreModuleTest, AHostWithNoApplicationDirectoryRejectsEveryCall) {
    ReactHost reactHost;

    loadProbe(reactHost, kUnconfiguredProbe, "SecureStoreUnconfigured.js");

    EXPECT_EQ(awaitGlobalString(reactHost, "secureStoreTrace"),
              "Could not decrypt the value for key 'dpop-proof-key' under keychain 'app'. Caused by: this host has no "
              "application directory | "
              "Could not delete the value for key 'dpop-proof-key' under keychain 'app'. Caused by: this host has no "
              "application directory | "
              "Exception in HostFunction: Could not decrypt the value for key 'dpop-proof-key' under keychain 'app'. "
              "Caused by: this host has no application directory");
    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
