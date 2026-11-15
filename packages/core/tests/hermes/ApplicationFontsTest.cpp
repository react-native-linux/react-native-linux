#include "AutomationProtocol.h"
#include "ReactHost.h"
#include "TextGeometry.h"
#include "TextPipeline.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cxxreact/JSBigString.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <thread>
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

facebook::react::AttributedString hebrewIn(const std::string& fontFamily) {
    facebook::react::AttributedString attributedString;
    facebook::react::AttributedString::Fragment fragment;

    fragment.string = "\u05E9\u05DC\u05D5\u05DD";
    fragment.textAttributes = facebook::react::TextAttributes::defaultTextAttributes();
    fragment.textAttributes.fontSize = 16;
    fragment.textAttributes.fontFamily = fontFamily;
    attributedString.appendFragment(std::move(fragment));

    return attributedString;
}

void measureIn(const std::string& fontFamily) {
    measureParagraphMetrics(hebrewIn(fontFamily), facebook::react::ParagraphAttributes{}, 200.0F);
}

/** Whether the thread `threadId` of this process is blocked in `openat`, as `/proc` reports its current syscall. */
bool blockedOpening(pid_t threadId) {
    std::ifstream syscallFile("/proc/self/task/" + std::to_string(threadId) + "/syscall");
    long syscallNumber = -1;

    syscallFile >> syscallNumber;

    return syscallNumber == SYS_openat;
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

/**
 * `expo-font` registers a file under a name of the app's choosing (#156), which need not be the face's own family or
 * PostScript name: that name draws without the #70 diagnostic, and an unregistered one still reports.
 */
TEST(ApplicationFontsTest, AFontRegisteredUnderAnAppChosenNameResolvesWithoutTheDiagnostic) {
    const bool registered = registerApplicationFont(
        "Brand Alias", {(std::filesystem::path(RNL_VENDORED_FONT_DIR) / "NotoSansHebrew-Regular.ttf").string()});

    measureIn("Brand Alias");
    measureIn("Brand Alias Unregistered");
    const bool listed = registers("Brand Alias");

    registerApplicationFonts(std::string());

    EXPECT_TRUE(registered);
    EXPECT_TRUE(listed);
    EXPECT_FALSE(reportedUnregistered("Brand Alias"));
    EXPECT_TRUE(reportedUnregistered("Brand Alias Unregistered"));
}

/**
 * `expo-font` registers from the JavaScript thread while the render thread paints (#156), so the file is read before
 * the pipeline's lock is taken: a registration stuck opening a FIFO that has no writer leaves layout, which takes
 * that lock on every call, free.
 */
TEST(ApplicationFontsTest, ARegistrationReadingItsFileLeavesTheTextPipelineFree) {
    const std::filesystem::path fifo =
        std::filesystem::temp_directory_path() / ("rnl-application-font-fifo-" + std::to_string(::getpid()));
    std::atomic<pid_t> registeringThread{0};

    layoutParagraph(hebrewIn("Noto Sans Hebrew"), facebook::react::ParagraphAttributes{}, 200.0F);
    std::filesystem::remove(fifo);
    ASSERT_EQ(::mkfifo(fifo.c_str(), 0600), 0);

    std::future<bool> registration = std::async(std::launch::async, [&] {
        registeringThread = static_cast<pid_t>(::syscall(SYS_gettid));

        return registerApplicationFont("Blocked Font", {fifo.string()});
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);

    while (!(registeringThread != 0 && blockedOpening(registeringThread)) &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    const bool reachedTheFile = blockedOpening(registeringThread);
    std::future<void> layout = std::async(std::launch::async, [] {
        layoutParagraph(hebrewIn("Noto Sans Hebrew"), facebook::react::ParagraphAttributes{}, 200.0F);
    });
    const bool laidOutWhileReading = layout.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    // A FIFO opened read-write never blocks and releases the reader's open; with no bytes the file is no font.
    const int writer = ::open(fifo.c_str(), O_RDWR);
    const bool registered = registration.get();

    layout.get();
    ::close(writer);
    std::filesystem::remove(fifo);

    EXPECT_TRUE(reachedTheFile);
    EXPECT_TRUE(laidOutWhileReading);
    EXPECT_FALSE(registered);
    EXPECT_FALSE(registers("Blocked Font"));
}

} // namespace
} // namespace react_native_linux
