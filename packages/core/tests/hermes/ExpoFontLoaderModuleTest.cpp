#include "OnJavaScriptThread.h"
#include "TextGeometry.h"

#include <chrono>
#include <cxxreact/JSBigString.h>
#include <filesystem>
#include <fstream>
#include <functional>
#include <gtest/gtest.h>
#include <string>
#include <unistd.h>

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>

namespace react_native_linux {
namespace {

const std::string kItalicFont = std::string(RNL_VENDORED_FONT_DIR) + "/NotoSans-Italic.ttf";
const std::string kBoldFont = std::string(RNL_VENDORED_FONT_DIR) + "/NotoSans-Bold.ttf";

/**
 * The JSON of what `expression`, evaluated over `ExpoFontLoader` as `fontLoader`, resolves to, or `threw: <message>`.
 * `settle(promise)` answers `resolved` or `rejected: <message>`. Every probe runs in a fresh `ReactHost`, whose script
 * load clears the application fonts, and clears the ones it registered after `whileLoaded` ran.
 */
std::string fontLoaderTrace(const std::string& expression, const std::function<void()>& whileLoaded = [] {}) {
    constexpr std::chrono::seconds kSettleBudget{5};
    ReactHost reactHost;

    reactHost.loadScript(
        std::make_unique<facebook::react::JSBigStdString>(
            "const fontLoader = globalThis.nativeModuleProxy.ExpoFontLoader;\n"
            "const settle = (promise) => promise.then(() => 'resolved', (error) => `rejected: ${error.message}`);\n"
            "(async () => JSON.stringify(await (" +
            expression +
            ")))().catch((error) => `threw: ${error.message}`).then((trace) => { globalThis.fontTrace = trace; });"),
        "ExpoFontLoaderModuleTest.js");
    EXPECT_TRUE(reactHost.runUntilQuiescent(kSettleBudget));
    whileLoaded();
    registerApplicationFonts(std::string());

    return onJavaScriptThread<std::string>(reactHost, [](facebook::jsi::Runtime& runtime) {
        const facebook::jsi::Value trace = runtime.global().getProperty(runtime, "fontTrace");

        return trace.isString() ? trace.getString(runtime).utf8(runtime) : std::string("the probe never finished");
    });
}

/**
 * #156: upstream's `Font.loadAsync` downloads the font through `expo-asset` and calls `loadAsync(fontFamily,
 * localUri)`, a `file://` URL or a path. `getLoadedFonts`, which upstream's `isLoaded` reads, then lists that
 * `fontFamily` among the text pipeline's application fonts; the `expo-font` e2e draws it.
 */
TEST(ExpoFontLoaderModuleTest, LoadAsyncRegistersAFontFileUnderItsFontFamily) {
    EXPECT_EQ(
        fontLoaderTrace("[fontLoader.getLoadedFonts(), await settle(fontLoader.loadAsync('Probe Brand', 'file://" +
                        kItalicFont + "')), await settle(fontLoader.loadAsync('Probe Path Brand', '" + kItalicFont +
                        "')), fontLoader.getLoadedFonts().sort()]"),
        R"([[],"resolved","resolved",["Probe Brand","Probe Path Brand"]])");
}

/** `loadFontFamilyAsync(fontFamily, faces)` registers every face's file under the one `fontFamily`. */
TEST(ExpoFontLoaderModuleTest, LoadFontFamilyAsyncRegistersEveryFace) {
    EXPECT_EQ(fontLoaderTrace("[await settle(fontLoader.loadFontFamilyAsync('Probe Family', [{ localUri: 'file://" +
                              kItalicFont + "', style: 'italic' }, { localUri: 'file://" + kBoldFont +
                              "', weight: 700 }])), fontLoader.getLoadedFonts()]"),
              R"(["resolved",["Probe Family"]])");
}

std::filesystem::path writeNotAFont() {
    const std::filesystem::path notAFont =
        std::filesystem::temp_directory_path() / ("rnl-expo-font-" + std::to_string(::getpid()) + ".ttf");

    std::ofstream(notAFont) << "not a font";

    return notAFont;
}

float widthIn(const std::string& fontFamily, facebook::react::FontWeight fontWeight,
              facebook::react::FontStyle fontStyle) {
    facebook::react::AttributedString attributedString;
    facebook::react::AttributedString::Fragment fragment;

    fragment.string = "Families load whole";
    fragment.textAttributes = facebook::react::TextAttributes::defaultTextAttributes();
    fragment.textAttributes.fontSize = 16;
    fragment.textAttributes.fontFamily = fontFamily;
    fragment.textAttributes.fontWeight = fontWeight;
    fragment.textAttributes.fontStyle = fontStyle;
    attributedString.appendFragment(std::move(fragment));

    return measureParagraphMetrics(attributedString, facebook::react::ParagraphAttributes{}, 1000.0F).longestLineWidth;
}

/**
 * A family whose second face fails registers none of its faces, so `getLoadedFonts` does not list it and upstream's
 * `isLoaded` does not skip the corrected retry, which then draws both faces: the bold and the italic measure apart.
 */
TEST(ExpoFontLoaderModuleTest, AFamilyWithAFailingFaceRegistersNoneAndACorrectedRetryDrawsEveryFace) {
    const std::filesystem::path notAFont = writeNotAFont();
    float boldWidth = 0.0F;
    float italicWidth = 0.0F;

    const std::string trace = fontLoaderTrace(
        "[await settle(fontLoader.loadFontFamilyAsync('Probe Family', [{ localUri: 'file://" + kItalicFont +
            "' }, { localUri: 'file://" + notAFont.string() +
            "' }])), fontLoader.getLoadedFonts(), await settle(fontLoader.loadFontFamilyAsync('Probe Family', "
            "[{ localUri: 'file://" +
            kItalicFont + "' }, { localUri: 'file://" + kBoldFont + "' }])), fontLoader.getLoadedFonts()]",
        [&] {
            boldWidth = widthIn("Probe Family", facebook::react::FontWeight::Bold, facebook::react::FontStyle::Normal);
            italicWidth =
                widthIn("Probe Family", facebook::react::FontWeight::Regular, facebook::react::FontStyle::Italic);
        });

    std::filesystem::remove(notAFont);

    EXPECT_EQ(trace, R"(["rejected: Could not create font from loaded data for 'Probe Family'",[],"resolved",)"
                     R"(["Probe Family"]])");
    EXPECT_GT(boldWidth, italicWidth);
}

/** Upstream's iOS messages for a missing file, a file that is not a font, no faces, and a face without a file. */
TEST(ExpoFontLoaderModuleTest, RejectsWithUpstreamsMessages) {
    const std::filesystem::path notAFont = writeNotAFont();

    EXPECT_EQ(fontLoaderTrace("Promise.all([settle(fontLoader.loadAsync('Missing', 'file:///nonexistent.ttf')), "
                              "settle(fontLoader.loadAsync('Broken', 'file://" +
                              notAFont.string() +
                              "')), settle(fontLoader.loadFontFamilyAsync('Empty', [])), "
                              "settle(fontLoader.loadFontFamilyAsync('Faceless', [{ weight: 400 }]))])"),
              R"(["rejected: Font file 'file:///nonexistent.ttf' doesn't exist",)"
              R"("rejected: Could not create font from loaded data for 'Broken'",)"
              R"("rejected: Could not load font family 'Empty' because no faces were provided. Pass at least one font )"
              R"(face with a 'localUri' to 'loadFontFamilyAsync'",)"
              R"("rejected: Could not load font family 'Faceless' because one of its faces has no 'localUri'. Make )"
              R"(sure every face passed to 'loadFontFamilyAsync' includes a font file URI"])");

    std::filesystem::remove(notAFont);
}

TEST(ExpoFontLoaderModuleTest, ThrowsWithoutUpstreamsTwoArguments) {
    EXPECT_EQ(fontLoaderTrace("fontLoader.loadAsync('Brand')"),
              "threw: ExpoFontLoader.loadAsync takes a fontFamily and a localUri");
    EXPECT_EQ(fontLoaderTrace("fontLoader.loadFontFamilyAsync('Brand')"),
              "threw: ExpoFontLoader.loadFontFamilyAsync takes a fontFamily and faces");
}

} // namespace
} // namespace react_native_linux
