#pragma once

#include "RetainedScene.h"

#include <cstdint>
#include <folly/json/dynamic.h>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace react_native_linux {

/**
 * The five commands of the automation channel (#214), the out-of-process assertion surface react-native-windows
 * calls its automation channel. Each one answers a question about the running app that a screenshot cannot:
 * what the runtime reported as an error, what the committed tree actually is, and whether the bundle itself says
 * the test passed.
 *
 * The wire format is line-delimited JSON over a `SOCK_STREAM` Unix socket: one request object per line, one
 * response object per line, in order. It is not JSON-RPC — react-native-windows needs the id-and-batching half
 * of JSON-RPC because its channel is multiplexed over TCP with a JavaScript client library; ours is one driver
 * talking to one window over a socket only that pair can see, so a request is answered by the next line and
 * nothing else has to correlate them.
 *
 * Everything in this file is pure: it turns a line into a request and a result into a line, and knows nothing
 * about sockets, frames or threads. `AutomationServer` is the socket, and `rnl_window` is what decides what each
 * command does. That split is what puts the format under the coverage gate.
 */
enum class AutomationCommand : uint8_t {
    DumpAccessibilityTree = 0,
    DumpVisualTree = 1,
    HangForTesting = 2,
    ListAccessibilityChanges = 3,
    ListErrors = 4,
    MarkTestPassed = 5,
    TakeScreenshot = 6,
};

/**
 * A parsed request. `screenshotPath` carries `TakeScreenshot`'s `"path"` and `hangMilliseconds` carries
 * `HangForTesting`'s `"milliseconds"`; a command that takes no argument leaves both at their defaults.
 */
struct AutomationRequest {
    AutomationCommand command{};
    std::string screenshotPath;
    int64_t hangMilliseconds{0};
};

/** Exactly one of the two is set: a request, or the reason the line is not one. */
struct AutomationRequestParse {
    std::optional<AutomationRequest> request;
    std::string error;
};

/**
 * One committed change to a node's `accessibilityState` or `accessibilityValue`, carried on an `Update` mutation
 * rather than a remount — same tag, no `Remove`/`Insert` around it. `LinuxMountingManager::executeMount` records
 * one of these per changed tag per commit by diffing the mutation's old and new props, and `takeAccessibilityChanges`
 * drains them for `ListAccessibilityChanges` below.
 *
 * This is the smallest observable of "an AT-SPI `object:state-changed:<state>` or `object:property-change` event
 * would fire here": the bridge that would actually put an event on the bus is #27's and does not exist yet, so
 * this is graded on the projection and the channel, not on D-Bus traffic. See #264.
 */
struct AccessibilityChange {
    facebook::react::Tag tag{};
    bool stateChanged{false};
    bool valueChanged{false};

    /**
     * The mutation's own `testID`, captured at record time rather than looked up later from the visual tree:
     * `ListAccessibilityChanges` takes its tree snapshot after `takeAccessibilityChanges` drains the queue, so a
     * commit between the two (in particular a removal) would otherwise leave `describeAccessibilityChanges`
     * resolving a stale or missing testID for a tag that no longer names the node it changed.
     */
    std::string testId;
};

/**
 * One thing the runtime reported as an error. `source` is `"javascript"` for an uncaught JavaScript error and
 * the bracketed component name — `image`, `text`, `window` — for a native fault, which is the same set of
 * failures #233's gate greps out of the merged trace. Reporting them structured is what lets the gate stop
 * grepping: a scenario asserts the list is empty rather than that no line matched a prefix.
 */
struct AutomationError {
    std::string source;
    std::string message;
};

/**
 * The most a request may buffer before the channel gives up on the client that sent it. A request is one line of
 * JSON naming one of five commands and at most a path, so the real ones are a few dozen bytes; this is the
 * bound that stops a client which never sends a newline from growing the buffer for as long as the window runs.
 */
inline constexpr size_t kMaxRequestBytes = 64 * 1024;

/**
 * The line-delimited framing, without the socket underneath it: bytes in, whole requests out.
 *
 * The cap is on unconsumed bytes rather than on a line, which is the same thing here — the driver sends one
 * request per connection and waits for its answer, so nothing is ever pending behind a complete line.
 */
class AutomationLineBuffer final {
public:
    /**
     * Adds received bytes. False when they would take the buffer past `kMaxRequestBytes`, which means the
     * sender is not speaking this protocol; the buffer is emptied and its owner is expected to drop the client.
     */
    bool append(std::string_view bytes);

    /** The next complete line, without its terminator, and nothing when none has arrived yet. */
    std::optional<std::string> takeLine();
    void clear();

private:
    std::string pending_;
};

