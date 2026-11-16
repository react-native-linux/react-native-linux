#pragma once

#include "ToplevelState.h"

#include <functional>
#include <mutex>
#include <string_view>

namespace react_native_linux {

/** The `AppStateStatus` values a desktop window can be in; `extension` and `unknown` have no Linux meaning. */
enum class AppStatus { Active, Inactive, Background };

/**
 * What `AppState` reports for one window: its `appStateDidChange` status and its `appStateFocusChange` focus.
 *
 * The mapping is react-native-macos's (`RCTAppState.mm`), the one desktop platform that reports more than
 * `active`: a window that is open but not the one the user is typing into is `inactive`, as `NSApplication`'s
 * resign-active is, and a window the compositor says nobody can see — `xdg_toplevel`'s `suspended` state, the
 * Wayland counterpart of a hidden application — is `background`. Focus is the `activated` bit alone, so a suspended
 * window that is still activated stays focused.
 */
struct AppStateSnapshot {
    AppStatus status{AppStatus::Active};
    bool isFocused{true};

    bool operator==(const AppStateSnapshot&) const = default;
};

AppStateSnapshot appStateOf(const ToplevelState& state) noexcept;

/** The `AppStateStatus` string JavaScript receives. */
std::string_view nameOfAppStatus(AppStatus status) noexcept;

/**
 * The state behind `AppState` (#765): the window's activation, as `xdg_toplevel.configure` last reported it.
 *
 * It starts `active` and focused rather than at the all-false `ToplevelState` a window has before its first
 * configure, because a process with no window at all — the headless bundle runner, every Hermes test — is the
 * foreground application as far as its JavaScript can tell, and because react-native-windows answers `active`
 * unconditionally for the same reason.
 *
 * Threading contract: one writer and one reader, which is `ActivationModel`'s pairing. `onToplevelStateChanged` is
 * called on the platform frame thread, once per frame by `WindowSession::deliverToplevelState`; `current` is read on
 * the JavaScript thread by `getConstants` and `getCurrentAppState`. One mutex guards the snapshot, and the change
 * listener runs after it is released, still on the frame thread. The listener only queues device events on the
 * module's `CallInvoker`, which is the hop onto the JavaScript thread; one writer is what keeps those events in
 * the order the window changed state.
 */
class AppStateModel {
public:
    using ChangeListener = std::function<void(const AppStateSnapshot& previous, const AppStateSnapshot& next)>;

    AppStateSnapshot current() const;

    void onToplevelStateChanged(const ToplevelState& state);

    void setChangeListener(ChangeListener listener);

private:
    mutable std::mutex mutex_;
    AppStateSnapshot current_;
    ChangeListener changeListener_;
};

} // namespace react_native_linux
