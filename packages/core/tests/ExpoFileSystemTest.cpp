#include "ExpoFileSystem.h"

#include <cmath>
#include <filesystem>
#include <gtest/gtest.h>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
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
    const auto homeless = react_native_linux::fileSystemDirectories("/data/org.example.app", nullptr, "");

    EXPECT_EQ(set.document, fs::path("/data/org.example.app/Documents"));
    EXPECT_EQ(set.cache, fs::path("/cache/org.example.app"));
    EXPECT_EQ(unset.cache, fs::path("/home/user/.cache/org.example.app"));
    EXPECT_EQ(relative.cache, fs::path("/home/user/.cache/org.example.app"));
    EXPECT_EQ(homeless.document, fs::path("/data/org.example.app/Documents"));
    EXPECT_EQ(homeless.cache, std::nullopt);
}

TEST(ExpoFileSystemByteViewTest, AViewsRangeMustBeWholeNonnegativeAndInsideItsBuffer) {
    using react_native_linux::byteViewRange;
    constexpr double kInfinity = std::numeric_limits<double>::infinity();

    EXPECT_EQ(byteViewRange(2, 3, 8), std::make_pair(size_t{2}, size_t{3}));
    EXPECT_EQ(byteViewRange(8, 0, 8), std::make_pair(size_t{8}, size_t{0}));
    for (const auto& [offset, length] : std::vector<std::pair<double, double>>{
             {-1, 1}, {1.5, 1}, {std::nan(""), 1}, {kInfinity, 0}, {9, 0}, {0, -1}, {0, 0.5}, {0, kInfinity}, {2, 7}}) {
        EXPECT_THROW(static_cast<void>(byteViewRange(offset, length, 8)), std::invalid_argument)
            << offset << ", " << length;
    }
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
    EXPECT_EQ(encodeBase64("hi"), "aGk=");
    EXPECT_EQ(encodeBase64("foo"), "Zm9v");
    EXPECT_EQ(encodeBase64(std::string("\xFF\x00\xFE", 3)), "/wD+");
    EXPECT_EQ(decodeBase64("Zg=="), "f");
    EXPECT_EQ(decodeBase64("aGk="), "hi");
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

    react_native_linux::removeEntry(file, FileSystemEntryKind::File);
    EXPECT_THROW(react_native_linux::removeEntry(file, FileSystemEntryKind::File), std::runtime_error);
    EXPECT_THROW(static_cast<void>(react_native_linux::readFileBytes(file)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(react_native_linux::readFileBytes(root_)), std::runtime_error);
    EXPECT_THROW(react_native_linux::writeFileBytes(root_ / "missing" / "note.txt", "x", false), std::runtime_error);
}

TEST_F(ExpoFileSystemTest, AWriteThatDoesNotReachTheFileFails) {
    EXPECT_THROW(react_native_linux::writeFileBytes("/dev/full", "x", false), std::runtime_error);
}

TEST_F(ExpoFileSystemTest, AnOperationRechecksWhatIsAtItsPathNow) {
    const fs::path path = root_ / "entry";

    fs::create_directories(path / "kept");
    EXPECT_THROW(react_native_linux::removeEntry(path, FileSystemEntryKind::File), std::invalid_argument);
    EXPECT_THROW(react_native_linux::createFile(path, false, true), std::invalid_argument);
    EXPECT_TRUE(fs::is_directory(path / "kept"));

    fs::remove_all(path);
    react_native_linux::writeFileBytes(path, "kept", false);
    EXPECT_THROW(react_native_linux::removeEntry(path, FileSystemEntryKind::Directory), std::invalid_argument);
    EXPECT_THROW(react_native_linux::createDirectory(path, false, true, false), std::invalid_argument);
    react_native_linux::writeFileBytes(root_ / "source.txt", "source", false);
    EXPECT_THROW(react_native_linux::relocateEntry(root_ / "source.txt", FileSystemEntryKind::File, path,
                                                   FileSystemEntryKind::Directory, true, true),
                 std::invalid_argument);
    EXPECT_EQ(react_native_linux::readFileBytes(path), "kept");
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

    react_native_linux::removeEntry(root_ / "a", FileSystemEntryKind::Directory);
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

    react_native_linux::writeFileBytes(root_ / "fresh.txt", "fresh", false);
    relocateEntry(root_ / "fresh.txt", FileSystemEntryKind::File, root_ / "moved.txt", FileSystemEntryKind::File, true,
                  false);
    EXPECT_EQ(react_native_linux::readFileBytes(root_ / "moved.txt"), "fresh");
}

TEST_F(ExpoFileSystemTest, ADirectoryBecomesAnAbsentDestinationAndReplacesAnExistingOneOnlyWithOverwrite) {
    using react_native_linux::relocateEntry;
    const fs::path folder = root_ / "folder";

    react_native_linux::createDirectory(folder / "inner", true, false, false);
    react_native_linux::writeFileBytes(folder / "inner" / "a.txt", "a", false);

    EXPECT_EQ(relocateEntry(folder, FileSystemEntryKind::Directory, root_ / "copied", FileSystemEntryKind::Directory,
                            false, false),
              root_ / "copied");
    EXPECT_EQ(react_native_linux::readFileBytes(root_ / "copied" / "inner" / "a.txt"), "a");
    EXPECT_EQ(relocateEntry(root_ / "copied", FileSystemEntryKind::Directory, root_ / "renamed",
                            FileSystemEntryKind::Directory, false, true),
              root_ / "renamed");
    EXPECT_FALSE(fs::exists(root_ / "copied"));
    EXPECT_EQ(react_native_linux::readFileBytes(root_ / "renamed" / "inner" / "a.txt"), "a");

    react_native_linux::writeFileBytes(root_ / "renamed" / "inner" / "stale.txt", "stale", false);
    EXPECT_THROW(relocateEntry(folder / "inner", FileSystemEntryKind::Directory, root_ / "renamed",
                               FileSystemEntryKind::Directory, false, false),
                 std::runtime_error);
    relocateEntry(folder / "inner", FileSystemEntryKind::Directory, root_, FileSystemEntryKind::Directory, false,
                  false);
    react_native_linux::writeFileBytes(folder / "inner" / "a.txt", "replaced", false);
    EXPECT_EQ(relocateEntry(folder / "inner", FileSystemEntryKind::Directory, root_ / "renamed",
                            FileSystemEntryKind::Directory, true, false),
              root_ / "renamed" / "inner");
    EXPECT_EQ(react_native_linux::readFileBytes(root_ / "renamed" / "inner" / "a.txt"), "replaced");
    EXPECT_FALSE(fs::exists(root_ / "renamed" / "inner" / "stale.txt"));
    EXPECT_EQ(relocateEntry(root_ / "inner", FileSystemEntryKind::Directory, root_ / "renamed",
                            FileSystemEntryKind::Directory, true, true),
              root_ / "renamed" / "inner");
    EXPECT_EQ(react_native_linux::readFileBytes(root_ / "renamed" / "inner" / "a.txt"), "a");
}

TEST_F(ExpoFileSystemTest, AFailedOrSelfReplacementLeavesWhatWasThereIntact) {
    using react_native_linux::relocateEntry;
    const fs::path file = root_ / "file.txt";
    const fs::path outer = root_ / "outer";

    react_native_linux::writeFileBytes(file, "file", false);
    fs::create_hard_link(file, root_ / "alias.txt");
    fs::create_directories(outer / "outer");
    fs::create_directories(root_ / "folder" / "file.txt");
    react_native_linux::writeFileBytes(root_ / "folder" / "file.txt" / "kept.txt", "kept", false);
    react_native_linux::writeFileBytes(root_ / "folder" / "outer", "kept", false);
    fs::create_directories(root_ / "nest" / "inner" / "nest");
    react_native_linux::writeFileBytes(root_ / "nest" / "inner" / "nest" / "kept.txt", "kept", false);

    for (const bool move : {false, true}) {
        EXPECT_THROW(relocateEntry(file, FileSystemEntryKind::File, file, FileSystemEntryKind::File, true, move),
                     std::invalid_argument);
        EXPECT_THROW(
            relocateEntry(file, FileSystemEntryKind::File, root_ / "alias.txt", FileSystemEntryKind::File, true, move),
            std::invalid_argument);
        EXPECT_THROW(relocateEntry(outer / "outer", FileSystemEntryKind::Directory, root_,
                                   FileSystemEntryKind::Directory, true, move),
                     std::invalid_argument);
        EXPECT_THROW(relocateEntry(file, FileSystemEntryKind::File, root_ / "folder", FileSystemEntryKind::Directory,
                                   true, move),
                     fs::filesystem_error);
        EXPECT_THROW(relocateEntry(outer, FileSystemEntryKind::Directory, root_ / "folder",
                                   FileSystemEntryKind::Directory, true, move),
                     fs::filesystem_error);
        EXPECT_THROW(relocateEntry(root_ / "nest", FileSystemEntryKind::Directory, root_ / "nest" / "inner",
                                   FileSystemEntryKind::Directory, true, move),
                     std::invalid_argument);
        EXPECT_THROW(relocateEntry(root_ / "nest", FileSystemEntryKind::Directory, root_ / "nest" / "absent",
                                   FileSystemEntryKind::Directory, false, move),
                     std::invalid_argument);
    }

    EXPECT_EQ(react_native_linux::readFileBytes(file), "file");
    EXPECT_TRUE(fs::is_directory(outer / "outer"));
    EXPECT_EQ(react_native_linux::readFileBytes(root_ / "folder" / "file.txt" / "kept.txt"), "kept");
    EXPECT_EQ(react_native_linux::readFileBytes(root_ / "folder" / "outer"), "kept");
    EXPECT_EQ(react_native_linux::readFileBytes(root_ / "nest" / "inner" / "nest" / "kept.txt"), "kept");
    EXPECT_FALSE(fs::exists(root_ / "nest" / "absent"));
}

} // namespace
