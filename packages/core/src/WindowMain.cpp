#include "AutomationProtocol.h"
#include "AutomationServer.h"
#include "FrameClock.h"
#include "FrameJournal.h"
#include "FrameTiming.h"
#include "InputPipeline.h"
#include "LinuxMountingManager.h"
#include "RendererLadder.h"
#include "RetainedScene.h"
#include "ScenePainter.h"
#include "SharedMemoryRasterRenderer.h"
#include "SingleInstanceActivation.h"
#ifdef RNL_ENABLE_SINGLE_INSTANCE_ACTIVATION
#include "SingleInstanceCoordinator.h"
#endif
#include "SkiaVulkanRenderer.h"
#include "SurfaceCommitGate.h"
#include "TextInputClient.h"
#include "TitleBarPainter.h"
#include "ToplevelState.h"
#include "WaylandWindow.h"
#include "WindowDecorations.h"
#include "WindowRenderer.h"
#include "WindowSession.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRRect.h"
#include "include/core/SkRect.h"
#include "include/core/SkScalar.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <exception>
#include <filesystem>
#include <folly/json/dynamic.h>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kInitialWidth = 800;
constexpr uint32_t kInitialHeight = 600;
constexpr uint32_t kDefaultScreenshotFrames = 60;
constexpr std::chrono::milliseconds kFrameCallbackFallback{50};
constexpr SkColor kCardColor = SkColorSetRGB(0x33, 0x66, 0xCC);
constexpr SkScalar kCardInset = 64.0F;
constexpr SkScalar kCardCornerRadius = 24.0F;
constexpr std::string_view kFabricFlag = "--fabric";
constexpr std::string_view kScreenshotFlag = "--screenshot";
constexpr std::string_view kFramesFlag = "--frames";
constexpr std::string_view kFrameLogFlag = "--frame-log";
constexpr std::string_view kImeDebugFlag = "--ime-debug";
// --ime-debug has no shadow tree and therefore no tag; one identifier for the one field it pretends to have.
constexpr int32_t kImeDebugFieldIdentifier = 1;
constexpr std::string_view kWindowDebugFlag = "--window-debug";
constexpr std::string_view kAutomationFlag = "--automation";
constexpr std::string_view kRendererFlag = "--renderer";
constexpr std::string_view kAppIdFlag = "--app-id";
constexpr std::string_view kTitleFlag = "--title";
constexpr std::string_view kForceClientDecorationsFlag = "--force-client-decorations";
constexpr std::string_view kNoDecorationsFlag = "--no-decorations";
constexpr std::string_view kDefaultTitle = "react-native-linux";
constexpr std::string_view kDefaultApplicationIdentifier = "react-native-linux";
constexpr int kPrimaryPointerButton = 0;
constexpr std::string_view kInjectProtocolErrorFlag = "--inject-protocol-error";
constexpr std::string_view kInjectProtocolErrorAfterFrameFlag = "--inject-protocol-error-after-frame";
constexpr std::string_view kWindowErrorSource = "rnl-window";
constexpr std::string_view kImeDebugSurroundingText = "react-native-linux";
constexpr int32_t kImeDebugCursorX = 64;
constexpr int32_t kImeDebugCursorY = 64;
constexpr int32_t kImeDebugCursorWidth = 2;
constexpr int32_t kImeDebugCursorHeight = 24;
constexpr uint64_t kNanosecondsPerSecond = 1'000'000'000;

/**
 * `--screenshot <path>` runs the ordinary loop and reads the last presented swapchain image back into a PNG, so
 * the picture it writes came through the real Vulkan and Wayland path rather than an offscreen surface.
 * `--frames` is how long the bundle is given to mount and settle before that frame is captured.
 *
 * `--frame-log <path>` writes the `wp_presentation` measurements as JSON Lines: one record per presented frame
 * and a final summary line carrying the frame count, the discarded count and the p50, p95 and maximum frame
 * times. It is what the e2e driver's perf gate reads. See *Frame timing* in docs/cpp-toolchain.md.
 *
 * When a bundle is running, each presented frame's line is followed by the frame journal's own line for the same
 * event (#345) — the dirty-to-present latency and paint span, when the frame closed an open interval — and the
 * run ends with the journal's own summary line, beside `FrameTiming`'s. See *Frame journal* in
 * docs/cpp-toolchain.md.
 *
 * `--window-debug` is issue #218's manual proof, the same role `--ime-debug` plays for text composition: none of
 * the desktop lifecycle contract's activated/maximized/fullscreen/resizing bits, `wl_surface` enter/leave, or
 * keyboard focus reach JavaScript today — there is no `AppState`-equivalent module to carry them there, and
 * building one is out of #218's scope — so this prints every transition `WaylandWindow`/`WaylandSeat` decode to
 * stdout, which is how a developer under a real compositor (Hyprland, for instance, where activate/deactivate,
 * maximize and multi-monitor `wl_surface.enter` are all reachable) confirms the decode is correct end to end. See
 * *Window host* in docs/cpp-toolchain.md.
 * It also arms one injected `VK_ERROR_OUT_OF_DATE_KHR` at the first acquire, so the swapchain recreation the
 * `VkResult` policy prescribes is exercised on every run of the flag rather than only when a compositor happens
 * to invalidate the swapchain.
 * On top of that it walks the four `SurfaceCommitFault` states, one per frame, printing the action
 * `SurfaceCommitGate` chose and the frame that presented afterwards. Each of them is a way a Wayland client ends
 * up mapped and never shown, and none has a trigger a developer can otherwise pull: no compositor withholds the
 * initial configure on request, and no driver starves an acquire to order. The proof the flag gives is that the
 * window comes back — a present after every injected state — rather than going blank. See *Surface commit
 * ordering* in docs/cpp-toolchain.md.
 *
 * `--inject-protocol-error` is #331's fault-injection hook: right after the window is up but before the first
 * frame, it acknowledges the initial configure a second time with a serial the compositor never sent, which
 * xdg-shell requires it to reject. It exists to prove that the rejection is reported through `reportNativeError`
 * — `[rnl-window] wayland protocol error: <interface>#<id> code <n> (<errno text>)` — instead of the bare "broken
 * pipe" the process used to exit with, even on the fatal-before-dispatch path: bring-up has not yet read the
 * socket, so a fatal renderer failure this early can otherwise report its own symptom (an unrecoverable
 * `VkResult`, say) before the Wayland cause is ever read — `WaylandWindow::reportPendingDisplayError` closes that
 * gap. `--inject-protocol-error-after-frame` injects the same rejection once a frame has already presented
 * instead, proving the ordinary path: `WaylandWindow::dispatchWithTimeout`'s own event loop reports it, with
 * nothing to catch. See *Window host* in docs/cpp-toolchain.md.
 */
