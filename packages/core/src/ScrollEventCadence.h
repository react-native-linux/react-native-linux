#pragma once

namespace react_native_linux {

/**
 * What one frame of scrolling looked like, as the only four facts the cadence depends on.
 *
 * `hasMoved` is whether the content offset changed in this frame, `isDragging` whether a finger is on the
 * touchpad as of the end of it, and `isMomentumRunning` whether the deceleration curve is still moving after it.
 * `frameMilliseconds` is how long the frame took, which is what the throttle is measured in.
 */
struct ScrollCadenceFrame {
    double frameMilliseconds{0.0};
    double throttleMilliseconds{0.0};
    bool hasMoved{false};
    bool isDragging{false};
    bool isMomentumRunning{false};
};

/**
 * Which of a `<ScrollView>`'s five events this frame emits, in the order they are named here.
 *
 * A frame can carry several: the frame a finger lifts in both ends the drag and starts the momentum, and the
 * frame a fling stops in reports its final offset and ends the momentum.
 */
struct ScrollCadenceEvents {
    bool beginDrag{false};
    bool scroll{false};
    bool endDrag{false};
    bool momentumBegin{false};
    bool momentumEnd{false};
};

/**
 * The `onScroll` cadence of one `<ScrollView>`: when each of the five scroll events is emitted, and nothing else.
 *
 * This is issue #45 as arithmetic. `VirtualizedList` windowing is written against a cadence rather than against a
 * picture — it assumes a drag is bracketed, that momentum is bracketed inside that, and that `scrollEventThrottle`
 * bounds how often `onScroll` arrives — so a renderer that gets the picture right and the cadence wrong makes
 * every list in the ecosystem look broken. react-native-macos#1119 is twenty-four comments of exactly that.
 *
 * Three rules, all of them `RCTScrollView`'s:
 *
 * - **The throttle is a minimum interval, not a sampling rate.** `scrollEventThrottle` is milliseconds between
 *   `onScroll` dispatches; frames that move inside the interval are folded into the next dispatch rather than
 *   dropped, so the offset a list sees is always the newest one. Zero means every frame that moved, which is what
 *   an unset prop resolves to and what `RCTScrollView` does with the same value.
 * - **A boundary always flushes.** The end of a drag and the end of momentum emit the movement they are the end
 *   of, whatever the throttle says, so the final offset is never the one nobody was told about.
 * - **Begin and end are paired, once each.** A drag emits `onScrollBeginDrag` on the frame it starts and
 *   `onScrollEndDrag` on the frame the finger lifts; momentum brackets itself inside that with
 *   `onMomentumScrollBegin` and `onMomentumScrollEnd`. A frame that is both — the release — emits the drag's end
 *   before the momentum's beginning.
 *
 * There is no state here that is not per-ScrollView, and no clock: the caller hands over the frame time it
 * integrated the physics with, so a headless run at a fixed step and a window pacing off `wp_presentation` get
 * the same cadence for the same scroll.
 */
class ScrollEventCadence final {
public:
    ScrollCadenceEvents advance(const ScrollCadenceFrame& frame);

private:
    double millisecondsSinceScroll_{0.0};
    bool hasUnreportedMovement_{false};
    bool wasDragging_{false};
    bool wasMomentumRunning_{false};
};

} // namespace react_native_linux
