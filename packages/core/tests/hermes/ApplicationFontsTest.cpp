#include "ReactHost.h"
#include "TextGeometry.h"

#include <algorithm>
#include <cxxreact/JSBigString.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

namespace react_native_linux {
namespace {

bool registers(const std::string& family) {
    const std::vector<std::string> families = applicationFontFamilies();

    return std::ranges::find(families, family) != families.end();
}

/**
 * Issue #70, item 1, per the owner's decision: an application's fonts live in `assets/fonts` beside its bundle,
 * and loading the bundle makes them resolvable by family name ahead of the vendored faces and fontconfig. A source
 * that is a URL, or a bundle with no such directory, registers nothing.
 */
TEST(ApplicationFontsTest, ABundleRegistersTheFontsBesideItAndAUrlRegistersNone) {
    const std::filesystem::path application =
        std::filesystem::temp_directory_path() / ("rnl-application-fonts-" + std::to_string(::getpid()));

    std::filesystem::remove_all(application);
    std::filesystem::create_directories(application / "assets" / "fonts");
    std::filesystem::copy_file(std::filesystem::path(RNL_VENDORED_FONT_DIR) / "NotoSansHebrew-Regular.ttf",
                               application / "assets" / "fonts" / "Brand.ttf");

    ReactHost reactHost;

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>("void 0;"),
                         "http://127.0.0.1:8081/index.bundle?platform=linux");
    const bool registeredFromUrl = registers("Noto Sans Hebrew");

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>("void 0;"),
                         (application / "index.linux.bundle").string());
    const bool registeredFromBundle = registers("Noto Sans Hebrew");

    reactHost.drainJavaScriptThread();
    std::filesystem::remove_all(application);

    EXPECT_FALSE(registeredFromUrl);
    EXPECT_TRUE(registeredFromBundle);
}

} // namespace
} // namespace react_native_linux