struct WindowArguments {
    std::optional<std::string> bundlePath;
    std::optional<react_native_linux::RendererRung> forcedRung;
    std::optional<std::string> screenshotPath;
    std::optional<std::string> frameLogPath;
    std::string title{kDefaultTitle};
    std::string applicationIdentifier{kDefaultApplicationIdentifier};
    uint32_t frameCount{kDefaultScreenshotFrames};
    bool automation{false};
    bool forceClientDecorations{false};
    bool noDecorations{false};
    bool imeDebug{false};
    bool windowDebug{false};
    bool injectProtocolError{false};
    bool injectProtocolErrorAfterFrame{false};
    std::string error;
};

/**
 * `--automation` opens the channel of issue #214: a line-delimited JSON socket under `XDG_RUNTIME_DIR` whose
 * path the window prints to the trace, so the e2e driver can ask what a screenshot cannot tell it — the errors
 * the runtime reported, the committed tree, and whether the bundle marked itself passed. Off unless the flag is
 * passed, so a shipped window never listens. See *The automation channel (#214)* in docs/cpp-toolchain.md.
 *
 * `pendingScreenshotPath` is why the dispatch lives in the frame loop rather than in the server: the picture
 * `TakeScreenshot` names only exists after the next present, so the request is armed on one iteration and
 * answered on a later one.
 */
struct AutomationChannel {
    std::optional<react_native_linux::AutomationServer> server;
    std::optional<std::string> pendingScreenshotPath;
};

/**
 * One sample of the clock `wp_presentation.clock_id` named, absent when there is no clock id, when it names
 * `CLOCK_MONOTONIC` — which `steady_clock` already is, so there is nothing to convert — and when the clock cannot
 * be read at all.
 */
std::optional<uint64_t> readPresentationClockNanoseconds(std::optional<uint32_t> presentationClockId) {
    if (!presentationClockId.has_value() || presentationClockId.value() == static_cast<uint32_t>(CLOCK_MONOTONIC)) {
        return std::nullopt;
    }

    timespec sample{};

    if (clock_gettime(static_cast<clockid_t>(presentationClockId.value()), &sample) != 0) {
        return std::nullopt;
    }

    return (static_cast<uint64_t>(sample.tv_sec) * kNanosecondsPerSecond) + static_cast<uint64_t>(sample.tv_nsec);
}

/**
 * Drains the `wp_presentation` feedback events `WaylandWindow` recorded since the last call — presented frames and
 * discarded content updates, in the order the compositor delivered them — writes `FrameTiming`'s line for each
 * presented one, and, when a session is running the frame journal, closes the matching journal interval or charges
 * a discontinuity in that same order. Order is what makes a presented-then-discarded pair report the
 * presentation's latency and its hang rather than losing both to the discard.
 *
 * The compositor's timestamps are in whatever clock `wp_presentation.clock_id` named, and the journal's dirty
 * edges come from `steady_clock`, so both clocks are sampled once per batch and the offset between them is
 * applied to every timestamp in it. See *Frame journal* in docs/cpp-toolchain.md.
 */
void writeFrameLines(std::ostream& frameLog, react_native_linux::WaylandWindow& window,
                     react_native_linux::WindowSession* session) {
    const std::vector<react_native_linux::WaylandWindow::PresentationEvent> events = window.takePresentationEvents();

    if (events.empty()) {
        return;
    }

    const int64_t clockOffsetNanoseconds = react_native_linux::presentationClockOffsetNanoseconds(
        window.presentationClockId(), readPresentationClockNanoseconds(window.presentationClockId()),
        static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));

    for (const react_native_linux::WaylandWindow::PresentationEvent& event : events) {
        if (!event.has_value()) {
            if (session != nullptr) {
                session->reportJournalDiscontinuity();
            }

            continue;
        }

        frameLog << react_native_linux::FrameTiming::formatFrameLine(event.value()) << "\n";

        if (session == nullptr) {
            continue;
        }

        const std::optional<react_native_linux::FrameJournal::ClosedFrame> closed = session->closeJournalFrame(
            react_native_linux::toSteadyClockNanoseconds(event.value().presentedNanoseconds, clockOffsetNanoseconds));

        if (closed.has_value()) {
            frameLog << react_native_linux::FrameJournal::formatClosedFrameLine(closed.value()) << "\n";
        }
    }
}

/**
 * `--ime-debug` is what proves `zwp_text_input_v3` before there is a `<TextInput>` to prove it with: it enables
 * the text input on the window itself as soon as the compositor gives it focus, reports a stub surrounding text
 * and a fixed caret rectangle so an input method has somewhere to put its candidate window, and prints every
 * composition batch. With fcitx5 running, typing CJK into the window prints the pre-edit as it is composed and
 * the commit that replaces it. See *IME* in docs/cpp-toolchain.md.
 */
class ImeDebugSink final : public react_native_linux::ImeSink {
public:
    void onImePreedit(const std::string& text, int32_t cursorBegin, int32_t cursorEnd) override {
        std::cout << "[rnl-ime] preedit \"" << text << "\" cursor " << cursorBegin << ".." << cursorEnd << std::endl;
    }

    void onImeCommit(const std::string& text) override {
        std::cout << "[rnl-ime] commit \"" << text << "\"" << std::endl;
    }

    void onImeDeleteSurrounding(uint32_t beforeLength, uint32_t afterLength) override {
        std::cout << "[rnl-ime] delete-surrounding before " << beforeLength << " after " << afterLength << std::endl;
    }
};

void enableImeDebug(react_native_linux::TextInputClient* textInput) {
    if (textInput == nullptr || !textInput->isFocused() || textInput->isEnabled()) {
        return;
    }

    const std::string surroundingText(kImeDebugSurroundingText);
    const int32_t cursor = static_cast<int32_t>(surroundingText.size());

    // Every request is cached until the flush, which is what puts the field, its content type, its text and its
    // caret on the wire as one batch.
    textInput->focusField(kImeDebugFieldIdentifier, react_native_linux::TextInputContentPurpose::Normal);
    textInput->setSurroundingText(surroundingText, cursor, cursor);
    textInput->setCursorRectangle(kImeDebugCursorX, kImeDebugCursorY, kImeDebugCursorWidth, kImeDebugCursorHeight);
    textInput->flushTextInput();

    std::cout << "[rnl-ime] enabled on the focused surface" << std::endl;
}

std::string describeMissingValue(std::string_view flag) {
    if (flag == kFabricFlag) {
        return "--fabric requires a bundle path";
    }

    if (flag == kScreenshotFlag) {
        return "--screenshot requires an output path";
    }

    if (flag == kFrameLogFlag) {
        return "--frame-log requires an output path";
    }

    if (flag == kRendererFlag) {
        return "--renderer requires one of preferred-vulkan, alternate-vulkan, software-vulkan, raster";
    }

    if (flag == kAppIdFlag) {
        return "--app-id requires an application identifier";
    }

    if (flag == kTitleFlag) {
        return "--title requires a window title";
    }

    return "--frames requires a positive frame count";
}

