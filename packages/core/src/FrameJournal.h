#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace react_native_linux {

/**
 * The frame journal of #345: the number a user complains about is neither #20's p95 pacing gate nor #124's
 * native-driver frame cost (`FrameTiming` owns both) — it is GPUI's `PresentedFrame::dirty_to_present_duration()`,
 * the span from the first invalidation a frame is answering to the moment that frame was presented.
 *
 * `recordDamage` is the clean-to-dirty edge, and only the edge: a call while an interval is already open is a
 * coalesced invalidation — it does not move `dirtyAt`, it only counts, exactly as GPUI's journal counts how many
 * invalidations one frame answered. `recordPaintStart`/`recordPaintEnd` bracket the paint span of whichever frame
 * closes the interval, and are no-ops when no interval is open, because a callback-driven draw of an unchanged
 * picture still paints without ever having been dirty. `recordPresented` closes the open interval against a
 * presentation result and reports `std::nullopt` for exactly that idle-boundary case — a presented frame with
 * nothing recorded dirty since the last close is a real outcome, not an error.
 *
 * `recordDiscontinuity` is the other way an interval ends: a `wp_presentation_feedback.discarded`, or any other
 * reason a caller cannot say which presentation answered it, abandons the interval outright. The next
 * `recordPresented` starts clean, so a latency is never computed across a frame the compositor threw away.
 *
 * Pure, in the shape of `FrameClock` and `FrameTiming`: no clock reads, no Wayland, every timestamp a nanosecond
 * count the caller supplies. The caller — `WindowSession` for the dirty edge and the paint span, `WindowMain` for
 * closing against a `wp_presentation` result — is expected to share one clock domain, `std::chrono::steady_clock`,
 * which is `CLOCK_MONOTONIC` on this platform and therefore the same domain `wp_presentation.clock_id` reports
 * under every compositor this runs on; see *Frame timing* in docs/cpp-toolchain.md for why that match is not
 * resolved more rigorously than stating it.
 *
 * The hang rule has two independent triggers, matching GPUI's `crates/gpui/src/profiler/hang.rs`: a paint whose
 * own span exceeded `paintHangThresholdNanoseconds`, or a total dirty-to-present that reached
 * `totalHangThresholdNanoseconds` — "many small pieces of work can drop a frame as thoroughly as one long
 * stall". Either flags the closed frame `isHang`; a frame with no paint span recorded can still hang on the
 * total trigger alone.
 */
class FrameJournal final {
public:
    struct ClosedFrame {
        uint64_t dirtyToPresentNanoseconds{0};
        std::optional<uint64_t> paintNanoseconds;
        bool isHang{false};
    };

    struct Summary {
        size_t frames{0};
        size_t hangs{0};
        uint64_t medianNanoseconds{0};
        uint64_t percentile95Nanoseconds{0};
        uint64_t maximumNanoseconds{0};
    };

    static constexpr size_t kDefaultSampleCapacity = 4096;

    FrameJournal(uint64_t paintHangThresholdNanoseconds, uint64_t totalHangThresholdNanoseconds,
                size_t sampleCapacity = kDefaultSampleCapacity);

    void recordDamage(uint64_t nowNanoseconds);
    void recordPaintStart(uint64_t nowNanoseconds);
    void recordPaintEnd(uint64_t nowNanoseconds);
    std::optional<ClosedFrame> recordPresented(uint64_t presentedNanoseconds);
    void recordDiscontinuity();

    Summary summarise() const;

    /** How many `recordDamage` calls coalesced into the interval open right now; zero when clean. Test seam. */
    uint64_t pendingInvalidations() const noexcept;

    /** One JSON Lines record per closed frame, as `--frame-log` writes it beside `FrameTiming`'s own record. */
    static std::string formatClosedFrameLine(const ClosedFrame& frame);
    /** The last frame-journal line of a frame log. */
    static std::string formatSummaryLine(const Summary& summary);

private:
    struct OpenInterval {
        uint64_t dirtyAtNanoseconds{0};
        uint64_t invalidationCount{0};
        std::optional<uint64_t> paintStartNanoseconds;
        std::optional<uint64_t> paintEndNanoseconds;
    };

    uint64_t paintHangThresholdNanoseconds_;
    uint64_t totalHangThresholdNanoseconds_;
    size_t sampleCapacity_;
    std::optional<OpenInterval> openInterval_;
    std::deque<uint64_t> dirtyToPresentNanoseconds_;
    size_t presentedFrames_{0};
    size_t hangCount_{0};
};

} // namespace react_native_linux
