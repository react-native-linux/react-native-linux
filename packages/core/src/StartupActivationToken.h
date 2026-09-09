#pragma once

#include <optional>
#include <string>

namespace react_native_linux {

/**
 * The startup activation token of #336: the credential a Wayland launcher puts in `XDG_ACTIVATION_TOKEN` in the
 * environment of the process it starts, which the process must hand back to the compositor — `xdg_activation_v1`
 * `set_token` + `activate` on its first toplevel — for the shell to complete its startup notification. A client
 * that never consumes it leaves GNOME showing a busy cursor for fifteen seconds (zed#59397) and the window
 * absent from the switcher (zed#33897).
 *
 * Three rules, each one of the acceptance bullets:
 *
 * - **Consumed exactly once.** The first toplevel takes the token; nothing later may re-activate it, because an
 *   activation is a one-shot credential and re-using it would re-raise a window the user has already moved on
 *   from.
 * - **Stripped from the environment.** The token must not be inherited by processes we spawn — Metro, the CLI's
 *   tooling, a `Linking.openURL` handler — each of which would otherwise claim an activation that is not theirs.
 *   The class holds the value; the caller strips the variable with `unsetenv` once `take` has moved it out.
 * - **A snapshot at construction.** A token appearing in the environment *after* this process has created its
 *   first window is another process's activation, not ours, and is never taken — the environment is read once,
 *   at construction, and the holder owns that answer from then on.
 *
 * Pure: every value comes in through the constructor, nothing here reads the environment or calls `unsetenv`,
 * and that is what puts the lifecycle under the 100% gate. `WindowMain` reads `XDG_ACTIVATION_TOKEN` once at
 * startup, constructs the holder, and strips the variable on the same line.
 *
 * Threading contract: constructed and consumed on the frame thread, before the window's first frame — the same
 * window in which the contract of every Wayland object in this process states.
 */
class StartupActivationToken final {
public:
    /** `envValue` is `XDG_ACTIVATION_TOKEN`'s value, or `std::nullopt` when the variable is unset. Empty is unset. */
    static StartupActivationToken fromEnvironment(const std::optional<std::string>& envValue);

    /** The token, exactly once: the first call takes it, every later one answers `std::nullopt`. */
    std::optional<std::string> take();

    /** Whether `take` has already been called — the after-first-window visibility a test or a trace wants. */
    [[nodiscard]] bool consumed() const noexcept;

private:
    explicit StartupActivationToken(std::optional<std::string> token);

    std::optional<std::string> token_;
};

/** The environment variable the xdg-activation specification names for the launcher-to-client handoff. */
inline constexpr const char* kStartupActivationTokenVariable = "XDG_ACTIVATION_TOKEN";

} // namespace react_native_linux
