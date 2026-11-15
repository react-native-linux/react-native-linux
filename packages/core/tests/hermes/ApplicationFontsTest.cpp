#include "AutomationProtocol.h"
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

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>

namespace react_native_linux {
namespace {

bool registers(const std::string& family) {
    const std::vector<std::string> families = applicationFontFamilies();

    return std::ranges::find(families, family) != families.end();
}

std::filesystem::path makeApplicationWithBrandFont() {
    const std::filesystem::path application =
        std::filesystem::temp_directory_path() / ("rnl-application-fonts-" + std::to_string(::getpid()));

    std::filesystem::remove_all(application);
    std::filesystem::create_directories(application / "assets" / "fonts");
    std::filesystem::copy_file(std::filesystem::path(RNL_VENDORED_FONT_DIR) / "NotoSansHebrew-Regular.ttf",
                               application / "assets" / "fonts" / "Brand.ttf");

    return application;
}

void measureIn(const std::string& fontFamily) {
    facebook::react::AttributedString attributedString;
    facebook::react::AttributedString::Fragment fragment;

    fragment.string = "\u05E9\u05DC\u05D5\u05DD";
    fragment.textAttributes = facebook::react::TextAttributes::defaultTextAttributes();
    fragment.textAttributes.fontSize = 16;
    fragment.textAttributes.fontFamily = fontFamily;
    attributedString.appendFragment(std::move(fragment));
    measureParagraphMetrics(attributedString, facebook::react::ParagraphAttributes{}, 200.0F);
}

bool reportedUnregistered(const std::string& fontFamily) {
    const std::vector<AutomationError> errors = automationErrorLog().list();

    return std::ranges::any_of(errors, [&](const AutomationError& error) {
        return error.source == "text" &&
               error.message.starts_with("fontFamily \"" + fontFamily + "\" is not registered");
    });
}

/**
 * Issue #70, item 1, per the owner's decision: an application's fonts live in `assets/fonts` beside its bundle,
 * and loading the bundle makes them resolvable by family name ahead of the vendored faces and fontconfig. A source
 * that is a URL, or a bundle with no such directory, registers nothing and clears what an earlier source registered.
 */
TEST(ApplicationFontsTest, ABundleRegistersTheFontsBesideItAndAUrlRegistersNone) {
    const std::filesystem::path application = makeApplicationWithBrandFont();
    ReactHost reactHost;

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>("void 0;"),
                         "http://127.0.0.1:8081/index.bundle?platform=linux");
    const bool registeredFromUrl = registers("Noto Sans Hebrew");

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>("void 0;"),
                         (application / "index.linux.bundle").string());
    const bool registeredFromBundle = registers("Noto Sans Hebrew");

    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>("void 0;"),
                         "http://127.0.0.1:8081/index.bundle?platform=linux");
    const bool keptAfterAUrl = registers("Noto Sans Hebrew");

    reactHost.drainJavaScriptThread();
    std::filesystem::remove_all(application);

    EXPECT_FALSE(registeredFromUrl);
    EXPECT_TRUE(registeredFromBundle);
    EXPECT_FALSE(keptAfterAUrl);
}

/**
 * Expo's `expo-font` config plugin embeds an app's fonts and the app names each face by its PostScript name, the
 * way iOS resolves it: T3 Code asks for `DMSans-Medium`, a face of the family `DM Sans`. That name draws the
 * application's face without the #70 diagnostic, and a face of the same family that the directory does not ship still
 * reports.
 */
TEST(ApplicationFontsTest, AFaceResolvesByItsPostScriptNameAndAFaceTheDirectoryLacksStillReports) {
    const std::filesystem::path application = makeApplicationWithBrandFont();

    registerApplicationFonts((application / "assets" / "fonts").string());
    measureIn("NotoSansHebrew-Regular");
    measureIn("NotoSansHebrew-Thin");
    const bool registeredByPostScriptName = registers("NotoSansHebrew-Regular");

    registerApplicationFonts(std::string());
    std::filesystem::remove_all(application);

    EXPECT_TRUE(registeredByPostScriptName);
    EXPECT_FALSE(reportedUnregistered("NotoSansHebrew-Regular"));
    EXPECT_TRUE(reportedUnregistered("NotoSansHebrew-Thin"));
}

} // namespace
} // namespace react_native_linux
