#include "AppearancePortal.h"

#include <cstdint>
#include <string_view>

namespace react_native_linux {

namespace {

constexpr char kPortalDestination[] = "org.freedesktop.portal.Desktop";
constexpr char kPortalObjectPath[] = "/org/freedesktop/portal/desktop";
constexpr char kSettingsInterface[] = "org.freedesktop.portal.Settings";
constexpr char kReadOneMethod[] = "ReadOne";
constexpr char kSettingChangedSignal[] = "SettingChanged";
constexpr char kAppearanceNamespace[] = "org.freedesktop.appearance";
constexpr char kColorSchemeKey[] = "color-scheme";

std::optional<ColorScheme> readColorSchemeVariant(sd_bus_message* message) {
    uint32_t portalSettingValue = 0;

    if (sd_bus_message_read(message, "v", "u", &portalSettingValue) < 0) {
        return std::nullopt;
    }

    return colorSchemeFromPortalSetting(portalSettingValue);
}

} // namespace

void AppearancePortal::BusDeleter::operator()(sd_bus* bus) const noexcept { sd_bus_unref(bus); }

void AppearancePortal::SlotDeleter::operator()(sd_bus_slot* slot) const noexcept { sd_bus_slot_unref(slot); }

int AppearancePortal::onSettingChanged(sd_bus_message* message, void* userData, sd_bus_error* /*error*/) {
    const char* settingNamespace = nullptr;
    const char* settingKey = nullptr;

    if (sd_bus_message_read(message, "ss", &settingNamespace, &settingKey) < 0) {
        return 0;
    }

    if (std::string_view(settingNamespace) != kAppearanceNamespace || std::string_view(settingKey) != kColorSchemeKey) {
        return 0;
    }

    uint32_t portalSettingValue = 0;

    if (sd_bus_message_read(message, "v", "u", &portalSettingValue) < 0) {
        return 0;
    }

    static_cast<AppearancePortal*>(userData)->signalledColorScheme_ =
        resolvePortalSettingOrFallback(portalSettingValue);

    return 0;
}

AppearancePortal::AppearancePortal() {
    sd_bus* openedBus = nullptr;

    if (sd_bus_open_user(&openedBus) < 0) {
        return;
    }

    bus_.reset(openedBus);

    sd_bus_slot* matchSlot = nullptr;

    if (sd_bus_match_signal(bus_.get(), &matchSlot, kPortalDestination, kPortalObjectPath, kSettingsInterface,
                            kSettingChangedSignal, onSettingChanged, this) >= 0) {
        settingChangedSlot_.reset(matchSlot);
    }

    sd_bus_error callError = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;

    if (sd_bus_call_method(bus_.get(), kPortalDestination, kPortalObjectPath, kSettingsInterface, kReadOneMethod,
                           &callError, &reply, "ss", kAppearanceNamespace, kColorSchemeKey) >= 0) {
        initialColorScheme_ = readColorSchemeVariant(reply);
    }

    sd_bus_error_free(&callError);
    sd_bus_message_unref(reply);
}

std::optional<ColorScheme> AppearancePortal::initialColorScheme() const noexcept { return initialColorScheme_; }

void AppearancePortal::processPendingSignals(AppearanceModel& appearanceModel) {
    if (!bus_) {
        return;
    }

    while (sd_bus_process(bus_.get(), nullptr) > 0) {
    }

    if (signalledColorScheme_.has_value()) {
        appearanceModel.onPortalColorSchemeChanged(signalledColorScheme_.value());
        signalledColorScheme_.reset();
    }
}

} // namespace react_native_linux
