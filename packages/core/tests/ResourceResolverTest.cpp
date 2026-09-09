#include "ResourceResolver.h"

#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <optional>
#include <string>

namespace {

using react_native_linux::ResourceNotFoundError;
using react_native_linux::ResourceResolver;

namespace fs = std::filesystem;

const std::string kResourceRelativePath = "fonts/NotoSans-Regular.ttf";

fs::path writeFile(const fs::path& path, const std::string& contents) {
    fs::create_directories(path.parent_path());

    std::ofstream file(path);

    file << contents;

    return path;
}

struct Tree {
    fs::path root;

    fs::path file(const std::string& relativePath) const { return writeFile(root / relativePath, "x"); }
};

class ResourceResolverTest : public ::testing::Test {
protected:
    void SetUp() override {
        root_ = fs::temp_directory_path() / ("rnl-resolver-" + std::to_string(counter()));

        fs::remove_all(root_);
        fs::create_directories(root_);
    }

    void TearDown() override { fs::remove_all(root_); }

    static int counter() {
        static int value = 0;

        return value += 1;
    }

    fs::path root_;
};

TEST_F(ResourceResolverTest, TheBuildTreeSourceResolvesWhenTheMarkerIsPresent) {
    Tree buildTree{root_ / "build-tree"};
    buildTree.file(kResourceRelativePath);

    const ResourceResolver resolver({.buildTreeRoot = buildTree.root});

    EXPECT_EQ(resolver.resolve(kResourceRelativePath), buildTree.root / kResourceRelativePath);
    EXPECT_EQ(resolver.searchedDirectories(), std::vector<fs::path>{buildTree.root});
}

TEST_F(ResourceResolverTest, TheInstalledPrefixWinsOverTheBuildTree) {
    Tree installed{root_ / "installed" / "share" / "react-native-linux"};
    Tree buildTree{root_ / "build-tree"};
    installed.file(kResourceRelativePath);
    buildTree.file(kResourceRelativePath);

    const ResourceResolver resolver({.installedResourceRoot = installed.root, .buildTreeRoot = buildTree.root});

    EXPECT_EQ(resolver.resolve(kResourceRelativePath), installed.root / kResourceRelativePath);
    // The search stops at the first hit, so only the winning directory is recorded.
    EXPECT_EQ(resolver.searchedDirectories(), std::vector<fs::path>{installed.root});
}

TEST_F(ResourceResolverTest, ThePortableRootWinsOverTheInstalledPrefix) {
    Tree portable{root_ / "portable"};
    Tree installed{root_ / "installed" / "share" / "react-native-linux"};
    portable.file(kResourceRelativePath);
    installed.file(kResourceRelativePath);

    const ResourceResolver resolver({.portableRoot = portable.root, .installedResourceRoot = installed.root});

    EXPECT_EQ(resolver.resolve(kResourceRelativePath), portable.root / kResourceRelativePath);
}

TEST_F(ResourceResolverTest, TheOverrideIsTheOnlySourceSearchedAndDoesNotFallThrough) {
    Tree installed{root_ / "installed" / "share" / "react-native-linux"};
    Tree buildTree{root_ / "build-tree"};
    installed.file(kResourceRelativePath);
    buildTree.file(kResourceRelativePath);

    const fs::path overrideRoot = root_ / "override";
    fs::create_directories(overrideRoot);

    const ResourceResolver resolver(
        {.overrideRoot = overrideRoot, .installedResourceRoot = installed.root, .buildTreeRoot = buildTree.root});

    EXPECT_THROW(
        {
            try {
                (void)resolver.resolve(kResourceRelativePath);
            } catch (const ResourceNotFoundError& error) {
                EXPECT_NE(error.what(), nullptr);
                EXPECT_EQ(error.what(), std::string("the resource \"" + kResourceRelativePath +
                                                    "\" was not found in any of 1 searched directories: " +
                                                    overrideRoot.string() + ";"));
                throw;
            }
        },
        ResourceNotFoundError);
}

TEST_F(ResourceResolverTest, AnOverrideThatContainsTheResourceBeatsEveryOtherSource) {
    Tree installed{root_ / "installed" / "share" / "react-native-linux"};
    Tree buildTree{root_ / "build-tree"};
    installed.file(kResourceRelativePath);
    buildTree.file(kResourceRelativePath);

    const Tree overrideTree{root_ / "override"};
    overrideTree.file(kResourceRelativePath);

    const ResourceResolver resolver(
        {.overrideRoot = overrideTree.root, .installedResourceRoot = installed.root, .buildTreeRoot = buildTree.root});

    EXPECT_EQ(resolver.resolve(kResourceRelativePath), overrideTree.root / kResourceRelativePath);
}

TEST_F(ResourceResolverTest, AMissingEverywhereResourceNamesTheResourceAndEverySearchedDirectory) {
    const Tree installed{root_ / "installed" / "share" / "react-native-linux"};
    const Tree buildTree{root_ / "build-tree"};
    fs::create_directories(installed.root);
    fs::create_directories(buildTree.root);

    const ResourceResolver resolver({.installedResourceRoot = installed.root, .buildTreeRoot = buildTree.root});

    EXPECT_THROW(
        {
            try {
                (void)resolver.resolve(kResourceRelativePath);
            } catch (const ResourceNotFoundError& error) {
                EXPECT_EQ(error.what(), std::string("the resource \"" + kResourceRelativePath +
                                                    "\" was not found in any of 2 searched " + "directories: " +
                                                    installed.root.string() + "; " + buildTree.root.string() + ";"));
                throw;
            }
        },
        ResourceNotFoundError);
}

TEST_F(ResourceResolverTest, AnExecutableBehindOneSymlinkResolvesToItsTarget) {
    const fs::path realBinary = writeFile(root_ / "opt" / "rnl" / "bin" / "rnl_window", "binary");
    const fs::path symlink = root_ / "usr" / "local" / "bin" / "rnl_window";

    fs::create_directories(symlink.parent_path());
    fs::create_symlink(realBinary, symlink);

    EXPECT_EQ(ResourceResolver::resolveExecutablePath(symlink), realBinary);
}

TEST_F(ResourceResolverTest, AnExecutableBehindNestedSymlinksResolvesToItsTarget) {
    const fs::path realBinary = writeFile(root_ / "opt" / "rnl" / "bin" / "rnl_window", "binary");
    const fs::path innerSymlink = root_ / "opt" / "bin" / "rnl_window";
    const fs::path outerSymlink = root_ / "usr" / "bin" / "rnl_window";

    fs::create_directories(innerSymlink.parent_path());
    fs::create_directories(outerSymlink.parent_path());
    fs::create_symlink(realBinary, innerSymlink);
    fs::create_symlink(innerSymlink, outerSymlink);

    EXPECT_EQ(ResourceResolver::resolveExecutablePath(outerSymlink), realBinary);
}

TEST_F(ResourceResolverTest, APortableRootIsDetectedFromTheMarkerBesideTheExecutable) {
    const fs::path binary = writeFile(root_ / "bundle" / "bin" / "rnl_window", "binary");
    writeFile(root_ / "bundle" / "bin" / "portable.marker", "");

    EXPECT_EQ(ResourceResolver::detectPortableRoot(binary), std::optional<fs::path>(root_ / "bundle" / "bin"));
}

TEST_F(ResourceResolverTest, NoPortableRootWithoutTheMarker) {
    const fs::path binary = writeFile(root_ / "bundle" / "bin" / "rnl_window", "binary");

    EXPECT_EQ(ResourceResolver::detectPortableRoot(binary), std::nullopt);
}

TEST_F(ResourceResolverTest, TheWritableDirectoryHonoursAnAbsoluteOverride) {
    const fs::path home = root_ / "home";

    EXPECT_EQ(ResourceResolver::writableDirectory("/custom/data", true, home, ".local/share"),
              fs::path("/custom/data"));
}

TEST_F(ResourceResolverTest, TheWritableDirectoryFallsBackToTheDefaultWhenUnset) {
    const fs::path home = root_ / "home";

    EXPECT_EQ(ResourceResolver::writableDirectory("", false, home, ".local/share"), home / ".local/share");
}

TEST_F(ResourceResolverTest, TheWritableDirectoryFallsBackToTheDefaultWhenEmpty) {
    const fs::path home = root_ / "home";

    EXPECT_EQ(ResourceResolver::writableDirectory("", true, home, ".cache"), home / ".cache");
}

TEST_F(ResourceResolverTest, TheWritableDirectoryIgnoresARelativeOverrideAsTheSpecificationRequires) {
    const fs::path home = root_ / "home";

    EXPECT_EQ(ResourceResolver::writableDirectory("relative/cache", true, home, ".cache"), home / ".cache");
}

TEST_F(ResourceResolverTest, TheRuntimeSourcesWithoutABakedBuildTreeHaveNoBuildTreeRoot) {
    unsetenv("RNL_RESOURCE_ROOT");

    const ResourceResolver::Sources sources = ResourceResolver::runtimeSources(std::nullopt);

    EXPECT_EQ(sources.overrideRoot, std::nullopt);
    EXPECT_EQ(sources.buildTreeRoot, std::nullopt);
}

TEST_F(ResourceResolverTest, TheRuntimeFontDirectoryPrefersTheOverride) {
    const Tree overrideTree{root_ / "override"};
    overrideTree.file("fonts/NotoSans-Regular.ttf");

    setenv("RNL_RESOURCE_ROOT", overrideTree.root.string().c_str(), 1);
    const std::filesystem::path resolved = ResourceResolver::runtimeFontDirectory(root_ / "baked" / "fonts");
    unsetenv("RNL_RESOURCE_ROOT");

    EXPECT_EQ(resolved, overrideTree.root / "fonts");
}

TEST_F(ResourceResolverTest, TheDevelopmentFontDirectoryRequiresTheMarker) {
    const fs::path baked = root_ / "baked" / "fonts";
    fs::create_directories(baked);

    EXPECT_EQ(ResourceResolver::developmentFontDirectory(baked), std::nullopt);

    writeFile(baked / ".." / "CMakeLists.txt", "");

    EXPECT_EQ(ResourceResolver::developmentFontDirectory(baked), std::optional<fs::path>(baked));
}

TEST_F(ResourceResolverTest, TheRuntimeFontDirectoryResolvesTheRepositoryFontsWhenRunFromTheBuildTree) {
    unsetenv("RNL_RESOURCE_ROOT");

    const std::filesystem::path baked = fs::path(__FILE__).parent_path() / ".." / "fonts";

    EXPECT_TRUE(fs::exists(baked));
    EXPECT_EQ(ResourceResolver::developmentFontDirectory(baked), std::optional<fs::path>(fs::weakly_canonical(baked)));
    EXPECT_EQ(ResourceResolver::runtimeFontDirectory(baked), fs::weakly_canonical(baked));
}

TEST_F(ResourceResolverTest, TheWritableDirectoryReturnsANonExistentOverrideRatherThanCreatingIt) {
    const fs::path home = root_ / "home";
    const fs::path notCreated = root_ / "never" / "created";

    EXPECT_EQ(ResourceResolver::writableDirectory(notCreated.string(), true, home, ".cache"), notCreated);
    EXPECT_FALSE(fs::exists(notCreated));
}

} // namespace
