#include "FrameJournal.h"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <vector>

namespace react_native_linux {

namespace {

// A one-sample ring is the floor: `recordPresented` pops the front once the ring is full, and a zero capacity
// would pop an empty deque.
constexpr size_t kMinimumSampleCapacity = 1;

constexpr double kMedianFraction = 0.50;
constexpr double kPercentile95Fraction = 0.95;

/** Nearest-rank percentile, matching `FrameTiming`'s own — see that file for why no interpolation. */
uint64_t percentileNanoseconds(const std::vector<uint64_t>& sortedSamples, double fraction) {
    if (sortedSamples.empty()) {
        return 0;
    }

    const size_t rank = static_cast<size_t>(std::ceil(fraction * static_cast<double>(sortedSamples.size())));

    return sortedSamples[rank - 1];
}

} // namespace

FrameJournal::FrameJournal(uint64_t paintHangThresholdNanoseconds, uint64_t totalHangThresholdNanoseconds,
                           size_t sampleCapacity)
    : paintHangThresholdNanoseconds_(paintHangThresholdNanoseconds),
      totalHangThresholdNanoseconds_(totalHangThresholdNanoseconds),
      sampleCapacity_(std::max<size_t>(sampleCapacity, kMinimumSampleCapacity)) {}

void FrameJournal::recordDamage(uint64_t nowNanoseconds) {
    if (openInterval_.has_value()) {
        ++openInterval_->invalidationCount;

        return;
    }

    openInterval_ = OpenInterval{.dirtyAtNanoseconds = nowNanoseconds, .invalidationCount = 1};
}

void FrameJournal::recordPaintStart(uint64_t nowNanoseconds) {
    if (openInterval_.has_value()) {
        openInterval_->paintStartNanoseconds = nowNanoseconds;
    }
}

void FrameJournal::recordPaintEnd(uint64_t nowNanoseconds) {
    if (openInterval_.has_value()) {
        openInterval_->paintEndNanoseconds = nowNanoseconds;
    }
}

std::optional<FrameJournal::ClosedFrame> FrameJournal::recordPresented(uint64_t presentedNanoseconds) {
    if (!openInterval_.has_value()) {
        return std::nullopt;
    }

    const OpenInterval interval = openInterval_.value();
    openInterval_.reset();

    const uint64_t dirtyToPresentNanoseconds = presentedNanoseconds - interval.dirtyAtNanoseconds;
    std::optional<uint64_t> paintNanoseconds;
    bool isHang = dirtyToPresentNanoseconds >= totalHangThresholdNanoseconds_;

    if (interval.paintStartNanoseconds.has_value() && interval.paintEndNanoseconds.has_value()) {
        paintNanoseconds = interval.paintEndNanoseconds.value() - interval.paintStartNanoseconds.value();
        isHang = isHang || paintNanoseconds.value() >= paintHangThresholdNanoseconds_;
    }

    ++presentedFrames_;

    if (isHang) {
        ++hangCount_;
    }

    if (dirtyToPresentNanoseconds_.size() == sampleCapacity_) {
        dirtyToPresentNanoseconds_.pop_front();
    }

    dirtyToPresentNanoseconds_.push_back(dirtyToPresentNanoseconds);

    return ClosedFrame{
        .dirtyToPresentNanoseconds = dirtyToPresentNanoseconds, .paintNanoseconds = paintNanoseconds, .isHang = isHang};
}

void FrameJournal::recordDiscontinuity() { openInterval_.reset(); }

FrameJournal::Summary FrameJournal::summarise() const {
    std::vector<uint64_t> sortedSamples(dirtyToPresentNanoseconds_.begin(), dirtyToPresentNanoseconds_.end());
    std::sort(sortedSamples.begin(), sortedSamples.end());

    return Summary{
        .frames = presentedFrames_,
        .hangs = hangCount_,
        .medianNanoseconds = percentileNanoseconds(sortedSamples, kMedianFraction),
        .percentile95Nanoseconds = percentileNanoseconds(sortedSamples, kPercentile95Fraction),
        .maximumNanoseconds = sortedSamples.empty() ? 0 : sortedSamples.back(),
    };
}

uint64_t FrameJournal::pendingInvalidations() const noexcept {
    return openInterval_.has_value() ? openInterval_->invalidationCount : 0;
}

std::string FrameJournal::formatClosedFrameLine(const ClosedFrame& frame) {
    std::string line = "{\"journal\":true,\"dirtyToPresentNs\":" + std::to_string(frame.dirtyToPresentNanoseconds);

    if (frame.paintNanoseconds.has_value()) {
        line += ",\"paintNs\":" + std::to_string(frame.paintNanoseconds.value());
    }

    line += frame.isHang ? std::string(",\"hang\":true") : std::string(",\"hang\":false");

    return line + "}";
}

int64_t presentationClockOffsetNanoseconds(std::optional<uint32_t> presentationClockId,
                                           std::optional<uint64_t> presentationClockNanoseconds,
                                           uint64_t steadyClockNanoseconds) {
    if (!presentationClockId.has_value() || presentationClockId.value() == static_cast<uint32_t>(CLOCK_MONOTONIC) ||
        !presentationClockNanoseconds.has_value()) {
        return 0;
    }

    return static_cast<int64_t>(steadyClockNanoseconds) - static_cast<int64_t>(presentationClockNanoseconds.value());
}

uint64_t toSteadyClockNanoseconds(uint64_t presentationTimestampNanoseconds, int64_t offsetNanoseconds) {
    const int64_t converted = static_cast<int64_t>(presentationTimestampNanoseconds) + offsetNanoseconds;

    return converted < 0 ? 0 : static_cast<uint64_t>(converted);
}

std::string FrameJournal::formatSummaryLine(const Summary& summary) {
    return "{\"journalSummary\":true,\"frames\":" + std::to_string(summary.frames) +
           ",\"hangs\":" + std::to_string(summary.hangs) + ",\"p50Ns\":" + std::to_string(summary.medianNanoseconds) +
           ",\"p95Ns\":" + std::to_string(summary.percentile95Nanoseconds) +
           ",\"maxNs\":" + std::to_string(summary.maximumNanoseconds) + "}";
}

} // namespace react_native_linux
