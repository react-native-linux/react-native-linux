#include "ScrollEventCadence.h"

namespace react_native_linux {

ScrollCadenceEvents ScrollEventCadence::advance(const ScrollCadenceFrame& frame) {
    ScrollCadenceEvents events{.beginDrag = frame.isDragging && !wasDragging_,
                               .endDrag = !frame.isDragging && wasDragging_,
                               .momentumBegin = frame.isMomentumRunning && !wasMomentumRunning_,
                               .momentumEnd = !frame.isMomentumRunning && wasMomentumRunning_};

    millisecondsSinceScroll_ += frame.frameMilliseconds;
    hasUnreportedMovement_ = hasUnreportedMovement_ || frame.hasMoved;

    // A boundary flushes whatever has moved since the last dispatch, so the offset a drag or a fling ended at is
    // always reported — and it is reported before the event that says it ended.
    const bool isBoundary = events.endDrag || events.momentumEnd;

    events.scroll = hasUnreportedMovement_ && (isBoundary || millisecondsSinceScroll_ >= frame.throttleMilliseconds);

    if (events.scroll) {
        millisecondsSinceScroll_ = 0.0;
        hasUnreportedMovement_ = false;
    }

    wasDragging_ = frame.isDragging;
    wasMomentumRunning_ = frame.isMomentumRunning;

    return events;
}

} // namespace react_native_linux
