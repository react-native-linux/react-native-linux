#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace react_native_linux {

/**
 * The frame-timing probe of #7: `wp_presentation_feedback` records, the interval between them, and the summary
 * the e2e driver asserts a budget against.
 *
 * ADR-0001 decision 3 is what makes this the right measurement. `wl_surface.frame` throttles and its timestamp
 * has an undefined base, so it cannot be a frame-time source; `wp_presentation_feedback` is purely retrospective
 * and reports when a content update actually turned into light. A frame time is therefore the delta between two
 * consecutive `presented` timestamps, which is why the first presented frame has no delta at all rather than a
 * delta measured from something else.
 *
 * Pure: no Wayland, no clock reads, no file handles. Every value is passed in by the caller, which is what puts
 * the percentile arithmetic under the C++ coverage gate without a compositor. The timestamps are whatever clock
 * domain `wp_presentation.clock_id` named — only their differences are used, so the domain never matters.
 *
 * The interval samples are a ring of the last `sampleCapacity` deltas: a long-running window must not grow a
 * vector for the life of the process, and a percentile over the recent past is what a frame budget is about.
 */
class FrameTiming final {
public:
    struct Frame {
        uint64_t sequence{0};
        uint64_t presentedNanoseconds{0};
        uint32_t refreshNanoseconds{0};
        uint32_t flags{0};
        std::optional<uint64_t> frameNanoseconds;
    };

    struct Summary {
        size_t frames{0};
        size_t discarded{0};
        uint64_t medianNanoseconds{0};
        uint64_t percentile95Nanoseconds{0};
        uint64_t maximumNanoseconds{0};
    };

    static constexpr size_t kDefaultSampleCapacity = 4096;

    explicit FrameTiming(size_t sampleCapacity = kDefaultSampleCapacity);

    Frame recordPresented(uint64_t sequence, uint64_t presentedNanoseconds, uint32_t refreshNanoseconds,
                          uint32_t flags);
    void recordDiscarded();
    Summary summarise() const;

    /** One JSON Lines record per presented frame, as `--frame-log` writes it. */
    static std::string formatFrameLine(const Frame& frame);
    /**
     * The last line of a frame log. `isPresentationSupported` is false when the compositor advertised no
     * `wp_presentation` global at all, which is reported as `"unsupported":true` beside a frame count of zero
     * rather than as an empty file the driver would have to guess about.
     */
    static std::string formatSummaryLine(const Summary& summary, bool isPresentationSupported);

private:
    size_t sampleCapacity_;
    std::deque<uint64_t> intervalNanoseconds_;
    std::optional<uint64_t> previousPresentedNanoseconds_;
    size_t presentedFrames_{0};
    size_t discardedFrames_{0};
};

} // namespace react_native_linux