std::optional<uint32_t> parseFrameCount(std::string_view value) {
    uint32_t frameCount = 0;
    const std::from_chars_result parsed = std::from_chars(value.data(), value.data() + value.size(), frameCount);

    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || frameCount == 0) {
        return std::nullopt;
    }

    return frameCount;
}

WindowArguments parseArguments(std::span<char*> arguments) {
    WindowArguments parsed;

    for (size_t index = 1; index < arguments.size(); ++index) {
        const std::string_view flag = arguments[index];

        if (flag == kImeDebugFlag) {
            parsed.imeDebug = true;

            continue;
        }

        if (flag == kWindowDebugFlag) {
            parsed.windowDebug = true;

            continue;
        }

        if (flag == kAutomationFlag) {
            parsed.automation = true;

            continue;
        }

        if (flag == kForceClientDecorationsFlag) {
            parsed.forceClientDecorations = true;

            continue;
        }

        if (flag == kInjectProtocolErrorFlag) {
            parsed.injectProtocolError = true;

            continue;
        }

        if (flag == kInjectProtocolErrorAfterFrameFlag) {
            parsed.injectProtocolErrorAfterFrame = true;

            continue;
        }

        if (flag == kNoDecorationsFlag) {
            parsed.noDecorations = true;

            continue;
        }

        if (flag != kFabricFlag && flag != kScreenshotFlag && flag != kFramesFlag && flag != kFrameLogFlag &&
            flag != kRendererFlag && flag != kAppIdFlag && flag != kTitleFlag) {
            // Not a flag at all: the desktop entry's `Exec=... %u` expansion for single-instance activation
            // (#363) hands this process a bare URL, with no `--` of its own. Nothing here consumes it — the
            // single-instance check reads the *original* argv directly, ahead of this parse — so it is not an
            // error, only not a flag this parser has anything to do with.
            if (!flag.starts_with("--")) {
                continue;
            }

            parsed.error = "unknown argument " + std::string(flag);

            return parsed;
        }

        if (index + 1 >= arguments.size()) {
            parsed.error = describeMissingValue(flag);

            return parsed;
        }

        const std::string_view value = arguments[index + 1];
        ++index;

        if (flag == kFabricFlag) {
            parsed.bundlePath = std::string(value);
        } else if (flag == kScreenshotFlag) {
            parsed.screenshotPath = std::string(value);
        } else if (flag == kFrameLogFlag) {
            parsed.frameLogPath = std::string(value);
        } else if (flag == kRendererFlag) {
            parsed.forcedRung = react_native_linux::parseRendererRung(value);

            if (!parsed.forcedRung.has_value()) {
                parsed.error = describeMissingValue(kRendererFlag);

                return parsed;
            }
        } else if (flag == kAppIdFlag) {
            parsed.applicationIdentifier = std::string(value);
        } else if (flag == kTitleFlag) {
            parsed.title = std::string(value);
        } else {
            const std::optional<uint32_t> frameCount = parseFrameCount(value);

            if (!frameCount.has_value()) {
                parsed.error = describeMissingValue(flag);

                return parsed;
            }

            parsed.frameCount = frameCount.value();
        }
    }

    return parsed;
}

/**
 * Unconditional and independent of `--window-debug`: the e2e driver (#304) waits for this line before the first
 * keyboard step, so it has to survive whether or not a developer asked for the full debug trace. The tag is
 * `[rnl-focus]`, not `[rnl-window]`, because `ERROR_TRACE_PATTERNS` in `scripts/e2e/scenario.ts` treats any
 * `[rnl-window]` line as a fault — this one fires on the ordinary, expected first `wl_keyboard.enter` and must
 * not trip that gate. It prints once, because the driver only ever waits for focus to arrive, never to leave.
 */
void announceKeyboardFocusOnce(react_native_linux::WaylandWindow& window, bool& keyboardFocusAnnounced) {
    if (!keyboardFocusAnnounced && window.hasKeyboardFocus()) {
        keyboardFocusAnnounced = true;
        std::cout << "[rnl-focus] keyboard entered" << std::endl;
    }
}

void printWindowDebugTransitions(react_native_linux::WaylandWindow& window, bool& lastKeyboardFocus,
                                 uint32_t& lastOutputEnterCount, uint32_t& lastOutputLeaveCount) {
    if (window.takeStateChange()) {
        const react_native_linux::ToplevelState state = window.toplevelState();
        std::cout << "[rnl-window] state activated=" << state.activated << " maximized=" << state.maximized
                  << " fullscreen=" << state.fullscreen << " resizing=" << state.resizing
                  << " tiled=" << react_native_linux::isEffectivelyTiled(state) << std::endl;
    }

    const bool keyboardFocus = window.hasKeyboardFocus();

    if (keyboardFocus != lastKeyboardFocus) {
        std::cout << "[rnl-window] keyboard " << (keyboardFocus ? "enter" : "leave") << std::endl;
        lastKeyboardFocus = keyboardFocus;
    }

    const uint32_t outputEnterCount = window.outputEnterCount();
    const uint32_t outputLeaveCount = window.outputLeaveCount();

    if (outputEnterCount != lastOutputEnterCount || outputLeaveCount != lastOutputLeaveCount) {
        std::cout << "[rnl-window] surface enter=" << outputEnterCount << " leave=" << outputLeaveCount << std::endl;
        lastOutputEnterCount = outputEnterCount;
        lastOutputLeaveCount = outputLeaveCount;
    }
}

constexpr std::array<react_native_linux::SurfaceCommitFault, 4> kInjectedSurfaceCommitFaults{
    react_native_linux::SurfaceCommitFault::CommitBeforeConfigure,
    react_native_linux::SurfaceCommitFault::BufferExtentMismatch,
    react_native_linux::SurfaceCommitFault::AcquireStarvation,
    react_native_linux::SurfaceCommitFault::ContentUpdateDiscarded,
};

/**
 * One fault every other frame, so the frame between two of them is an ordinary one and its present is the proof
 * the window recovered rather than a present the fault itself happened to allow.
 */
constexpr uint32_t kFramesPerInjectedFault = 2;

void injectNextSurfaceCommitFault(react_native_linux::SkiaVulkanRenderer& renderer, uint32_t& injectedFaultCount) {
    if (injectedFaultCount >= kInjectedSurfaceCommitFaults.size() * kFramesPerInjectedFault) {
        return;
    }

    const uint32_t step = injectedFaultCount;

    ++injectedFaultCount;

    if (step % kFramesPerInjectedFault != 0) {
        return;
    }

    const react_native_linux::SurfaceCommitFault fault = kInjectedSurfaceCommitFaults[step / kFramesPerInjectedFault];

    renderer.injectSurfaceCommitFaultOnNextFrame(fault);
    std::cout << "[rnl-window] injecting surface-commit fault " << react_native_linux::describeSurfaceCommitFault(fault)
              << std::endl;
}

void printSurfaceCommitOutcome(const react_native_linux::SkiaVulkanRenderer& renderer, bool presented) {
    std::cout << "[rnl-window] surface-commit "
              << react_native_linux::describeSurfaceCommitAction(renderer.lastSurfaceCommitAction())
              << (presented ? " presented" : " attached nothing") << std::endl;
}

