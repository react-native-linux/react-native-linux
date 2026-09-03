#include "FrameTiming.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace react_native_linux {

namespace {

constexpr double kMedianFraction = 0.50;
constexpr double kPercentile95Fraction = 0.95;

/**
 * Nearest-rank percentile: the value at ceil(fraction * count) in the sorted samples, one-based. No interpolation
 * between neighbours, so every reported number is a frame time that actually happened.
 */
uint64_t percentileNanoseconds(const std::vector<uint64_t>& sortedIntervals, double fraction) {
    if (sortedIntervals.empty()) {
        return 0;
    }

    const size_t rank = static_cast<size_t>(std::ceil(fraction * static_cast<double>(sortedIntervals.size())));

    return sortedIntervals[rank - 1];
}

} // namespace

FrameTiming::FrameTiming(size_t sampleCapacity) : sampleCapacity_(sampleCapacity) {}

FrameTiming::Frame FrameTiming::recordPresented(uint64_t sequence, uint64_t presentedNanoseconds,
                                                uint32_t refreshNanoseconds, uint32_t flags) {
    Frame frame{.sequence = sequence,
                .presentedNanoseconds = presentedNanoseconds,
                .refreshNanoseconds = refreshNanoseconds,
                .flags = flags,
                .frameNanoseconds = std::nullopt};

    if (previousPresentedNanoseconds_.has_value()) {
        const uint64_t intervalNanoseconds = presentedNanoseconds - previousPresentedNanoseconds_.value();
        frame.frameNanoseconds = intervalNanoseconds;

        if (intervalNanoseconds_.size() == sampleCapacity_) {
            intervalNanoseconds_.pop_front();
        }

        intervalNanoseconds_.push_back(intervalNanoseconds);
    }

    previousPresentedNanoseconds_ = presentedNanoseconds;
    ++presentedFrames_;

    return frame;
}

void FrameTiming::recordDiscarded() { ++discardedFrames_; }

FrameTiming::Summary FrameTiming::summarise() const {
    std::vector<uint64_t> sortedIntervals(intervalNanoseconds_.begin(), intervalNanoseconds_.end());
    std::sort(sortedIntervals.begin(), sortedIntervals.end());

    return Summary{
        .frames = presentedFrames_,
        .discarded = discardedFrames_,
        .medianNanoseconds = percentileNanoseconds(sortedIntervals, kMedianFraction),
        .percentile95Nanoseconds = percentileNanoseconds(sortedIntervals, kPercentile95Fraction),
        .maximumNanoseconds = sortedIntervals.empty() ? 0 : sortedIntervals.back(),
    };
}

std::string FrameTiming::formatFrameLine(const Frame& frame) {
    std::string line = "{\"seq\":" + std::to_string(frame.sequence) +
                       ",\"presentedNs\":" + std::to_string(frame.presentedNanoseconds) +
                       ",\"refreshNs\":" + std::to_string(frame.refreshNanoseconds) +
                       ",\"flags\":" + std::to_string(frame.flags);

    if (frame.frameNanoseconds.has_value()) {
        line += ",\"frameNs\":" + std::to_string(frame.frameNanoseconds.value());
    }

    return line + "}";
}

std::string FrameTiming::formatSummaryLine(const Summary& summary, bool isPresentationSupported) {
    std::string line = "{\"summary\":true,\"frames\":" + std::to_string(summary.frames) +
                       ",\"discarded\":" + std::to_string(summary.discarded) +
                       ",\"p50Ns\":" + std::to_string(summary.medianNanoseconds) +
                       ",\"p95Ns\":" + std::to_string(summary.percentile95Nanoseconds) +
                       ",\"maxNs\":" + std::to_string(summary.maximumNanoseconds);

    if (!isPresentationSupported) {
        line += ",\"unsupported\":true";
    }

    return line + "}";
}

} // namespace react_native_linux