AutomationRequestParse parseAutomationRequest(const std::string& line);

/** `{"ok":true,"command":"...","result":{...}}` and a newline. */
std::string formatAutomationResponse(AutomationCommand command, const folly::dynamic& result);

/** `{"ok":false,"error":"..."}` and a newline. */
std::string formatAutomationFailure(const std::string& reason);

folly::dynamic describeErrors(const std::vector<AutomationError>& errors);

/**
 * `{"roots":[node,...]}`, where a node carries its tag, component name, absolute-in-parent frame, and the props
 * a react-native-windows tree snapshot asserts on: `testID`, `accessibilityLabel` and the text a `<Text>` laid
 * out. A prop the node does not set is omitted rather than emitted empty, so a snapshot names only what the
 * scenario is about. Children are the mount order the scene holds, which is already `zIndex` order.
 */
folly::dynamic describeVisualTree(const SceneNodes& nodes);

/**
 * `{"nodes":[node,...]}`: the accessibility projection of the same committed tree, in reading order, which is the
 * assertion react-native-windows makes 21 times over and a pixel diff cannot make once — a role, name or state
 * regression changes no pixel a perceptual threshold would catch.
 *
 * The projection is what an accessibility client would be handed, not what the scene holds, so it is smaller than
 * the scene and shaped differently:
 *
 * - **Pruned** — `accessibilityElementsHidden` and `importantForAccessibility: "no-hide-descendants"` remove the
 *   node *and its subtree*; React Native documents the two as the same thing on their respective platforms.
 * - **Skipped** — `importantForAccessibility: "no"` removes the node alone, and its exposed descendants take its
 *   place at its position in reading order. So does a node that is merely a box: one that is not `accessible`,
 *   carries no label and resolves to no role. This is the flattening every accessibility API does, and it is why
 *   the projection is not just the visual tree with extra fields.
 * - **Exposed** — everything else, with `role` always present, and `name`, `hint`, `state`, `value`, `labelledBy`
 *   and `children` present only when they carry something.
 *
 * `role` is `accessibilityRole` as the bundle authored it, else upstream's own `toString` of the ARIA `role`
 * prop, else `"text"` for a `<Paragraph>` that laid out any text, else `"none"`. `name` is `accessibilityLabel`,
 * falling back to that same paragraph text. `labelledBy` resolves each `nativeID` in `accessibilityLabelledBy` to
 * the tag that carries it and drops the ones nothing carries, because a snapshot has to be diffable and a raw
 * name that resolves to nothing is not a relation.
 *
 * The full `accessibilityRole` enumeration and its AT-SPI counterpart are #61's, and the bridge that would carry
 * this over D-Bus is #27's. This is the surface both are graded against.
 */
folly::dynamic describeAccessibilityTree(const SceneNodes& nodes);

/**
 * `{"changes":[{"tag","testID","state","value"},...]}`, drained in commit order: `testID` is the one
 * `AccessibilityChange` captured from the mutation's own props when the change was recorded, not looked up from a
 * later tree snapshot, so a commit — including a removal — between `takeAccessibilityChanges` and this call never
 * turns a real testID stale or blank. `testID` is omitted when the change carried none, and `state`/`value` are
 * each omitted when that half of the change is `false` rather than written out negatively, matching
 * `describeAccessibilityState`'s carries-something convention.
 */
folly::dynamic describeAccessibilityChanges(const std::vector<AccessibilityChange>& changes);

/**
 * Where `ListErrors` reads from.
 *
 * Threading contract: `record` is called from whichever thread hit the fault — the JavaScript thread for an
 * uncaught error, an image decode thread for a decode failure, the frame thread for a window fault — and `list`
 * from the frame thread while the automation channel is being served. The mutex is the whole guarantee.
 */
class AutomationErrorLog final {
public:
    void record(std::string source, std::string message);
    std::vector<AutomationError> list() const;

private:
    mutable std::mutex mutex_;
    std::vector<AutomationError> errors_;
};

/**
 * The one log per process. It is a global because the fault sites that write into it — the image pipeline, the
 * text pipeline, the JavaScript error handler — are reached through upstream interfaces that carry no place to
 * hang a per-window object, and there is exactly one window per process.
 */
AutomationErrorLog& automationErrorLog();

/**
 * Records a native fault and prints it as `[source] message` on standard error, which is the line the trace has
 * always carried. Both halves, so adopting the channel never costs a diagnostic a developer already reads.
 */
void reportNativeError(std::string_view source, const std::string& message);

} // namespace react_native_linux