folly::dynamic answerSessionCommand(react_native_linux::AutomationCommand command,
                                    const react_native_linux::AutomationRequest& request,
                                    react_native_linux::WindowSession& session) {
    if (command == react_native_linux::AutomationCommand::DumpVisualTree) {
        return react_native_linux::describeVisualTree(session.visualTreeNodes());
    }

    if (command == react_native_linux::AutomationCommand::DumpAccessibilityTree) {
        return react_native_linux::describeAccessibilityTree(session.visualTreeNodes());
    }

    if (command == react_native_linux::AutomationCommand::ListAccessibilityChanges) {
        return react_native_linux::describeAccessibilityChanges(session.takeAccessibilityChanges());
    }

    if (command == react_native_linux::AutomationCommand::HangForTesting) {
        session.blockJavaScriptThread(std::chrono::milliseconds(request.hangMilliseconds));

        return folly::dynamic::object("milliseconds", request.hangMilliseconds);
    }

    return folly::dynamic::object("passed", session.hasMarkedTestPassed());
}

void answerAutomationRequest(AutomationChannel& automation, const react_native_linux::AutomationRequest& request,
                             react_native_linux::WindowRenderer& renderer, react_native_linux::WindowSession* session) {
    if (request.command == react_native_linux::AutomationCommand::ListErrors) {
        automation.server->sendResponse(react_native_linux::formatAutomationResponse(
            request.command, react_native_linux::describeErrors(react_native_linux::automationErrorLog().list())));

        return;
    }

    if (request.command == react_native_linux::AutomationCommand::TakeScreenshot) {
        renderer.captureNextFrame(request.screenshotPath);
        automation.pendingScreenshotPath = request.screenshotPath;

        return;
    }

    if (session == nullptr) {
        automation.server->sendResponse(react_native_linux::formatAutomationFailure("no bundle is running"));

        return;
    }

    automation.server->sendResponse(react_native_linux::formatAutomationResponse(
        request.command, answerSessionCommand(request.command, request, *session)));
}

/**
 * One request per frame, at most: the channel is an assertion surface rather than a data path, and answering one
 * line per iteration keeps a driver that floods it from starving the frame loop it is measuring.
 */
void serveAutomation(AutomationChannel& automation, react_native_linux::WindowRenderer& renderer,
                     react_native_linux::WindowSession* session) {
    if (automation.pendingScreenshotPath.has_value()) {
        if (renderer.hasPendingCapture()) {
            return;
        }

        automation.server->sendResponse(react_native_linux::formatAutomationResponse(
            react_native_linux::AutomationCommand::TakeScreenshot,
            folly::dynamic::object("path", automation.pendingScreenshotPath.value())));
        automation.pendingScreenshotPath.reset();

        return;
    }

    const std::optional<std::string> line = automation.server->takeRequestLine();

    if (!line.has_value()) {
        return;
    }

    const react_native_linux::AutomationRequestParse parsed = react_native_linux::parseAutomationRequest(line.value());

    if (!parsed.request.has_value()) {
        automation.server->sendResponse(react_native_linux::formatAutomationFailure(parsed.error));

        return;
    }

    answerAutomationRequest(automation, parsed.request.value(), renderer, session);
}

std::string_view decorationModeName(react_native_linux::DecorationMode mode) {
    switch (mode) {
    case react_native_linux::DecorationMode::Client:
        return "client";
    case react_native_linux::DecorationMode::Bare:
        return "bare";
    default:
        return "server";
    }
}

/**
 * Everything the drawn title bar needs that is not in `WindowDecorations`: where the content starts, whether the
 * bar has changed since it was last painted, and the two-press memory the double-click-to-maximize rule keeps.
 * Under server-side decorations `mode` stays `Server`, `content.topOffset` stays zero and none of the routing or
 * painting below does anything at all — one code path, with the bar's height as its only variable.
 */
struct WindowChrome {
    react_native_linux::DecorationMetrics metrics;
    react_native_linux::DoubleClickDetector doubleClick;
    react_native_linux::PointerCapture pointerCapture;
    react_native_linux::DecorationMode mode{react_native_linux::DecorationMode::Server};
    react_native_linux::ContentExtent content{};
    bool wasActive{false};
    bool isFullscreen{false};
    bool isRepaintNeeded{true};
};

void refreshChrome(WindowChrome& chrome, react_native_linux::WaylandWindow& window) {
    const react_native_linux::DecorationMode mode = window.decorationMode();
    const react_native_linux::WindowSize size = window.size();
    const react_native_linux::ToplevelState state = window.toplevelState();
    const react_native_linux::ContentExtent content =
        react_native_linux::contentExtentOf(mode, state.fullscreen, chrome.metrics, size.width, size.height);
    const bool isActive = state.activated;

    // `content.height` joins `content.width` here for the same reason fullscreen took a parameter of its own on
    // `contentExtentOf` (#374): entering or leaving fullscreen can drop or restore the bar's inset without the
    // window's own width or height changing, and a stale `topOffset` would shift the paint and the pointer
    // mapping out of step with what the Fabric root was actually laid out at.
    chrome.isRepaintNeeded = chrome.isRepaintNeeded || mode != chrome.mode || isActive != chrome.wasActive ||
                             content.width != chrome.content.width || content.height != chrome.content.height;

    if (mode != chrome.mode || state.fullscreen != chrome.isFullscreen) {
        chrome.pointerCapture.release();
    }

    chrome.mode = mode;
    chrome.content = content;
    chrome.wasActive = isActive;
    chrome.isFullscreen = state.fullscreen;
}

uint64_t steadyMilliseconds() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

bool carriesSurfacePoint(react_native_linux::InputEventKind kind) {
    return kind == react_native_linux::InputEventKind::PointerMotion ||
           kind == react_native_linux::InputEventKind::PointerButtonPress ||
           kind == react_native_linux::InputEventKind::PointerButtonRelease ||
           kind == react_native_linux::InputEventKind::PointerScrollContinuous ||
           kind == react_native_linux::InputEventKind::PointerScrollDiscrete ||
           kind == react_native_linux::InputEventKind::PointerScrollStop;
}

void activateDecoration(react_native_linux::WaylandWindow& window, WindowChrome& chrome,
                        react_native_linux::DecorationHit hit) {
    if (hit == react_native_linux::DecorationHit::Close) {
        std::cout << "[rnl-decorations] close" << std::endl;
        window.requestClose();

        return;
    }

    if (hit == react_native_linux::DecorationHit::Minimize) {
        window.minimize();

        return;
    }

    if (hit == react_native_linux::DecorationHit::Maximize) {
        window.toggleMaximized();

        return;
    }

    if (hit == react_native_linux::DecorationHit::Drag) {
        if (chrome.doubleClick.recordPress(steadyMilliseconds())) {
            window.toggleMaximized();

            return;
        }

        window.startInteractiveMove();

        return;
    }

    window.startInteractiveResize(react_native_linux::resizeEdgeOfHit(hit));
}

