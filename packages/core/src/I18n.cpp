#include "I18n.h"

#include "AsyncStorage.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

constexpr std::string_view kTrue = "true";
constexpr std::string_view kFalse = "false";
constexpr std::string_view kAllowRightToLeftKey = "allowRTL";
constexpr std::string_view kForceRightToLeftKey = "forceRTL";
constexpr std::string_view kSwapLeftAndRightKey = "swapLeftAndRightInRTL";

} // namespace

std::string localeFromEnvironment() {
    for (const char* variable : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const char* value = std::getenv(variable);

        if (value != nullptr && *value != '\0') {
            const std::string_view locale(value);

            return std::string(locale.substr(0, locale.find_first_of(".@")));
        }
    }

    return {};
}

bool isRightToLeftLanguage(std::string_view localeIdentifier) noexcept {
    constexpr std::array<std::string_view, 11> kRightToLeftLanguages{"ar", "he", "iw", "fa", "ur", "ps",
                                                                     "sd", "ug", "yi", "dv", "ckb"};
    const std::string_view language = localeIdentifier.substr(0, localeIdentifier.find_first_of("_-.@"));

    return std::ranges::find(kRightToLeftLanguages, language) != kRightToLeftLanguages.end();
}

I18nModel::I18nModel(std::string localeIdentifier, std::shared_ptr<KeyValueStore> store)
    : localeIdentifier_(std::move(localeIdentifier)), store_(std::move(store)) {}

void I18nModel::restore() {
    const std::array<bool Choices::*, 3> storedChoices{&Choices::allowRightToLeft, &Choices::forceRightToLeft,
                                                       &Choices::swapLeftAndRightInRightToLeft};
    const std::vector<KeyValueStore::Entry> entries =
        store_->get(std::string(kDatabaseName), {std::string(kAllowRightToLeftKey), std::string(kForceRightToLeftKey),
                                                 std::string(kSwapLeftAndRightKey)});
    const std::scoped_lock lock(mutex_);

    for (std::size_t index = 0; index < storedChoices.size(); ++index) {
        if (entries.at(index).second.has_value()) {
            choices_.*storedChoices.at(index) = entries.at(index).second.value() == kTrue;
        }
    }

    hasPendingChange_ = true;
}

const std::string& I18nModel::localeIdentifier() const noexcept { return localeIdentifier_; }

bool I18nModel::isRightToLeft() const {
    const std::scoped_lock lock(mutex_);

    return isRightToLeftLocked();
}

bool I18nModel::doesSwapLeftAndRightInRightToLeft() const {
    const std::scoped_lock lock(mutex_);

    return choices_.swapLeftAndRightInRightToLeft;
}

void I18nModel::allowRightToLeft(bool isAllowed) {
    choose(&Choices::allowRightToLeft, kAllowRightToLeftKey, isAllowed);
}

void I18nModel::forceRightToLeft(bool isForced) { choose(&Choices::forceRightToLeft, kForceRightToLeftKey, isForced); }

void I18nModel::swapLeftAndRightInRightToLeft(bool isSwapped) {
    choose(&Choices::swapLeftAndRightInRightToLeft, kSwapLeftAndRightKey, isSwapped);
}

std::optional<LayoutDirectionRequest> I18nModel::takeLayoutDirectionChange() {
    const std::scoped_lock lock(mutex_);

    if (!std::exchange(hasPendingChange_, false)) {
        return std::nullopt;
    }

    const bool isRightToLeft = isRightToLeftLocked();

    return LayoutDirectionRequest{.layoutDirection = isRightToLeft ? facebook::react::LayoutDirection::RightToLeft
                                                                   : facebook::react::LayoutDirection::LeftToRight,
                                  .swapLeftAndRightInRightToLeft =
                                      isRightToLeft && choices_.swapLeftAndRightInRightToLeft};
}

void I18nModel::choose(bool Choices::*choice, std::string_view key, bool value) {
    store_->set(std::string(kDatabaseName), {{std::string(key), std::string(value ? kTrue : kFalse)}});

    const std::scoped_lock lock(mutex_);

    choices_.*choice = value;
    hasPendingChange_ = true;
}

bool I18nModel::isRightToLeftLocked() const {
    return choices_.forceRightToLeft || (choices_.allowRightToLeft && isRightToLeftLanguage(localeIdentifier_));
}

} // namespace react_native_linux
