#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include <react/renderer/core/LayoutPrimitives.h>

namespace react_native_linux {

class KeyValueStore;

/**
 * The locale messages are shown in, as POSIX resolves it — `LC_ALL`, then `LC_MESSAGES`, then `LANG`, the first
 * that is set and not empty — with the codeset and modifier dropped, so `ar_EG.UTF-8@latin` reads as `ar_EG`, the
 * shape Android's `Locale.toString()` gives `I18nManager.localeIdentifier`.
 */
std::string localeFromEnvironment();

/**
 * Whether the locale's language is written right to left, which is what React Native's Android `I18nUtil` and iOS
 * `RCTI18nUtil` ask of the system locale. The list is the languages whose default script ICU's likely subtags make
 * Arabic, Hebrew or Thaana: Arabic, Hebrew (and its legacy code `iw`), Persian, Urdu, Pashto, Sindhi, Uyghur,
 * Yiddish, Dhivehi and Central Kurdish. Plain `ku` is not in it, because ICU writes Kurmanji in Latin script.
 */
bool isRightToLeftLanguage(std::string_view localeIdentifier) noexcept;

/** What `FabricHost::setLayoutDirection` lays the surface out with. */
struct LayoutDirectionRequest {
    facebook::react::LayoutDirection layoutDirection{facebook::react::LayoutDirection::LeftToRight};
    bool swapLeftAndRightInRightToLeft{false};

    bool operator==(const LayoutDirectionRequest&) const = default;
};

/**
 * `I18nManager`'s state (#72): the locale, and the `allowRTL`, `forceRTL` and `swapLeftAndRightInRTL` choices an
 * application makes. The surface is right to left when `forceRTL` is on, or when `allowRTL` is on and the locale's
 * language is right to left; `left` and `right` swap to `start` and `end` only while it is, as on iOS.
 *
 * The choices survive a restart in `KeyValueStore`'s reserved database `kDatabaseName`, and reach the surface
 * without a reload: every choice marks a layout direction change, which the frame loop takes and applies. The
 * constants JavaScript read at startup do not change with it — `I18nManager.isRTL` stays what it was until the
 * next bundle load, exactly as upstream's does on Android and iOS.
 *
 * Threading contract: `restore` runs on the frame thread before the bundle does, which is `KeyValueStore`'s own
 * contract for its pre-bundle caller. The three choices and the getters run on the JavaScript thread, inside the
 * `I18nManager` module, and `takeLayoutDirectionChange` runs on the frame thread once per frame. `mutex_` guards
 * the choices and the pending change between those two threads; the store is only ever touched by one of them at
 * a time.
 */
class I18nModel final {
public:
    static constexpr std::string_view kDatabaseName = "@react-native-linux/I18nManager";

    I18nModel(std::string localeIdentifier, std::shared_ptr<KeyValueStore> store);

    void restore();

    const std::string& localeIdentifier() const noexcept;
    bool isRightToLeft() const;
    bool doesSwapLeftAndRightInRightToLeft() const;

    void allowRightToLeft(bool isAllowed);
    void forceRightToLeft(bool isForced);
    void swapLeftAndRightInRightToLeft(bool isSwapped);

    /** The layout the choices ask for, once after each change; the first call answers the startup layout. */
    std::optional<LayoutDirectionRequest> takeLayoutDirectionChange();

private:
    struct Choices {
        bool allowRightToLeft{true};
        bool forceRightToLeft{false};
        bool swapLeftAndRightInRightToLeft{true};
    };

    void choose(bool Choices::*choice, std::string_view key, bool value);
    bool isRightToLeftLocked() const;

    const std::string localeIdentifier_;
    const std::shared_ptr<KeyValueStore> store_;
    mutable std::mutex mutex_;
    Choices choices_;
    bool hasPendingChange_{true};
};

} // namespace react_native_linux
