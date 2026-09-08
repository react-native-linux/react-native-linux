#pragma once

#include <functional>
#include <mutex>
#include <optional>
#include <string>

namespace react_native_linux {

/**
 * The state behind `Linking.getInitialURL()` and the `url` device event (#363): whatever URL last reached this
 * process, whether that was this process's own launch `argv` (seeded by `WindowSession` before the bundle loads,
 * the same way `seedColorScheme` seeds `AppearanceModel`) or a later instance forwarding its `argv` over the
 * single-instance D-Bus name and exiting.
 *
 * There is exactly one writer — `onActivationUrlReceived`, called from the platform frame thread, either once at
 * startup or every time `SingleInstanceCoordinator` decodes an incoming activation — and one JavaScript-thread
 * reader, `LinuxLinkingModule::getInitialURL`. That pairing is `AppearanceModel`'s, minus the second writer: there
 * is no JavaScript-thread mutator here, so one mutex held for the duration of each accessor is enough, with the
 * listener invoked after it is released so a listener calling back in cannot deadlock on it.
 *
 * The seed call and a later warm call are the same method: emitting a `url` event before the bundle has loaded
 * any listeners is a no-op observed by nobody, exactly as an `appearanceChanged` emitted from `seedColorScheme`
 * is.
 */
class ActivationModel {
public:
    std::optional<std::string> currentUrl() const;

    void onActivationUrlReceived(const std::string& url);

    void setChangeListener(std::function<void(const std::string&)> listener);

private:
    mutable std::mutex mutex_;
    std::optional<std::string> currentUrl_;
    std::function<void(const std::string&)> changeListener_;
};

} // namespace react_native_linux
