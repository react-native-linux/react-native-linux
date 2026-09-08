#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace react_native_linux {

/**
 * The bus-free half of single-instance activation (#363): deriving the well-known D-Bus name two processes race
 * for, encoding the handoff payload, and reading a URL back out of it. `SingleInstanceCoordinator` is the sd-bus
 * client that calls these; nothing here touches a bus, which is what makes it the part the coverage gate scores
 * and the unit tests exercise directly rather than through a live session bus.
 *
 * The handoff carries `argv` and `cwd` as D-Bus byte arrays (`ay`), never as `s`/`as`: a D-Bus `STRING` must be
 * valid UTF-8 and an `argv` element or a working directory on a real filesystem is not guaranteed to be — POSIX
 * paths are arbitrary non-NUL byte sequences (PR #387's review). `std::string` here is always a byte buffer, not
 * text: nothing in this file validates or transcodes it.
 */
using ActivationByteArgument = std::vector<uint8_t>;

/**
 * The two things a second launch hands the first: its own `argv` (starting from `argv[0]`) and the working
 * directory it was started in, because the second process's relative paths mean nothing to the first
 * (tauri#14151).
 */
struct ActivationRequest {
    std::vector<ActivationByteArgument> argv;
    ActivationByteArgument cwd;
};

/**
 * The well-known D-Bus name two instances of the same application race for with `sd_bus_request_name`, flags
 * `0` — no queueing, no replacing — so whichever process's request wins the race is unambiguously the primary
 * instance and the name itself is the arbitration: there is no separate lock file that can go stale after a
 * crash, because a crashed process's bus connection is already gone and the name is released with it.
 *
 * `applicationId` is folded into this platform's own reverse-DNS namespace rather than trusted as a bus name on
 * its own: a bus name element cannot start with a digit and may contain only `[A-Za-z0-9_-]`, and an application
 * identifier is not guaranteed to already satisfy that. Any character outside that set folds to `_`, which is an
 * accepted collision risk between two application identifiers that differ only in punctuation, not a rejection.
 */
std::string singleInstanceBusName(std::string_view applicationId);

/**
 * Copies the bytes of `text` into a byte argument. `text` is never interpreted as UTF-8 here — this is the
 * identity transform a D-Bus `ay` marshals directly, char for char, whatever the bytes are.
 */
ActivationByteArgument toByteArgument(std::string_view text);

/** The inverse of `toByteArgument`: the bytes back into a `std::string` byte buffer, still uninterpreted. */
std::string fromByteArgument(const ActivationByteArgument& bytes);

/** Encodes a process's own `argv` and working directory into the wire shape the D-Bus call carries. */
ActivationRequest buildActivationRequest(const std::vector<std::string>& argv, std::string_view workingDirectory);

/**
 * The first `argv` element (skipping `argv[0]`, the executable path) that looks like a URI — an ASCII letter
 * followed by letters, digits, `+`, `.` or `-`, then a `:` — decoded as the activation URL `Linking` surfaces
 * through `getInitialURL()` on a cold start and a `url` event on a warm one. Nothing else in `argv` is inspected:
 * a bundle path or a `--flag` never matches the scheme grammar, so this cannot mistake one for a URL.
 */
std::optional<std::string> extractActivationUrl(const std::vector<ActivationByteArgument>& argv);

} // namespace react_native_linux