/**
 * Splits one frame's events into the chrome's and the application's. A pointer event over the bar or the resize
 * gutter never reaches the scene — the compositor takes over the pointer the instant `xdg_toplevel.move` or
 * `.resize` is sent, so forwarding it as well would leave a press the application never sees released — and
 * everything below the bar is translated into the content's coordinate system, which is the same shift the paint
 * applies to the canvas.
 *
 * The hit test alone only says where an event landed, not which side owns the gesture: `chrome.pointerCapture`
 * is what makes a press that starts in the content keep reaching the content through a drag that ends on the
 * bar, and symmetrically for a press that starts on the bar, per #399.
 */
std::vector<react_native_linux::InputEvent>
routeDecorationInput(react_native_linux::WaylandWindow& window, WindowChrome& chrome,
                     const std::vector<react_native_linux::InputEvent>& events) {
    if (!react_native_linux::isChromeActive(chrome.mode, chrome.isFullscreen)) {
        return events;
    }

    const react_native_linux::WindowSize size = window.size();
    const bool isTiled = react_native_linux::isEffectivelyTiled(window.toplevelState());
    std::vector<react_native_linux::InputEvent> contentEvents;

    for (const react_native_linux::InputEvent& event : events) {
        if (!carriesSurfacePoint(event.kind)) {
            contentEvents.push_back(event);

            continue;
        }

        const react_native_linux::DecorationHit hit = react_native_linux::hitTestDecorations(
            chrome.metrics, size.width, size.height, isTiled, static_cast<float>(event.surfacePoint.x),
            static_cast<float>(event.surfacePoint.y));

        const bool isPrimaryPress = event.kind == react_native_linux::InputEventKind::PointerButtonPress &&
                                    event.button == kPrimaryPointerButton;
        const bool isPrimaryRelease = event.kind == react_native_linux::InputEventKind::PointerButtonRelease &&
                                      event.button == kPrimaryPointerButton;
        const bool routedToContent = chrome.pointerCapture.routeToContent(hit, isPrimaryPress, isPrimaryRelease);

        if (!routedToContent) {
            if (isPrimaryPress) {
                activateDecoration(window, chrome, hit);
            }

            continue;
        }

        react_native_linux::InputEvent contentEvent = event;
        contentEvent.surfacePoint.y -= chrome.content.topOffset;
        contentEvents.push_back(contentEvent);
    }

    return contentEvents;
}

/**
 * Runs `paint` with the canvas shifted below the bar, then draws the bar over it. The damage the scene produced
 * is in content coordinates and the damage the renderer accumulates is in surface ones, so it is shifted back by
 * the same offset on the way in — the one place, besides the canvas translate, where the two coordinate systems
 * meet.
 */
void paintDecoratedFrame(SkCanvas& canvas, const WindowChrome& chrome, const std::string& title,
                         const react_native_linux::SceneDamage& surfaceDamage,
                         const std::function<void(SkCanvas&, const react_native_linux::SceneDamage&)>& paint) {
    react_native_linux::SceneDamage contentDamage = surfaceDamage;

    for (facebook::react::Rect& rectangle : contentDamage) {
        rectangle.origin.y -= chrome.content.topOffset;
    }

    canvas.save();
    canvas.translate(0.0F, chrome.content.topOffset);
    paint(canvas, contentDamage);
    canvas.restore();

    if (!react_native_linux::isChromeActive(chrome.mode, chrome.isFullscreen)) {
        return;
    }

    react_native_linux::paintTitleBar(canvas, react_native_linux::layoutTitleBar(chrome.content.width, chrome.metrics),
                                      title, chrome.wasActive);
}

void paintPlaceholderFrame(SkCanvas& canvas, react_native_linux::WindowSize size,
                           const react_native_linux::SceneDamage& /*damage*/) {
    canvas.clear(react_native_linux::kSceneBackgroundColor);

    const SkRect cardBounds = SkRect::MakeLTRB(kCardInset, kCardInset, static_cast<SkScalar>(size.width) - kCardInset,
                                               static_cast<SkScalar>(size.height) - kCardInset);

    SkPaint cardPaint;
    cardPaint.setColor(kCardColor);
    cardPaint.setAntiAlias(true);

    canvas.drawRRect(SkRRect::MakeRectXY(cardBounds, kCardCornerRadius, kCardCornerRadius), cardPaint);
}

/**
 * The ladder of #368, applied at bring-up: the rung the policy chose, then every rung below it, until one comes
 * up. A rung that throws is a rung this machine cannot use — no second device, no lavapipe installed, a driver
 * that refuses to create a context — and moving past it in the same process is what turns that into the next
 * rung instead of an exit. The persisted record is written before each attempt and cleared by the first
 * presented frame, which is how a rung that does not throw but *crashes* is caught by the next launch instead.
 * See *The renderer ladder (#368)* in docs/cpp-toolchain.md.
 */
struct RendererBringUp {
    std::unique_ptr<react_native_linux::WindowRenderer> renderer;
    react_native_linux::SkiaVulkanRenderer* vulkanRenderer{nullptr};
    react_native_linux::RendererLadderRecord record;
};

std::optional<std::string> ladderStatePath() {
    const char* stateHome = std::getenv("XDG_STATE_HOME");
    const char* home = std::getenv("HOME");

    return react_native_linux::rendererLadderStatePath(stateHome == nullptr ? "" : stateHome,
                                                       home == nullptr ? "" : home);
}

std::optional<react_native_linux::RendererLadderRecord> readLadderRecord(const std::optional<std::string>& path) {
    if (!path.has_value()) {
        return std::nullopt;
    }

    std::ifstream stored(path.value());

    if (!stored.is_open()) {
        return std::nullopt;
    }

    const std::string contents((std::istreambuf_iterator<char>(stored)), std::istreambuf_iterator<char>());

    return react_native_linux::parseRendererLadderRecord(contents);
}

void writeLadderRecord(const std::optional<std::string>& path, const react_native_linux::RendererLadderRecord& record) {
    if (!path.has_value()) {
        return;
    }

    std::error_code directoryError;
    std::filesystem::create_directories(std::filesystem::path(path.value()).parent_path(), directoryError);

    if (directoryError) {
        return;
    }

    std::ofstream stored(path.value(), std::ios::trunc);

    stored << react_native_linux::formatRendererLadderRecord(record);
}

/**
 * The one first-presentation event #396 and #373 both need: `presented` is `drawFrame`'s own immediate result,
 * true on the same call whether or not the loop below ever runs, which is what lets `--screenshot --frames 1`
 * clear the crash count and print the readiness line before the process can exit without another Wayland round
 * trip; `window.hasPresentedFirstFrame()` is the compositor's own confirmation via `wp_presentation_feedback` (or
 * `wl_surface.frame` when presentation is not supported), which a longer-lived run dispatches on an earlier
 * iteration's `waitForRedraw`. Either one means this rung's first frame reached the screen, so it fires on
 * whichever comes first. The tag is `[rnl-present]`, not `[rnl-window]`, because `ERROR_TRACE_PATTERNS` in
 * `scripts/e2e/scenario.ts` treats any `[rnl-window]` line as a fault, and this one fires on the ordinary,
 * expected first presented frame. Called after the startup draw and again inside the loop, because the startup
 * draw alone can be the only frame this process ever presents.
 */
