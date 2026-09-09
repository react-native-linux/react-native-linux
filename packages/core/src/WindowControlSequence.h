#pragma once

#include "ToplevelState.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace react_native_linux {

/**
 * One `xdg_toplevel.configure` to replay: the extent the compositor would have named, and the state array it
 * would have carried, with the state already resolved to absolutes.
 *
 * A zero extent means the extent the window already has — the sequence has not named one yet, so a state-only
 * token changes the state and nothing else. The parser cannot resolve that itself: under a kiosk compositor the
 * window's real extent is the output's, decided long after the command line was read.
 */
struct WindowControlStep {
    uint32_t width{0};
    uint32_t height{0};
    ToplevelState state;

    bool operator==(const WindowControlStep&) const = default;
};

/** `steps` is empty whenever `error` is set; an accepted sequence always has at least one step. */
struct WindowControlSequence {
    std::vector<WindowControlStep> steps;
    std::string error;
};

/**
 * The grammar of `--inject-window-sequence` (#430), which the e2e driver uses to drive a resize, a window-state
 * transition and a resize drag under a compositor that offers none of the three. cage is a kiosk compositor: it
 * sizes its only window to the output and honours neither `set_maximized` nor `set_fullscreen`, so a scenario
 * that needs a second extent can only get one by replaying the configure by hand — the same reason
 * `--inject-key-sequence` replays an input method cage does not run.
 *
 * Tokens are brace-delimited and applied one per interval, each carrying the extent and the state bits that all
 * the tokens before it left behind, so a state token alone changes no size and a size token alone changes no
 * state:
 *
 *   `{800x600}`                       one configure at that extent
 *   `{Drag:800x600:1000x600:5}`       five configures interpolated from the first extent to the second
 *   `{Maximized}` `{Unmaximized}`     the matching state bit, set or cleared
 *   `{Fullscreen}` `{Unfullscreen}`
 *   `{Tiled}` `{Untiled}`             all four tiled edges at once, per `isEffectivelyTiled`'s rule
 *   `{Maximized:1280x800}`            any state token may carry the extent that state arrives with
 *
 * A drag carries `resizing` on every configure but its last, which is what a compositor does across an
 * interactive resize and what makes the end of the drag distinguishable from its middle.
 */
WindowControlSequence parseWindowControlSequence(std::string_view specification);

/** The trace line one applied step prints, tagged so it is informational rather than a fault (#430). */
std::string describeWindowControlStep(const WindowControlStep& step);

} // namespace react_native_linux
