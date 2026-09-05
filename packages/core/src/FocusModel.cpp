#include "FocusModel.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

constexpr char kTabKey[] = "Tab";
constexpr char kEnterKey[] = "Enter";
constexpr char kSpaceKey[] = " ";
constexpr char kTextInputComponentName[] = "TextInput";
constexpr char kLinkRole[] = "link";
constexpr facebook::react::Tag kNoTag = 0;

} // namespace

FocusTransition FocusModel::setFocusableTags(std::vector<facebook::react::Tag> focusableTags) {
    focusableTags_ = std::move(focusableTags);

    if (focusedTag_ == kNoTag || isFocusable(focusedTag_)) {
        return {};
    }

    return transitionTo(kNoTag, false);
}

FocusTransition FocusModel::move(FocusDirection direction) {
    if (focusableTags_.empty()) {
        return {};
    }

    const size_t count = focusableTags_.size();
    const auto current = std::find(focusableTags_.begin(), focusableTags_.end(), focusedTag_);

    if (current == focusableTags_.end()) {
        return transitionTo(direction == FocusDirection::Forward ? focusableTags_.front() : focusableTags_.back(),
                            true);
    }

    const size_t index = static_cast<size_t>(current - focusableTags_.begin());
    const size_t next = direction == FocusDirection::Forward ? (index + 1) % count : (index + count - 1) % count;

    return transitionTo(focusableTags_[next], true);
}

FocusTransition FocusModel::focusTag(facebook::react::Tag tag, FocusOrigin origin) {
    if (!isFocusable(tag)) {
        return transitionTo(kNoTag, false);
    }

    return transitionTo(tag, origin == FocusOrigin::Keyboard);
}

facebook::react::Tag FocusModel::focusedTag() const noexcept { return focusedTag_; }

bool FocusModel::isFocusVisible() const noexcept { return isFocusVisible_; }

bool FocusModel::isFocusable(facebook::react::Tag tag) const {
    return std::find(focusableTags_.begin(), focusableTags_.end(), tag) != focusableTags_.end();
}

FocusTransition FocusModel::transitionTo(facebook::react::Tag tag, bool isFocusVisible) {
    if (tag == focusedTag_ && isFocusVisible == isFocusVisible_) {
        return {};
    }

    FocusTransition transition;

    if (tag != focusedTag_) {
        transition.blurredTag = focusedTag_;
        transition.focusedTag = tag;
    }

    transition.hasChanged = true;
    focusedTag_ = tag;
    isFocusVisible_ = isFocusVisible;

    return transition;
}

std::optional<FocusDirection> focusDirectionForKey(const std::string& key, bool isShiftDown) {
    if (key != kTabKey) {
        return std::nullopt;
    }

    return isShiftDown ? FocusDirection::Backward : FocusDirection::Forward;
}

bool isActivationKey(const std::string& role, const std::string& key) {
    if (key != kEnterKey && key != kSpaceKey) {
        return false;
    }

    return role != kLinkRole || key == kEnterKey;
}

bool isTextInputComponent(const std::string& componentName) { return componentName == kTextInputComponentName; }

std::optional<double> computeScrollIntoViewOffset(double viewportOffset, double viewportExtent,
                                                  double targetOffset, double targetExtent) {
    if (targetOffset < viewportOffset) {
        return targetOffset;
    }

    const double targetEnd = targetOffset + targetExtent;
    const double viewportEnd = viewportOffset + viewportExtent;

    if (targetEnd > viewportEnd) {
        return targetEnd - viewportExtent;
    }

    return std::nullopt;
}

} // namespace react_native_linux