void announceFirstPresentedFrameOnce(react_native_linux::WaylandWindow& window, bool presented,
                                     bool& firstPresentedFrameAnnounced, const std::optional<std::string>& ladderPath,
                                     const react_native_linux::RendererLadderRecord& record) {
    if (!firstPresentedFrameAnnounced && (presented || window.hasPresentedFirstFrame())) {
        firstPresentedFrameAnnounced = true;
        std::cout << "[rnl-present] first frame presented" << std::endl;
        writeLadderRecord(ladderPath, react_native_linux::recordFirstPresentedFrame(record));
    }
}

RendererBringUp createRenderer(react_native_linux::WaylandWindow& window, react_native_linux::RendererRung rung) {
    if (rung == react_native_linux::RendererRung::SharedMemoryRaster) {
        return RendererBringUp{.renderer = std::make_unique<react_native_linux::SharedMemoryRasterRenderer>(
                                   window.sharedMemory(), window.surface(), window.size())};
    }

    std::unique_ptr<react_native_linux::SkiaVulkanRenderer> vulkanRenderer =
        std::make_unique<react_native_linux::SkiaVulkanRenderer>(window.display(), window.surface(), window.size(),
                                                                 rung);
    react_native_linux::SkiaVulkanRenderer* borrowed = vulkanRenderer.get();

    return RendererBringUp{.renderer = std::move(vulkanRenderer), .vulkanRenderer = borrowed};
}

