#include "ExpoFileSystem.h"

#include <filesystem>
#include <gtest/gtest.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

using react_native_linux::FileSystemEntryKind;

class ExpoFileSystemTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ =
            fs::temp_directory_path() /
            ("rnl-expo-file-system-" + std::string(::testing::UnitTest::GetInstance()->current_test_info()->name()));
        fs::remove_all(root_);
        fs::create_directories(root_);
    }

    void TearDown() override { fs::remove_all(root_); }

    fs::path root_;
};

TEST(ExpoFileSystemPathsTest, TheDocumentDirectoryIsInsideTheApplicationDirectoryAndTheCacheIsUnderTheCacheHome) {
    const auto set = react_native_linux::fileSystemDirectories("/data/org.example.app", "/cache", "/home/user");
    const auto unset = react_native_linux::fileSystemDirectories("/data/org.example.app", nullptr, "/home/user");
    const auto relative = react_native_linux::fileSystemDirectories("/data/org.example.app", "cache", "/home/user");

    EXPECT_EQ(set.document, fs::path("/data/org.example.app/Documents"));
    EXPECT_EQ(set.cache, fs::path("/cache/org.example.app"));
    EXPECT_EQ(unset.cache, fs::path("/home/user/.cache/org.example.app"));
    EXPECT_EQ(relative.cache, fs::path("/home/user/.cache/org.example.app"));
}

TEST(ExpoFileSystemPathsTest, FileUrisAndAbsolutePathsNameTheDecodedNormalizedPath) {
    using react_native_linux::pathFromFileUri;

    EXPECT_EQ(pathFromFileUri("file:///data/Documents/"), fs::path("/data/Documents"));
    EXPECT_EQ(pathFromFileUri("file:/data/./a/../b.txt"), fs::path("/data/b.txt"));
    EXPECT_EQ(pathFromFileUri("/data/a%20b"), fs::path("/data/a b"));
    EXPECT_EQ(pathFromFileUri("file:///"), fs::path("/"));
    EXPECT_EQ(pathFromFileUri("/%e2%82%ac%C3%A9"), fs::path("/€é"));
    EXPECT_EQ(pathFromFileUri("/%!1%:1%G1%z1%4z%4"), fs::path("/%!1%:1%G1%z1%4z%4"));
    EXPECT_THROW(static_cast<void>(pathFromFileUri("relative/path")), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(pathFromFileUri("file://host/path")), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(pathFromFileUri("https://example.com/a")), std::invalid_argument);
}

TEST(ExpoFileSystemPathsTest, AFileUriIsPercentEncodedAndADirectoryUriEndsInASlash) {
    using react_native_linux::fileUriFromPath;

    EXPECT_EQ(fileUriFromPath("/data/a b/é-._~!$&'()*+,;=:@", FileSystemEntryKind::File),
              "file:///data/a%20b/%C3%A9-._~!$&'()*+,;=:@");
    EXPECT_EQ(fileUriFromPath("/data/Documents", FileSystemEntryKind::Directory), "file:///data/Documents/");
    EXPECT_EQ(fileUriFromPath("/", FileSystemEntryKind::Directory), "file:///");
}

TEST(ExpoFileSystemBase64Test, EncodesAndDecodesEveryPaddingLength) {
    using react_native_linux::decodeBase64;
    using react_native_linux::encodeBase64;

    EXPECT_EQ(encodeBase64(""), "");
    EXPECT_EQ(encodeBase64("f"), "Zg==");
    EXPECT_EQ(encodeBase64("fo"), "Zm8=");
    EXPECT_EQ(encodeBase64("foo"), "Zm9v");
    EXPECT_EQ(encodeBase64(std::string("\xFF\x00\xFE", 3)), "/wD+");
    EXPECT_EQ(decodeBase64("Zg=="), "f");
    EXPECT_EQ(decodeBase64("Zm8="), "fo");
    EXPECT_EQ(decodeBase64("/wD+"), std::string("\xFF\x00\xFE", 3));
    EXPECT_THROW(static_cast<void>(decodeBase64("Zm9")), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(decodeBase64("Z===")), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(decodeBase64("Zm9!")), std::invalid_argument);
}

