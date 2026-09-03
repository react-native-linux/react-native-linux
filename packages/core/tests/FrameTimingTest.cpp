#include "FrameTiming.h"

#include <cstdint>
#include <gtest/gtest.h>

namespace {

using react_native_linux::FrameTiming;

constexpr uint64_t kMillisecond = 1'000'000;
constexpr uint32_t kRefreshNanoseconds = 16'666'666;
constexpr uint32_t kVsyncFlag = 0x1;
constexpr uint64_t kFirstSequence = 41;

/**
 * Twenty intervals of 1 ms to 20 ms, presented out of order so the percentiles cannot come from insertion order.
 * Nearest-rank puts p50 at ceil(0.50 * 20) = 10 and p95 at ceil(0.95 * 20) = 19, so the answers are 10 ms and
 * 19 ms and the maximum is 20 ms.
 */
constexpr uint64_t kKnownIntervalsMilliseconds[] = {7,  3, 20, 1, 14, 9, 2,  18, 5,  11,
                                                    16, 4, 19, 8, 13, 6, 17, 10, 15, 12};

FrameTiming buildKnownTable() {
    FrameTiming timing;
    uint64_t presentedNanoseconds = 0;
    uint64_t sequence = 0;

    timing.recordPresented(sequence, presentedNanoseconds, kRefreshNanoseconds, 0);

    for (const uint64_t intervalMilliseconds : kKnownIntervalsMilliseconds) {
        presentedNanoseconds += intervalMilliseconds * kMillisecond;
        ++sequence;
        timing.recordPresented(sequence, presentedNanoseconds, kRefreshNanoseconds, 0);
    }

    return timing;
}

TEST(FrameTimingTest, AnEmptyProbeSummarisesToZeroes) {
    const FrameTiming::Summary summary = FrameTiming().summarise();

    EXPECT_EQ(summary.frames, 0U);
    EXPECT_EQ(summary.discarded, 0U);
    EXPECT_EQ(summary.medianNanoseconds, 0U);
    EXPECT_EQ(summary.percentile95Nanoseconds, 0U);
    EXPECT_EQ(summary.maximumNanoseconds, 0U);
}

TEST(FrameTimingTest, TheFirstPresentedFrameHasNoDelta) {
    FrameTiming timing;

    const FrameTiming::Frame frame =
        timing.recordPresented(kFirstSequence, 5 * kMillisecond, kRefreshNanoseconds, kVsyncFlag);

    EXPECT_EQ(frame.sequence, kFirstSequence);
    EXPECT_EQ(frame.presentedNanoseconds, 5U * kMillisecond);
    EXPECT_EQ(frame.refreshNanoseconds, kRefreshNanoseconds);
    EXPECT_EQ(frame.flags, kVsyncFlag);
    EXPECT_FALSE(frame.frameNanoseconds.has_value());
}

TEST(FrameTimingTest, ASinglePresentedFrameCountsWithoutAnyInterval) {
    FrameTiming timing;

    timing.recordPresented(kFirstSequence, 5 * kMillisecond, kRefreshNanoseconds, kVsyncFlag);

    const FrameTiming::Summary summary = timing.summarise();

    EXPECT_EQ(summary.frames, 1U);
    EXPECT_EQ(summary.medianNanoseconds, 0U);
    EXPECT_EQ(summary.percentile95Nanoseconds, 0U);
    EXPECT_EQ(summary.maximumNanoseconds, 0U);
}

TEST(FrameTimingTest, TheSecondPresentedFrameCarriesTheDeltaToTheFirst) {
    FrameTiming timing;

    timing.recordPresented(0, 5 * kMillisecond, kRefreshNanoseconds, 0);

    const FrameTiming::Frame second = timing.recordPresented(1, 21 * kMillisecond, kRefreshNanoseconds, 0);

    ASSERT_TRUE(second.frameNanoseconds.has_value());
    EXPECT_EQ(second.frameNanoseconds.value(), 16U * kMillisecond);
}

TEST(FrameTimingTest, PercentilesUseNearestRankOverAKnownTable) {
    const FrameTiming::Summary summary = buildKnownTable().summarise();

    EXPECT_EQ(summary.frames, 21U);
    EXPECT_EQ(summary.discarded, 0U);
    EXPECT_EQ(summary.medianNanoseconds, 10U * kMillisecond);
    EXPECT_EQ(summary.percentile95Nanoseconds, 19U * kMillisecond);
    EXPECT_EQ(summary.maximumNanoseconds, 20U * kMillisecond);
}

TEST(FrameTimingTest, DiscardedContentUpdatesAreCountedAndNeverTimed) {
    FrameTiming timing;

    timing.recordPresented(0, 0, kRefreshNanoseconds, 0);
    timing.recordDiscarded();
    timing.recordDiscarded();
    timing.recordPresented(1, 16 * kMillisecond, kRefreshNanoseconds, 0);

    const FrameTiming::Summary summary = timing.summarise();

    EXPECT_EQ(summary.frames, 2U);
    EXPECT_EQ(summary.discarded, 2U);
    EXPECT_EQ(summary.maximumNanoseconds, 16U * kMillisecond);
}

TEST(FrameTimingTest, TheSampleRingKeepsOnlyTheMostRecentIntervals) {
    FrameTiming timing(2);

    timing.recordPresented(0, 0, kRefreshNanoseconds, 0);
    timing.recordPresented(1, 100 * kMillisecond, kRefreshNanoseconds, 0);
    timing.recordPresented(2, 102 * kMillisecond, kRefreshNanoseconds, 0);
    timing.recordPresented(3, 106 * kMillisecond, kRefreshNanoseconds, 0);

    const FrameTiming::Summary summary = timing.summarise();

    EXPECT_EQ(summary.frames, 4U);
    EXPECT_EQ(summary.maximumNanoseconds, 4U * kMillisecond);
    EXPECT_EQ(summary.medianNanoseconds, 2U * kMillisecond);
}

TEST(FrameTimingTest, AFrameLineOmitsTheDeltaOnlyForTheFirstFrame) {
    FrameTiming timing;

    const FrameTiming::Frame first = timing.recordPresented(7, 1'000'000'000, kRefreshNanoseconds, kVsyncFlag);
    const FrameTiming::Frame second = timing.recordPresented(8, 1'016'000'000, kRefreshNanoseconds, kVsyncFlag);

    EXPECT_EQ(FrameTiming::formatFrameLine(first),
              "{\"seq\":7,\"presentedNs\":1000000000,\"refreshNs\":16666666,\"flags\":1}");
    EXPECT_EQ(FrameTiming::formatFrameLine(second),
              "{\"seq\":8,\"presentedNs\":1016000000,\"refreshNs\":16666666,\"flags\":1,\"frameNs\":16000000}");
}

TEST(FrameTimingTest, ASupportedSummaryLineCarriesEveryPercentile) {
    EXPECT_EQ(FrameTiming::formatSummaryLine(buildKnownTable().summarise(), true),
              "{\"summary\":true,\"frames\":21,\"discarded\":0,\"p50Ns\":10000000,\"p95Ns\":19000000,"
              "\"maxNs\":20000000}");
}

TEST(FrameTimingTest, AnUnsupportedSummaryLineSaysSoBesideAnEmptyCount) {
    EXPECT_EQ(FrameTiming::formatSummaryLine(FrameTiming().summarise(), false),
              "{\"summary\":true,\"frames\":0,\"discarded\":0,\"p50Ns\":0,\"p95Ns\":0,\"maxNs\":0,"
              "\"unsupported\":true}");
}

} // namespace
