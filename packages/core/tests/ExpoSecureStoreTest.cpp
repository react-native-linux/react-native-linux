#include "ExpoSecureStore.h"

#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace {

namespace fs = std::filesystem;

using react_native_linux::EncryptedFileBackend;
using react_native_linux::SecretBackend;
using react_native_linux::SecretLocation;
using react_native_linux::SecureStore;

const SecretLocation kToken{.service = "app", .key = "managed-relay-token"};

std::string contentsOf(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);

    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

void writeContents(const fs::path& path, const std::string& contents) {
    std::ofstream(path, std::ios::binary | std::ios::trunc) << contents;
}

std::string messageOf(const std::function<void()>& work) {
    try {
        work();
    } catch (const std::exception& error) {
        return error.what();
    }

    return "no error";
}

class ExpoSecureStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ =
            fs::temp_directory_path() /
            ("rnl-expo-secure-store-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        fs::create_directories(root_);
        machineIdentifier_ = root_ / "machine-id";
        writeContents(machineIdentifier_, "0123456789abcdef0123456789abcdef\n");
    }

    void TearDown() override { fs::remove_all(root_); }

    [[nodiscard]] std::unique_ptr<EncryptedFileBackend> backend(const std::string& applicationIdentifier) const {
        return std::make_unique<EncryptedFileBackend>(root_ / "SecureStore", machineIdentifier_, applicationIdentifier);
    }

    [[nodiscard]] fs::path onlyFile() const {
        fs::path found;

        for (const fs::directory_entry& entry : fs::directory_iterator(root_ / "SecureStore")) {
            found = entry.path();
        }

        return found;
    }

    fs::path root_;
    fs::path machineIdentifier_;
};

/** A keyring that keeps its values in memory, and counts how often `SecureStore` connected to it. */
class MemoryKeyring final : public SecretBackend {
public:
    [[nodiscard]] std::optional<std::string> read(const SecretLocation& location) override {
        const auto found = values_.find(location.service + '/' + location.key);

        return found == values_.end() ? std::nullopt : std::optional<std::string>(found->second);
    }

    void write(const SecretLocation& location, const std::string& value) override {
        values_[location.service + '/' + location.key] = value;
    }

    void remove(const SecretLocation& location) override { values_.erase(location.service + '/' + location.key); }

private:
    std::map<std::string, std::string> values_;
};

/**
 * #270: the fallback stores, replaces and deletes a value per service and key, in a 0600 file inside a 0700
 * directory whose bytes do not contain the value and change on every write, because each write draws a new nonce.
 */
TEST_F(ExpoSecureStoreTest, TheEncryptedFileStoresReplacesAndDeletesAValueWithoutWritingItInTheClear) {
    const std::unique_ptr<EncryptedFileBackend> store = backend("org.example.app");
    const SecretLocation otherService{.service = "voice", .key = kToken.key};

    EXPECT_EQ(store->read(kToken), std::nullopt);
    store->write(kToken, "first secret");
    const std::string firstBytes = contentsOf(onlyFile());

    store->write(kToken, "first secret");
    EXPECT_NE(contentsOf(onlyFile()), firstBytes);
    EXPECT_EQ(contentsOf(onlyFile()).find("first secret"), std::string::npos);
    EXPECT_EQ(store->read(kToken), "first secret");
    const std::string binary = std::string("ünïcode") + '\0' + "bytes";

    store->write(kToken, binary);
    EXPECT_EQ(store->read(kToken), binary);
    store->write(kToken, "");
    EXPECT_EQ(store->read(kToken), "");
    EXPECT_EQ(store->read(otherService), std::nullopt);
    EXPECT_EQ(fs::status(onlyFile()).permissions(), fs::perms::owner_read | fs::perms::owner_write);
    EXPECT_EQ(fs::status(root_ / "SecureStore").permissions(), fs::perms::owner_all);

    store->remove(kToken);
    store->remove(kToken);
    EXPECT_EQ(store->read(kToken), std::nullopt);
    EXPECT_TRUE(fs::is_empty(root_ / "SecureStore"));
}

/**
 * A second backend for the same machine, user and application reads what the first wrote, as a relaunch does; one for
 * another application derives another key and fails rather than answering garbage, as does a tampered or truncated
 * file, or a file moved to another key's name.
 */
