#pragma once

#include <cstdint>
#include <string>

namespace react_native_linux {

/**
 * What a negative-or-not return from `wl_display_dispatch_pending`/`flush`/`read_events` means, decoded from the
 * plain integers `wl_display_get_error` already gives without touching `wl_display` itself — which is what keeps
 * this classifier in the unit-test coverage gate the same way `decodeToplevelStates` is (see `ToplevelState.h`).
 *
 * - `Continue` — the call did not fail. This is the "clean close" case too: `xdg_toplevel.close` sets the exit
 *   flag from inside a dispatch that itself succeeded, so a window closing because the user closed it is not an
 *   error at all and must not be reported as one.
 * - `Retry` — `EAGAIN`. Normal back-pressure on the display file descriptor, not a fault; the caller polls again
 *   rather than tearing the connection down.
 * - `ProtocolError` — `wl_display_get_error` returned `EPROTO`. The compositor rejected a request; the caller
 *   should also read `wl_display_get_protocol_error` for which object and code.
 * - `DisplayError` — any other errno. The connection is unusable (`EPIPE` from a dead compositor, for instance).
 */
enum class WaylandDispatchOutcome : uint8_t {
    Continue = 0,
    Retry = 1,
    ProtocolError = 2,
    DisplayError = 3,
};

/**
 * `dispatchResult` is the raw return value of `wl_display_dispatch_pending`, `wl_display_flush` or
 * `wl_display_read_events`. `displayErrno` is `wl_display_get_error(display)`, read only when `dispatchResult`
 * is negative — the caller must not call it otherwise, since libwayland leaves it meaningless after a success.
 * `callErrno` is the plain C `errno` captured immediately after the same call: `wl_display_flush` reports
 * back-pressure by returning `-1` and setting `errno` to `EAGAIN` without touching the display's own fatal-error
 * state, so `displayErrno` alone is `0` on that path and looks identical to success having already been read.
 * Either one reporting `EAGAIN` is enough to classify the result as `Retry`.
 */
WaylandDispatchOutcome classifyWaylandDispatchResult(int dispatchResult, int displayErrno, int callErrno) noexcept;

/** The detail `wl_display_get_protocol_error` reports: which object, on which interface, rejected for what. */
struct WaylandProtocolErrorDetail {
    std::string interfaceName;
    uint32_t objectId{0};
    uint32_t errorCode{0};
};

/**
 * `[rnl-window] wayland protocol error: <interface>#<id> code <n> (<errno text>)`, without the `[rnl-window] `
 * prefix — `reportNativeError` adds that from its `source` argument, so the two must not both carry it.
 * `errnoText` is `strerror(EPROTO)`, passed in rather than computed here so this function stays free of
 * `<cstring>`'s locale-dependent behaviour and stays a pure string formatter.
 */
std::string formatWaylandProtocolError(const WaylandProtocolErrorDetail& detail, const std::string& errnoText);

/** `wayland display error: <errno text>`, for every display failure that is not a protocol error. */
std::string formatWaylandDisplayError(const std::string& errnoText);

} // namespace react_native_linux
