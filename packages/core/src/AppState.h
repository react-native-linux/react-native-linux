#pragma once

#include "ToplevelState.h"

#include <functional>
#include <mutex>
#include <string_view>

namespace react_native_linux {

/**
 * The `AppStateStatus` values a desktop window reports. `inactive` is not one of them: on iOS it is a brief
 * transition, and apps written for iOS pause media and reconnect around it, which a desktop window losing focus on
 * every alt-tab must not trigger. `extension` and `unknown` have no Linux meaning.
 */
enum class AppStatus { Active, Background };

/**
 * What `AppState` reports for one window: its `appStateDidChange` status and its `appStateFocusChange` focus.
 *
 * The status is visibility only, as react-native-web's is: a window the compositor says nobody can see —
 * `xdg_toplevel`'s `suspended` state — is `background`, and any other window is `active`, focused or not. Focus is
 * the `activated` bit alone, which `AppState.js` reports as `focus` and `blur`.
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
 *
 * `seed` is the same write without the listener: `WindowSession` calls it with the window's state before the bundle
 * loads, so the first `getConstants` already answers for that window and nothing announces a change no script saw.
 */
class AppStateModel {
public:
    using ChangeListener = std::function<void(const AppStateSnapshot& previous, const AppStateSnapshot& next)>;

    AppStateSnapshot current() const;

    void seed(const ToplevelState& state);

    void onToplevelStateChanged(const ToplevelState& state);

    void setChangeListener(ChangeListener listener);

private:
    mutable std::mutex mutex_;
    AppStateSnapshot current_;
    ChangeListener changeListener_;
};

} // namespace react_native_linux