RendererBringUp bringUpRenderer(react_native_linux::WaylandWindow& window, const WindowArguments& parsedArguments,
                                const std::optional<std::string>& statePath, std::string_view driverIdentity) {
    const std::optional<react_native_linux::RendererLadderRecord> persisted = readLadderRecord(statePath);
    const react_native_linux::RendererStart start =
        react_native_linux::rendererStartRung(persisted, parsedArguments.forcedRung, driverIdentity);
    std::optional<react_native_linux::RendererRung> rung = start.rung;
    std::string lastFailure;

    if (parsedArguments.windowDebug) {
        std::cout << "[rnl-window] renderer ladder starts at " << react_native_linux::describeRendererRung(start.rung)
                  << ": " << react_native_linux::describeRendererStartReason(start.reason) << std::endl;
    }

    while (rung.has_value()) {
        const react_native_linux::RendererLadderRecord attempt =
            react_native_linux::recordRendererAttempt(persisted, rung.value(), driverIdentity);

        writeLadderRecord(statePath, attempt);

        try {
            RendererBringUp broughtUp = createRenderer(window, rung.value());

            broughtUp.record = attempt;

            if (parsedArguments.windowDebug) {
                std::cout << "[rnl-window] renderer rung " << react_native_linux::describeRendererRung(rung.value())
                          << " came up on "
                          << (broughtUp.vulkanRenderer == nullptr ? std::string("wl_shm")
                                                                  : broughtUp.vulkanRenderer->driverIdentity())
                          << std::endl;
            }

            return broughtUp;
        } catch (const std::exception& error) {
            lastFailure = error.what();

            if (parsedArguments.windowDebug) {
                std::cout << "[rnl-window] renderer rung " << react_native_linux::describeRendererRung(rung.value())
                          << " failed to come up: " << lastFailure << std::endl;
            }
        }

        rung = react_native_linux::nextRendererRung(rung.value());
    }

    throw std::runtime_error("every renderer rung failed to come up; the last said: " + lastFailure);
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> arguments(argv, static_cast<size_t>(argc));
    const WindowArguments parsedArguments = parseArguments(arguments);

    if (!parsedArguments.error.empty()) {
        std::cerr << "[rnl-window] " << parsedArguments.error << std::endl;

        return 1;
    }

    // Single-instance activation (#363), ahead of any Wayland or Vulkan bring-up: a second launch that loses the
    // name race has nothing left to do but forward its argv and cwd to whichever process holds it and exit, and
    // it has to decide that before paying for a window it is about to close.
#ifdef RNL_ENABLE_SINGLE_INSTANCE_ACTIVATION
    std::optional<react_native_linux::SingleInstanceCoordinator> singleInstanceCoordinator;
    std::optional<std::string> ownActivationUrl;
    {
        const std::vector<std::string> ownArgv(arguments.begin(), arguments.end());
        const react_native_linux::ActivationRequest ownActivation =
            react_native_linux::buildActivationRequest(ownArgv, std::filesystem::current_path().native());

        singleInstanceCoordinator.emplace(parsedArguments.applicationIdentifier, ownActivation);
        ownActivationUrl = react_native_linux::extractActivationUrl(ownActivation.argv);
    }

    if (!singleInstanceCoordinator->isPrimaryInstance()) {
        return 0;
    }
#else
    const std::optional<std::string> ownActivationUrl;
#endif

    try {
        react_native_linux::WaylandWindow window(
            react_native_linux::WindowIdentity{.title = parsedArguments.title,
                                               .applicationIdentifier = parsedArguments.applicationIdentifier,
                                               .forceClientDecorations = parsedArguments.forceClientDecorations,
                                               .noDecorations = parsedArguments.noDecorations},
            react_native_linux::WindowSize{kInitialWidth, kInitialHeight});

        // A fatal failure below this line — an unrecoverable `VkResult` from the swapchain path, for instance —
        // can be the symptom of a Wayland protocol error the process has not read off the socket yet (#331):
        // draining the display before the outer catch reports the symptom is what puts the structured line on
        // the trace ahead of it. See `WaylandWindow::reportPendingDisplayError`.
        try {
            const std::optional<std::string> ladderPath = ladderStatePath();
            const std::string driverIdentity = react_native_linux::probeVulkanDriverIdentity();
            RendererBringUp broughtUp = bringUpRenderer(window, parsedArguments, ladderPath, driverIdentity);
            react_native_linux::WindowRenderer& renderer = *broughtUp.renderer;
            bool firstPresentedFrameAnnounced = false;
            std::optional<react_native_linux::WindowSession> session;
            WindowChrome chrome;

            refreshChrome(chrome, window);
            std::cout << "[rnl-decorations] mode=" << decorationModeName(chrome.mode)
                      << " app-id=" << parsedArguments.applicationIdentifier << " content=" << chrome.content.width
                      << "x" << chrome.content.height << std::endl;

            const auto drawPlaceholder = [&chrome, &window](SkCanvas& canvas, react_native_linux::WindowSize /*size*/,
                                                            const react_native_linux::SceneDamage& surfaceDamage) {
                paintDecoratedFrame(
                    canvas, chrome, window.title(), surfaceDamage,
                    [&chrome](SkCanvas& contentCanvas, const react_native_linux::SceneDamage& contentDamage) {
                        paintPlaceholderFrame(
                            contentCanvas, react_native_linux::WindowSize{chrome.content.width, chrome.content.height},
                            contentDamage);
                    });
            };

            if (parsedArguments.injectProtocolError) {
                window.injectInvalidAckConfigureForTesting();
            }

            // Known as soon as the registry roundtrip in WaylandWindow's constructor completes, so the degradation is
            // named once, here, rather than inferred later from which readiness signal happened to fire first.
            if (!window.isPresentationSupported()) {
                std::cout << "[rnl-present] no wp_presentation; degrading readiness to wl_surface.frame" << std::endl;
            }

            // This is the first buffer the compositor can ever show, so `--frames 1` has to name this present rather
            // than the loop's next one: the capture is armed beforehand, exactly as the loop arms it for every later
            // frame, so a first-buffer failure that recovers on the next frame cannot pass a fixture that means to
            // check the first one. See *Surface commit ordering* in docs/cpp-toolchain.md.
            uint32_t presentedFrames = 0;
            const bool isStartupCaptureFrame =
                parsedArguments.screenshotPath.has_value() && presentedFrames + 1 >= parsedArguments.frameCount;

            if (isStartupCaptureFrame) {
                renderer.captureNextFrame(parsedArguments.screenshotPath.value());
            }

            const bool startupFramePresented = renderer.drawFrame(window, {}, drawPlaceholder);

            if (startupFramePresented) {
                ++presentedFrames;
            }

            // The bring-up hook above proves the fatal-before-dispatch path (#331): a renderer failure that
            // happens before the client ever reads the socket. This one proves the ordinary path instead — the
            // surface is already up and a frame already presented, so the same injected error is expected to
            // reach `WaylandWindow::dispatchWithTimeout` through the normal event loop rather than through a
            // caught exception.
            if (parsedArguments.injectProtocolErrorAfterFrame) {
                window.injectInvalidAckConfigureForTesting();
            }

            announceFirstPresentedFrameOnce(window, startupFramePresented, firstPresentedFrameAnnounced, ladderPath,
                                            broughtUp.record);

            bool hasCaptured = isStartupCaptureFrame && !renderer.hasPendingCapture();

            if (parsedArguments.bundlePath.has_value()) {
                session.emplace(parsedArguments.bundlePath.value(),
                                react_native_linux::WindowSize{chrome.content.width, chrome.content.height},
                                ownActivationUrl);

                // --ime-debug owns the text input by hand, so focus must not also drive it: the two would race to
                // enable and disable the same object. Without that flag, focus is the only thing that touches it.
                if (!parsedArguments.imeDebug) {
                    session->setTextInputFocusSink(window.textInput());
                }
            }

            if (parsedArguments.imeDebug && window.textInput() == nullptr) {
                react_native_linux::reportNativeError(kWindowErrorSource,
                                                      "the compositor does not advertise zwp_text_input_manager_v3");
            }

            // The recreation paths of the VkResult policy have no other trigger a developer can pull: a headless
            // compositor never resizes the window, never loses the surface and never resets the GPU, so without this
            // they are only ever reached on a real desktop by closing a lid or updating a driver. One injected
            // VK_ERROR_OUT_OF_DATE_KHR at the first acquire rebuilds the swapchain, and one injected
            // VK_ERROR_DEVICE_LOST at the second rebuilds the device, the queue and the GrDirectContext and drops
            // every resource GpuResourceInvalidation.h marks device-owned. Both frames after them are full
            // repaints. See *VkResult policy* in docs/cpp-toolchain.md.
            // Vulkan-only: the raster rung has no swapchain to lose and no surface-commit state machine to fault.
            if (parsedArguments.windowDebug && broughtUp.vulkanRenderer != nullptr) {
                broughtUp.vulkanRenderer->injectSwapchainLossOnNextFrame();
                broughtUp.vulkanRenderer->injectDeviceLossOnNextFrame();
            }

            AutomationChannel automation;

            if (parsedArguments.automation) {
                automation.server.emplace(react_native_linux::defaultAutomationSocketPath());

                // The trace, because that is the only channel the driver is already reading when the window starts.
                std::cout << "[rnl-automation] listening on " << automation.server->socketPath() << std::endl;
            }

            ImeDebugSink imeDebugSink;
            std::optional<std::ofstream> frameLog;
            bool lastKeyboardFocus = false;
            bool keyboardFocusAnnounced = false;
            uint32_t lastOutputEnterCount = 0;
            uint32_t lastOutputLeaveCount = 0;
            uint32_t injectedFaultCount = 0;

            if (parsedArguments.frameLogPath.has_value()) {
                frameLog.emplace(parsedArguments.frameLogPath.value());
            }

            while (!window.isClosed() && !hasCaptured) {
                // Presentation feedback for the frames committed before this iteration arrived during the previous
                // waitForRedraw, so the drain belongs at the top of the loop rather than beside the present.
                if (frameLog.has_value()) {
                    writeFrameLines(frameLog.value(), window, session.has_value() ? &session.value() : nullptr);
                }

                const bool hasResized = window.takePendingResize();
                const react_native_linux::ContentExtent previousChromeContent = chrome.content;

                // The chrome first, and unconditionally: the content extent a resize hands the session is measured
                // below the bar, and the bar's own active state can change without any resize at all.
                refreshChrome(chrome, window);

                // A `zxdg_toplevel_decoration_v1.configure` can switch decoration modes with no window resize at
                // all, which moves the content extent by the bar's height without `hasResized` ever being true —
                // the session has to see that too, or its viewport stays sized for the mode it no longer has.
                const bool hasContentExtentChanged = chrome.content.width != previousChromeContent.width ||
                                                     chrome.content.height != previousChromeContent.height;

                if (hasResized) {
                    renderer.resize(window.size());
                }

                if ((hasResized || hasContentExtentChanged) && session.has_value()) {
                    session->resize(react_native_linux::WindowSize{chrome.content.width, chrome.content.height});
                }

                announceKeyboardFocusOnce(window, keyboardFocusAnnounced);

                if (parsedArguments.windowDebug) {
                    printWindowDebugTransitions(window, lastKeyboardFocus, lastOutputEnterCount, lastOutputLeaveCount);

                    if (broughtUp.vulkanRenderer != nullptr) {
                        injectNextSurfaceCommitFault(*broughtUp.vulkanRenderer, injectedFaultCount);
                    }
                }

                // The capture is armed before the frame that carries it, because the readback happens inside
                // drawFrame while the image is still owned by this process. A frame that rebuilds the swapchain
                // instead of painting leaves the request pending, so the next one takes it.
                if (automation.server.has_value()) {
                    serveAutomation(automation, renderer, session.has_value() ? &session.value() : nullptr);
                }

                // Never while an automation capture is armed: the two would name the same pending path and one
                // picture would be written to the other's file.
                const bool isCaptureFrame = parsedArguments.screenshotPath.has_value() &&
                                            !automation.pendingScreenshotPath.has_value() &&
                                            presentedFrames + 1 >= parsedArguments.frameCount;

                if (isCaptureFrame) {
                    renderer.captureNextFrame(parsedArguments.screenshotPath.value());
                }

                const std::vector<react_native_linux::InputEvent> frameEvents =
                    routeDecorationInput(window, chrome, window.takeInputEvents());

                if (parsedArguments.imeDebug) {
                    enableImeDebug(window.textInput());

                    for (const react_native_linux::InputEvent& event : frameEvents) {
                        react_native_linux::deliverImeEvent(event, imeDebugSink);
                    }
                }

                bool presented = false;

                // Pumped once per frame, on the same beat as `AppearancePortal` (#363): a later instance's
                // forwarded activation can arrive at any time this process keeps running, not only at startup.
#ifdef RNL_ENABLE_SINGLE_INSTANCE_ACTIVATION
                if (singleInstanceCoordinator.has_value() && singleInstanceCoordinator->isPrimaryInstance()) {
                    const std::optional<std::string> activatedUrl =
                        singleInstanceCoordinator->takePendingActivationUrl();

                    if (activatedUrl.has_value()) {
                        std::cout << "[rnl-single-instance] activation received" << std::endl;

                        if (session.has_value()) {
                            session->deliverActivationUrl(activatedUrl.value());
                        }
                    }
                }
#endif

                if (session.has_value()) {
                    // Input first, and unconditionally: the event beat is induced inside this call, and it is what
                    // releases everything Fabric has queued since the last frame onto the JavaScript thread.
                    session->deliverInput(frameEvents);

                    // A frame callback always draws; a fallback timeout draws only if the session reports pending
                    // work, so an occluded window with nothing left to animate stops spinning the GPU every fallback
                    // tick instead of chasing a callback the compositor is never going to send. See *Frame clock* in
                    // docs/cpp-toolchain.md.
                    const react_native_linux::FrameClock::Source frameSource =
                        window.hasFrameCallbackFired() ? react_native_linux::FrameClock::Source::Callback
                                                       : react_native_linux::FrameClock::Source::Timer;
                    const std::chrono::steady_clock::time_point frameTime = std::chrono::steady_clock::now();
                    const react_native_linux::FrameClock::Tick tick = session->recordFrameTick(frameSource, frameTime);

                    // A pending capture is work in its own right: --screenshot and the automation channel's
                    // TakeScreenshot both read back a presented frame, and a static scene under a compositor that
                    // withholds frame callbacks would otherwise never present one and never answer.
                    // A discarded content update is the third reason to draw regardless of the clock: it never turned
                    // into light and is owed no frame callback, so nothing else would ever wake this window to
                    // replace it. See *Surface commit ordering* in docs/cpp-toolchain.md.
                    if (tick.shouldDraw || renderer.hasPendingCapture() || window.hasContentUpdateDiscarded()) {
                        // The animation backend is driven by the same instant the frame clock measured, and before
                        // the scene is taken, so a mutation this frame produces is in the snapshot it paints. A
                        // running animation is also pending work, so a fallback timeout keeps drawing it when the
                        // compositor withholds callbacks. See *Animation choreographer* in docs/cpp-toolchain.md.
                        session->tickAnimations(frameTime);

                        // The scene and the damage that describes it have to come out of the mounting manager
                        // together, under one lock: a transaction landing between them would leave damage this scene
                        // cannot satisfy.
                        const react_native_linux::SceneFrame frame = session->takeFrame();

                        // A chrome change — activation, a resize, the mode itself — repaints everything rather than
                        // being expressed as another damage rectangle: it happens once per user gesture, and an
                        // empty damage list is already this renderer's word for a full repaint.
                        react_native_linux::SceneDamage surfaceDamage;

                        if (!chrome.isRepaintNeeded) {
                            surfaceDamage = frame.damage;

                            for (facebook::react::Rect& rectangle : surfaceDamage) {
                                rectangle.origin.y += chrome.content.topOffset;
                            }
                        }

                        // The paint span the frame journal (#345) times is exactly the Skia work between them: not
                        // `drawFrame`'s swapchain bookkeeping, and not the present call after it, because neither is
                        // the work `paintScene` is answering the frame's damage with.
                        presented = renderer.drawFrame(
                            window, surfaceDamage,
                            [&frame, &chrome, &window, &session](SkCanvas& canvas,
                                                                 react_native_linux::WindowSize /*size*/,
                                                                 const react_native_linux::SceneDamage& imageDamage) {
                                session->recordPaintStart(std::chrono::steady_clock::now());
                                paintDecoratedFrame(canvas, chrome, window.title(), imageDamage,
                                                    [&frame](SkCanvas& contentCanvas,
                                                             const react_native_linux::SceneDamage& contentDamage) {
                                                        react_native_linux::paintScene(contentCanvas, frame.scene,
                                                                                       contentDamage);
                                                    });
                                session->recordPaintEnd(std::chrono::steady_clock::now());
                            });
                    }
                } else {
                    presented = renderer.drawFrame(window, {}, drawPlaceholder);
                }

                if (parsedArguments.windowDebug && broughtUp.vulkanRenderer != nullptr) {
                    printSurfaceCommitOutcome(*broughtUp.vulkanRenderer, presented);
                }

                announceFirstPresentedFrameOnce(window, presented, firstPresentedFrameAnnounced, ladderPath,
                                                broughtUp.record);

                if (presented) {
                    ++presentedFrames;
                    chrome.isRepaintNeeded = false;
                }
                hasCaptured = isCaptureFrame && !renderer.hasPendingCapture();

                if (!hasCaptured && !window.waitForRedraw(kFrameCallbackFallback)) {
                    break;
                }
            }

            if (frameLog.has_value()) {
                react_native_linux::WindowSession* sessionPointer = session.has_value() ? &session.value() : nullptr;

                writeFrameLines(frameLog.value(), window, sessionPointer);
                frameLog.value() << react_native_linux::FrameTiming::formatSummaryLine(window.frameTimingSummary(),
                                                                                       window.isPresentationSupported())
                                 << "\n";

                if (sessionPointer != nullptr) {
                    frameLog.value() << react_native_linux::FrameJournal::formatSummaryLine(
                                            sessionPointer->frameJournalSummary())
                                     << "\n";
                }
            }

            if (parsedArguments.screenshotPath.has_value() && !hasCaptured) {
                react_native_linux::reportNativeError(
                    kWindowErrorSource, "the window closed before frame " + std::to_string(parsedArguments.frameCount) +
                                            " could be captured");

                return 1;
            }

            return session.has_value() && session->hasReportedFatalError() ? 1 : 0;
        } catch (const std::exception&) {
            window.reportPendingDisplayError();

            throw;
        }
    } catch (const std::exception& error) {
        react_native_linux::reportNativeError(kWindowErrorSource, error.what());

        return 1;
    }
}