TEST_F(ExpoFileSystemTest, AFileIsCreatedWrittenAppendedReadAndDeleted) {
    const fs::path file = root_ / "nested" / "note.txt";

    EXPECT_FALSE(react_native_linux::entryExists(file, FileSystemEntryKind::File));
    EXPECT_EQ(react_native_linux::fileSize(file), std::nullopt);
    EXPECT_THROW(react_native_linux::createFile(file, false, false), std::runtime_error);

    react_native_linux::createFile(file, true, false);
    EXPECT_TRUE(react_native_linux::entryExists(file, FileSystemEntryKind::File));
    EXPECT_THROW(react_native_linux::createFile(file, true, false), std::runtime_error);

    react_native_linux::writeFileBytes(file, "hello", false);
    react_native_linux::writeFileBytes(file, " world", true);
    EXPECT_EQ(react_native_linux::readFileBytes(file), "hello world");
    EXPECT_EQ(react_native_linux::fileSize(file), 11U);

    react_native_linux::createFile(file, false, true);
    EXPECT_EQ(react_native_linux::readFileBytes(file), "");

    react_native_linux::removeEntry(file);
    EXPECT_THROW(react_native_linux::removeEntry(file), std::runtime_error);
    EXPECT_THROW(static_cast<void>(react_native_linux::readFileBytes(file)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(react_native_linux::readFileBytes(root_)), std::runtime_error);
    EXPECT_THROW(react_native_linux::writeFileBytes(root_ / "missing" / "note.txt", "x", false), std::runtime_error);
}

TEST_F(ExpoFileSystemTest, ADirectoryIsCreatedIdempotentlyOverwrittenListedAndDeleted) {
    const fs::path directory = root_ / "a" / "b";

    EXPECT_THROW(react_native_linux::createDirectory(directory, false, false, false), fs::filesystem_error);
    react_native_linux::createDirectory(directory, true, false, true);
    EXPECT_TRUE(react_native_linux::entryExists(directory, FileSystemEntryKind::Directory));
    react_native_linux::createDirectory(directory, false, false, true);
    EXPECT_THROW(react_native_linux::createDirectory(directory, false, false, false), std::runtime_error);

    react_native_linux::writeFileBytes(directory / "z.txt", "z", false);
    react_native_linux::createDirectory(directory / "sub", false, false, false);
    const std::vector<react_native_linux::FileSystemRecord> records = react_native_linux::listDirectory(directory);

    ASSERT_EQ(records.size(), 2U);
    EXPECT_TRUE(records[0].isDirectory);
    EXPECT_EQ(records[0].path, directory / "sub");
    EXPECT_FALSE(records[1].isDirectory);
    EXPECT_EQ(records[1].path, directory / "z.txt");

    react_native_linux::createDirectory(directory, false, true, false);
    EXPECT_TRUE(react_native_linux::listDirectory(directory).empty());

    react_native_linux::removeEntry(root_ / "a");
    EXPECT_FALSE(react_native_linux::entryExists(directory, FileSystemEntryKind::Directory));
}

TEST_F(ExpoFileSystemTest, APathThatExistsAsTheOtherKindFailsValidation) {
    react_native_linux::writeFileBytes(root_ / "file", "", false);

    EXPECT_NO_THROW(react_native_linux::validateEntryPath(root_ / "file", FileSystemEntryKind::File));
    EXPECT_NO_THROW(react_native_linux::validateEntryPath(root_, FileSystemEntryKind::Directory));
    EXPECT_THROW(react_native_linux::validateEntryPath(root_, FileSystemEntryKind::File), std::invalid_argument);
    EXPECT_THROW(react_native_linux::validateEntryPath(root_ / "file", FileSystemEntryKind::Directory),
                 std::invalid_argument);
}

TEST_F(ExpoFileSystemTest, CopyAndMoveLandInADirectoryOrOnAFileAndReplaceOnlyWithOverwrite) {
    using react_native_linux::relocateEntry;
    const fs::path source = root_ / "source.txt";
    const fs::path folder = root_ / "folder";

    react_native_linux::writeFileBytes(source, "data", false);
    fs::create_directories(folder);

    EXPECT_EQ(relocateEntry(source, FileSystemEntryKind::File, folder, FileSystemEntryKind::Directory, false, false),
              folder / "source.txt");
    EXPECT_THROW(relocateEntry(source, FileSystemEntryKind::File, folder, FileSystemEntryKind::Directory, false, false),
                 std::runtime_error);
    EXPECT_EQ(
        relocateEntry(source, FileSystemEntryKind::File, root_ / "moved.txt", FileSystemEntryKind::File, true, true),
        root_ / "moved.txt");
    EXPECT_FALSE(fs::exists(source));
    EXPECT_THROW(relocateEntry(source, FileSystemEntryKind::File, folder, FileSystemEntryKind::Directory, true, true),
                 std::runtime_error);

    react_native_linux::writeFileBytes(root_ / "other.txt", "other", false);
    relocateEntry(root_ / "other.txt", FileSystemEntryKind::File, root_ / "moved.txt", FileSystemEntryKind::File, true,
                  true);
    EXPECT_EQ(react_native_linux::readFileBytes(root_ / "moved.txt"), "other");

    EXPECT_THROW(relocateEntry(folder, FileSystemEntryKind::Directory, root_ / "moved.txt", FileSystemEntryKind::File,
                               true, false),
                 std::invalid_argument);
    fs::create_directories(root_ / "copy");
    EXPECT_EQ(relocateEntry(folder, FileSystemEntryKind::Directory, root_ / "copy", FileSystemEntryKind::Directory,
                            false, false),
              root_ / "copy" / "folder");
    EXPECT_EQ(react_native_linux::readFileBytes(root_ / "copy" / "folder" / "source.txt"), "data");
}

} // namespace