TEST_F(ExpoSecureStoreTest, OnlyTheSameMachineUserAndApplicationOpenAnUntamperedFile) {
    const SecretLocation other{.service = "app", .key = "other"};

    backend("org.example.app")->write(kToken, "secret");
    EXPECT_EQ(backend("org.example.app")->read(kToken), "secret");
    EXPECT_EQ(messageOf([&] { static_cast<void>(backend("org.example.other")->read(kToken)); }),
              "OpenSSL: the value does not authenticate under this machine, user and application");

    const fs::path file = onlyFile();
    std::string bytes = contentsOf(file);

    bytes.back() = static_cast<char>(bytes.back() ^ 1);
    writeContents(file, bytes);
    EXPECT_THROW(static_cast<void>(backend("org.example.app")->read(kToken)), std::runtime_error);

    fs::remove(file);
    backend("org.example.app")->write(other, "other");
    fs::rename(onlyFile(), file);
    EXPECT_THROW(static_cast<void>(backend("org.example.app")->read(kToken)), std::runtime_error);

    writeContents(file, "short");
    EXPECT_EQ(messageOf([&] { static_cast<void>(backend("org.example.app")->read(kToken)); }),
              "the file is too short to be a sealed value");
}

TEST_F(ExpoSecureStoreTest, AMissingOrEmptyMachineIdentifierIsAnError) {
    writeContents(machineIdentifier_, "\n");
    EXPECT_EQ(messageOf([&] { static_cast<void>(backend("org.example.app")); }),
              machineIdentifier_.string() + " is empty");

    fs::remove(machineIdentifier_);
    EXPECT_EQ(messageOf([&] { static_cast<void>(backend("org.example.app")); }),
              "cannot read " + machineIdentifier_.string() + ": No such file or directory");
}

/** A write that cannot create its temporary file fails, and leaves the value it would have replaced intact. */
TEST_F(ExpoSecureStoreTest, AWriteThatCannotCreateItsTemporaryFileKeepsThePreviousValue) {
    const std::unique_ptr<EncryptedFileBackend> store = backend("org.example.app");

    store->write(kToken, "secret");
    const fs::path file = onlyFile();

    fs::create_directory(file.string() + ".tmp");
    EXPECT_EQ(messageOf([&] { store->write(kToken, "replacement"); }),
              "cannot create " + file.string() + ".tmp: Is a directory");
    EXPECT_EQ(store->read(kToken), "secret");
}

/**
 * `SecureStore` asks the keyring connector once, with the application directory's name, and keeps what it answered;
 * every task runs on its worker thread.
 */
TEST_F(ExpoSecureStoreTest, TheStoreUsesTheKeyringWhenTheConnectorReachesOne) {
    SecureStore store;
    int connections = 0;
    std::string connectedApplication;

    store.configure(root_ / "org.example.app", [&](const std::string& applicationIdentifier) {
        ++connections;
        connectedApplication = applicationIdentifier;

        return std::make_unique<MemoryKeyring>();
    });

    std::promise<std::thread::id> worker;

    store.post([&](SecureStore& posted) {
        posted.backend().write(kToken, "secret");
        worker.set_value(std::this_thread::get_id());
    });
    EXPECT_NE(worker.get_future().get(), std::this_thread::get_id());

    std::promise<std::optional<std::string>> read;

    store.post([&](SecureStore& posted) { read.set_value(posted.backend().read(kToken)); });
    EXPECT_EQ(read.get_future().get(), "secret");
    EXPECT_EQ(connections, 1);
    EXPECT_EQ(connectedApplication, "org.example.app");
    EXPECT_FALSE(fs::exists(root_ / "org.example.app" / "SecureStore"));
}

/** With no keyring, the values go to the encrypted file under the application directory, and outlive the store. */
TEST_F(ExpoSecureStoreTest, TheStoreFallsBackToTheEncryptedFileWhenThereIsNoKeyring) {
    const auto noKeyring = [](const std::string&) { return std::unique_ptr<SecretBackend>(); };
    {
        SecureStore store;

        store.configure(root_ / "org.example.app", noKeyring, machineIdentifier_);
        store.post([](SecureStore& posted) { posted.backend().write(kToken, "secret"); });
    }

    EXPECT_EQ(fs::directory_iterator(root_ / "org.example.app" / "SecureStore")->path().parent_path(),
              root_ / "org.example.app" / "SecureStore");

    SecureStore reloaded;
    std::promise<std::optional<std::string>> read;

    reloaded.configure(root_ / "org.example.app", noKeyring, machineIdentifier_);
    reloaded.post([&](SecureStore& posted) { read.set_value(posted.backend().read(kToken)); });
    EXPECT_EQ(read.get_future().get(), "secret");
}

TEST_F(ExpoSecureStoreTest, AStoreWithNoApplicationDirectoryRefusesEveryCall) {
    SecureStore store;

    EXPECT_EQ(messageOf([&] { static_cast<void>(store.backend()); }), "this host has no application directory");
}

} // namespace
