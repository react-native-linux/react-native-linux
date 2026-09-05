#include "AutomationProtocol.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <folly/json/json.h>
#include <react/renderer/components/view/accessibilityPropsConversions.h>
#include <iostream>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <unordered_map>
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
constexpr std::array<CommandName, 6> kCommandNames{{
    {.name = "DumpAccessibilityTree", .command = AutomationCommand::DumpAccessibilityTree},
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

void appendIfNotEmpty(folly::dynamic& node, const char* key, folly::dynamic value) {
    if (!value.empty()) {
        node[key] = std::move(value);
    }
}

std::string paragraphText(const SceneNode& node) {
    return node.text.has_value() ? node.text.value().attributedString.getString() : std::string{};
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

    appendIfNotEmpty(described, "testID", node.testId);
    appendIfNotEmpty(described, "accessibilityLabel", node.accessibilityLabel);
    appendIfNotEmpty(described, "text", paragraphText(node));

    folly::dynamic children = folly::dynamic::array;

    for (facebook::react::Tag childTag : node.childTags) {
        children.push_back(describeNode(nodes, childTag));
    }

    if (!children.empty()) {
        described["children"] = std::move(children);
    }

    return described;
}

constexpr std::string_view kNoRole = "none";
constexpr std::string_view kParagraphRole = "text";

// Indexed by AccessibilityState::CheckedState, so the name of a tri-state is a lookup rather than a switch whose
// unreachable default could never be covered.
constexpr std::array<std::string_view, 4> kCheckedNames{"unchecked", "checked", "mixed", "none"};

using NativeIdTags = std::unordered_map<std::string, facebook::react::Tag>;

/**
 * Which tag each `nativeID` names, built in tag order so that a bundle that reused one `nativeID` resolves to the
 * same node on every run rather than to whichever one the scene's hash order happened to reach first.
 */
NativeIdTags collectNativeIdTags(const SceneNodes& nodes) {
    std::vector<facebook::react::Tag> tags;

    tags.reserve(nodes.size());

    for (const auto& [tag, node] : nodes) {
        tags.push_back(tag);
    }

    std::ranges::sort(tags);

    NativeIdTags tagsByNativeId;

    for (facebook::react::Tag tag : tags) {
        const std::string& nativeId = nodes.at(tag).accessibility.nativeId;

        if (!nativeId.empty()) {
            tagsByNativeId.emplace(nativeId, tag);
        }
    }

    return tagsByNativeId;
}

std::string accessibilityRoleOf(const SceneNode& node) {
    if (!node.accessibility.accessibilityRole.empty()) {
        return node.accessibility.accessibilityRole;
    }

    if (node.accessibility.role != facebook::react::Role::None) {
        return facebook::react::toString(node.accessibility.role);
    }

    return std::string(paragraphText(node).empty() ? kNoRole : kParagraphRole);
}

bool isAccessibilityPruned(const SceneNode& node) {
    return node.accessibility.elementsHidden ||
           node.accessibility.importantForAccessibility ==
               facebook::react::ImportantForAccessibility::NoHideDescendants;
}

bool isAccessibilityExposed(const SceneNode& node) {
    if (node.accessibility.importantForAccessibility == facebook::react::ImportantForAccessibility::No) {
        return false;
    }

    return node.accessibility.importantForAccessibility == facebook::react::ImportantForAccessibility::Yes ||
           node.accessibility.accessible || !node.accessibilityLabel.empty() || accessibilityRoleOf(node) != kNoRole;
}

folly::dynamic describeAccessibilityState(const std::optional<facebook::react::AccessibilityState>& state) {
    folly::dynamic described = folly::dynamic::object;

    if (!state.has_value()) {
        return described;
    }

    if (state.value().busy) {
        described["busy"] = true;
    }

    if (state.value().disabled) {
        described["disabled"] = true;
    }

    if (state.value().selected) {
        described["selected"] = true;
    }

    if (state.value().expanded.has_value()) {
        described["expanded"] = state.value().expanded.value();
    }

    if (state.value().checked != facebook::react::AccessibilityState::CheckedState::None) {
        described["checked"] = std::string(kCheckedNames.at(static_cast<size_t>(state.value().checked)));
    }

    return described;
}

folly::dynamic describeAccessibilityValue(const facebook::react::AccessibilityValue& value) {
    folly::dynamic described = folly::dynamic::object;

    if (value.min.has_value()) {
        described["min"] = value.min.value();
    }

    if (value.max.has_value()) {
        described["max"] = value.max.value();
    }

    if (value.now.has_value()) {
        described["now"] = value.now.value();
    }

    if (value.text.has_value()) {
        described["text"] = value.text.value();
    }

    return described;
}

folly::dynamic describeLabelledBy(const NativeIdTags& tagsByNativeId, const std::vector<std::string>& nativeIds) {
    folly::dynamic labelledBy = folly::dynamic::array;

    for (const std::string& nativeId : nativeIds) {
        const auto entry = tagsByNativeId.find(nativeId);

        if (entry != tagsByNativeId.end()) {
            labelledBy.push_back(entry->second);
        }
    }

    return labelledBy;
}

folly::dynamic describeAccessibilityNode(const SceneNode& node, const NativeIdTags& tagsByNativeId,
                                         folly::dynamic children) {
    folly::dynamic described = folly::dynamic::object("tag", node.tag)("role", accessibilityRoleOf(node));

    appendIfNotEmpty(described, "name",
                     node.accessibilityLabel.empty() ? paragraphText(node) : node.accessibilityLabel);
    appendIfNotEmpty(described, "hint", node.accessibility.hint);
    appendIfNotEmpty(described, "state", describeAccessibilityState(node.accessibility.state));
    appendIfNotEmpty(described, "value", describeAccessibilityValue(node.accessibility.value));
    appendIfNotEmpty(described, "labelledBy", describeLabelledBy(tagsByNativeId, node.accessibility.labelledBy));
    appendIfNotEmpty(described, "children", std::move(children));

    return described;
}

void projectAccessibility(const SceneNodes& nodes, facebook::react::Tag tag, const NativeIdTags& tagsByNativeId,
                          folly::dynamic& into);

void projectAccessibilityChildren(const SceneNodes& nodes, const SceneNode& node, const NativeIdTags& tagsByNativeId,
                                  folly::dynamic& into) {
    for (facebook::react::Tag childTag : node.childTags) {
        projectAccessibility(nodes, childTag, tagsByNativeId, into);
    }
}

void projectAccessibility(const SceneNodes& nodes, facebook::react::Tag tag, const NativeIdTags& tagsByNativeId,
                          folly::dynamic& into) {
    const auto entry = nodes.find(tag);

    // A tag the scene no longer holds contributes nothing, rather than the visual tree's {"missing":true} marker:
    // an accessibility client would be handed no node for it, so the projection reports none.
    if (entry == nodes.end()) {
        return;
    }

    const SceneNode& node = entry->second;

    if (isAccessibilityPruned(node)) {
        return;
    }

    folly::dynamic children = folly::dynamic::array;

    projectAccessibilityChildren(nodes, node, tagsByNativeId, children);

    if (!isAccessibilityExposed(node)) {
        for (folly::dynamic& child : children) {
            into.push_back(std::move(child));
        }

        return;
    }

    into.push_back(describeAccessibilityNode(node, tagsByNativeId, std::move(children)));
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

folly::dynamic describeAccessibilityTree(const SceneNodes& nodes) {
    const NativeIdTags tagsByNativeId = collectNativeIdTags(nodes);
    folly::dynamic projected = folly::dynamic::array;

    for (facebook::react::Tag tag : sortedRootTags(nodes)) {
        projectAccessibility(nodes, tag, tagsByNativeId, projected);
    }

    return folly::dynamic::object("nodes", std::move(projected));
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
