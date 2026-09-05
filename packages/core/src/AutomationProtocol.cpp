#include "AutomationProtocol.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <folly/json/json.h>
#include <iostream>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

struct CommandName {
    std::string_view name;
    AutomationCommand command;
};

// Indexed by the enum's value, so describeAutomationCommand is total over the enum without a switch whose
// implicit no-match branch could never be covered.
constexpr std::array<CommandName, 5> kCommandNames{{
    {.name = "DumpVisualTree", .command = AutomationCommand::DumpVisualTree},
    {.name = "HangForTesting", .command = AutomationCommand::HangForTesting},
    {.name = "ListErrors", .command = AutomationCommand::ListErrors},
    {.name = "MarkTestPassed", .command = AutomationCommand::MarkTestPassed},
    {.name = "TakeScreenshot", .command = AutomationCommand::TakeScreenshot},
}};

std::string_view describeAutomationCommand(AutomationCommand command) {
    return kCommandNames.at(static_cast<size_t>(command)).name;
}

std::optional<AutomationCommand> findAutomationCommand(std::string_view name) {
    const auto match = std::ranges::find(kCommandNames, name, &CommandName::name);

    if (match == kCommandNames.end()) {
        return std::nullopt;
    }

    return match->command;
}

AutomationRequestParse rejectRequest(std::string reason) {
    return AutomationRequestParse{.request = std::nullopt, .error = std::move(reason)};
}

AutomationRequestParse readScreenshotPath(const folly::dynamic& request) {
    const folly::dynamic* path = request.get_ptr("path");

    if (path == nullptr || !path->isString() || path->asString().empty()) {
        return rejectRequest("TakeScreenshot needs a non-empty \"path\"");
    }

    return AutomationRequestParse{.request = AutomationRequest{.command = AutomationCommand::TakeScreenshot,
                                                               .screenshotPath = path->asString(),
                                                               .hangMilliseconds = 0},
                                  .error = {}};
}

AutomationRequestParse readHangMilliseconds(const folly::dynamic& request) {
    const folly::dynamic* milliseconds = request.get_ptr("milliseconds");

    if (milliseconds == nullptr || !milliseconds->isInt() || milliseconds->asInt() < 0) {
        return rejectRequest("HangForTesting needs a non-negative integer \"milliseconds\"");
    }

    return AutomationRequestParse{.request = AutomationRequest{.command = AutomationCommand::HangForTesting,
                                                               .screenshotPath = {},
                                                               .hangMilliseconds = milliseconds->asInt()},
                                  .error = {}};
}

void appendOptionalString(folly::dynamic& node, const char* key, const std::string& value) {
    if (!value.empty()) {
        node[key] = value;
    }
}

folly::dynamic describeFrame(const facebook::react::Rect& frame) {
    return folly::dynamic::object("x", static_cast<double>(frame.origin.x))("y", static_cast<double>(frame.origin.y))(
        "width", static_cast<double>(frame.size.width))("height", static_cast<double>(frame.size.height));
}

folly::dynamic describeNode(const SceneNodes& nodes, facebook::react::Tag tag) {
    const auto entry = nodes.find(tag);

    if (entry == nodes.end()) {
        return folly::dynamic::object("tag", tag)("missing", true);
    }

    const SceneNode& node = entry->second;
    folly::dynamic described = folly::dynamic::object("tag", node.tag)("componentName", node.componentName)(
        "frame", describeFrame(node.layoutMetrics.frame));

    appendOptionalString(described, "testID", node.testId);
    appendOptionalString(described, "accessibilityLabel", node.accessibilityLabel);

    if (node.text.has_value()) {
        appendOptionalString(described, "text", node.text.value().attributedString.getString());
    }

    folly::dynamic children = folly::dynamic::array;

    for (facebook::react::Tag childTag : node.childTags) {
        children.push_back(describeNode(nodes, childTag));
    }

    if (!children.empty()) {
        described["children"] = std::move(children);
    }

    return described;
}

std::vector<facebook::react::Tag> sortedRootTags(const SceneNodes& nodes) {
    std::vector<facebook::react::Tag> rootTags;

    for (const auto& [tag, node] : nodes) {
        if (node.parentTag == 0) {
            rootTags.push_back(tag);
        }
    }

    std::ranges::sort(rootTags);

    return rootTags;
}

} // namespace

AutomationRequestParse parseAutomationRequest(const std::string& line) {
    folly::dynamic parsed;

    try {
        parsed = folly::parseJson(line);
    } catch (const std::exception& error) {
        return rejectRequest(std::string("a request has to be one JSON object per line: ") + error.what());
    }

    if (!parsed.isObject()) {
        return rejectRequest("a request has to be a JSON object");
    }

    const folly::dynamic* name = parsed.get_ptr("command");

    if (name == nullptr || !name->isString()) {
        return rejectRequest("a request needs a \"command\" string");
    }

    const std::optional<AutomationCommand> command = findAutomationCommand(name->asString());

    if (!command.has_value()) {
        return rejectRequest("unknown command " + name->asString());
    }

    if (command.value() == AutomationCommand::TakeScreenshot) {
        return readScreenshotPath(parsed);
    }

    if (command.value() == AutomationCommand::HangForTesting) {
        return readHangMilliseconds(parsed);
    }

    return AutomationRequestParse{
        .request = AutomationRequest{.command = command.value(), .screenshotPath = {}, .hangMilliseconds = 0},
        .error = {}};
}

std::string formatAutomationResponse(AutomationCommand command, const folly::dynamic& result) {
    return folly::toJson(
               folly::dynamic::object("ok", true)("command", describeAutomationCommand(command))("result", result)) +
           "\n";
}

std::string formatAutomationFailure(const std::string& reason) {
    return folly::toJson(folly::dynamic::object("ok", false)("error", reason)) + "\n";
}

folly::dynamic describeErrors(const std::vector<AutomationError>& errors) {
    folly::dynamic described = folly::dynamic::array;

    for (const AutomationError& error : errors) {
        described.push_back(folly::dynamic::object("source", error.source)("message", error.message));
    }

    return folly::dynamic::object("errors", std::move(described));
}

folly::dynamic describeVisualTree(const SceneNodes& nodes) {
    folly::dynamic roots = folly::dynamic::array;

    for (facebook::react::Tag tag : sortedRootTags(nodes)) {
        roots.push_back(describeNode(nodes, tag));
    }

    return folly::dynamic::object("roots", std::move(roots));
}

void AutomationErrorLog::record(std::string source, std::string message) {
    const std::lock_guard<std::mutex> guard(mutex_);

    errors_.push_back(AutomationError{.source = std::move(source), .message = std::move(message)});
}

std::vector<AutomationError> AutomationErrorLog::list() const {
    const std::lock_guard<std::mutex> guard(mutex_);

    return errors_;
}

AutomationErrorLog& automationErrorLog() {
    static AutomationErrorLog log;

    return log;
}

void reportNativeError(std::string_view source, const std::string& message) {
    automationErrorLog().record(std::string(source), message);

    std::cerr << '[' << source << "] " << message << std::endl;
}

} // namespace react_native_linux
