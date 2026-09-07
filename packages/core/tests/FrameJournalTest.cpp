#include "FrameJournal.h"

#include <cstdint>
#include <gtest/gtest.h>

namespace {

using react_native_linux::FrameJournal;

constexpr uint64_t kMillisecond = 1'000'000;
constexpr uint64_t kPaintHangThreshold = 16 * kMillisecond;
constexpr uint64_t kTotalHangThreshold = 33 * kMillisecond;

FrameJournal buildJournal() { return FrameJournal(kPaintHangThreshold, kTotalHangThreshold); }

TEST(FrameJournalTest, AnEmptyJournalSummarisesToZeroes) {
    const FrameJournal::Summary summary = buildJournal().summarise();

    EXPECT_EQ(summary.frames, 0U);
    EXPECT_EQ(summary.hangs, 0U);
    EXPECT_EQ(summary.medianNanoseconds, 0U);
    EXPECT_EQ(summary.percentile95Nanoseconds, 0U);
    EXPECT_EQ(summary.maximumNanoseconds, 0U);
}

TEST(FrameJournalTest, APresentWithNothingDirtyIsAnIdleBoundaryWithNoSample) {
    FrameJournal journal = buildJournal();

    const std::optional<FrameJournal::ClosedFrame> closed = journal.recordPresented(5 * kMillisecond);

    EXPECT_FALSE(closed.has_value());
    EXPECT_EQ(journal.summarise().frames, 0U);
}

TEST(FrameJournalTest, ASingleInvalidationProducesOneDirtyAt) {
    FrameJournal journal = buildJournal();

    journal.recordDamage(1 * kMillisecond);
    const std::optional<FrameJournal::ClosedFrame> closed = journal.recordPresented(9 * kMillisecond);

    ASSERT_TRUE(closed.has_value());
    EXPECT_EQ(closed->dirtyToPresentNanoseconds, 8U * kMillisecond);
    EXPECT_FALSE(closed->isHang);
}

TEST(FrameJournalTest, CoalescedInvalidationsProduceOneDirtyAtFromTheFirst) {
    FrameJournal journal = buildJournal();

    journal.recordDamage(1 * kMillisecond);
    EXPECT_EQ(journal.pendingInvalidations(), 1U);

    journal.recordDamage(2 * kMillisecond);
    journal.recordDamage(3 * kMillisecond);
    EXPECT_EQ(journal.pendingInvalidations(), 3U);

    const std::optional<FrameJournal::ClosedFrame> closed = journal.recordPresented(9 * kMillisecond);

    ASSERT_TRUE(closed.has_value());
    // From the first damage (1 ms), not the last (3 ms): coalescing must not move dirtyAt.
    EXPECT_EQ(closed->dirtyToPresentNanoseconds, 8U * kMillisecond);
    EXPECT_EQ(journal.pendingInvalidations(), 0U);
}

TEST(FrameJournalTest, ClosingAnIntervalStartsTheNextOneClean) {
    FrameJournal journal = buildJournal();

    journal.recordDamage(1 * kMillisecond);
    journal.recordPresented(9 * kMillisecond);

    const std::optional<FrameJournal::ClosedFrame> secondClose = journal.recordPresented(20 * kMillisecond);

    EXPECT_FALSE(secondClose.has_value());
}

TEST(FrameJournalTest, ADiscontinuityAbandonsTheOpenIntervalWithoutASample) {
    FrameJournal journal = buildJournal();

    journal.recordDamage(1 * kMillisecond);
    journal.recordDiscontinuity();

    const std::optional<FrameJournal::ClosedFrame> closed = journal.recordPresented(9 * kMillisecond);

    EXPECT_FALSE(closed.has_value());
    EXPECT_EQ(journal.pendingInvalidations(), 0U);
}

TEST(FrameJournalTest, ADiscontinuityWithNoOpenIntervalIsANoOp) {
    FrameJournal journal = buildJournal();

    journal.recordDiscontinuity();

    EXPECT_EQ(journal.pendingInvalidations(), 0U);
}

TEST(FrameJournalTest, AFreshIntervalAfterADiscontinuityIsTimedFromItsOwnDirtyAt) {
    FrameJournal journal = buildJournal();

    journal.recordDamage(1 * kMillisecond);
    journal.recordDiscontinuity();

    journal.recordDamage(10 * kMillisecond);
    const std::optional<FrameJournal::ClosedFrame> closed = journal.recordPresented(15 * kMillisecond);

    ASSERT_TRUE(closed.has_value());
    EXPECT_EQ(closed->dirtyToPresentNanoseconds, 5U * kMillisecond);
}

TEST(FrameJournalTest, PaintTimestampsWithNoOpenIntervalAreANoOp) {
    FrameJournal journal = buildJournal();

    journal.recordPaintStart(1 * kMillisecond);
    journal.recordPaintEnd(2 * kMillisecond);

    journal.recordDamage(3 * kMillisecond);
    const std::optional<FrameJournal::ClosedFrame> closed = journal.recordPresented(4 * kMillisecond);

    ASSERT_TRUE(closed.has_value());
    EXPECT_FALSE(closed->paintNanoseconds.has_value());
}

TEST(FrameJournalTest, AnUnmatchedPaintTimestampCarriesNoPaintSpan) {
    FrameJournal startOnly = buildJournal();
    startOnly.recordDamage(1 * kMillisecond);
    startOnly.recordPaintStart(2 * kMillisecond);

    FrameJournal endOnly = buildJournal();
    endOnly.recordDamage(1 * kMillisecond);
    endOnly.recordPaintEnd(2 * kMillisecond);

    const std::optional<FrameJournal::ClosedFrame> closedFromStartOnly = startOnly.recordPresented(9 * kMillisecond);
    const std::optional<FrameJournal::ClosedFrame> closedFromEndOnly = endOnly.recordPresented(9 * kMillisecond);

    ASSERT_TRUE(closedFromStartOnly.has_value());
    ASSERT_TRUE(closedFromEndOnly.has_value());
    EXPECT_FALSE(closedFromStartOnly->paintNanoseconds.has_value());
    EXPECT_FALSE(closedFromEndOnly->paintNanoseconds.has_value());
}

TEST(FrameJournalTest, AClosedFrameCarriesThePaintSpan) {
    FrameJournal journal = buildJournal();

    journal.recordDamage(1 * kMillisecond);
    journal.recordPaintStart(2 * kMillisecond);
    journal.recordPaintEnd(5 * kMillisecond);
    const std::optional<FrameJournal::ClosedFrame> closed = journal.recordPresented(9 * kMillisecond);

    ASSERT_TRUE(closed.has_value());
    ASSERT_TRUE(closed->paintNanoseconds.has_value());
    EXPECT_EQ(closed->paintNanoseconds.value(), 3U * kMillisecond);
}

TEST(FrameJournalTest, APaintPastItsOwnThresholdHangsEvenUnderTheTotalBudget) {
    FrameJournal journal = buildJournal();

    journal.recordDamage(0);
    journal.recordPaintStart(1 * kMillisecond);
    journal.recordPaintEnd(1 * kMillisecond + kPaintHangThreshold + kMillisecond);
    const std::optional<FrameJournal::ClosedFrame> closed =
        journal.recordPresented(1 * kMillisecond + kPaintHangThreshold + 2 * kMillisecond);

    ASSERT_TRUE(closed.has_value());
    ASSERT_TRUE(closed->paintNanoseconds.has_value());
    EXPECT_GT(closed->paintNanoseconds.value(), kPaintHangThreshold);
    EXPECT_LT(closed->dirtyToPresentNanoseconds, kTotalHangThreshold);
    EXPECT_TRUE(closed->isHang);
}

TEST(FrameJournalTest, ATotalPastItsOwnThresholdHangsEvenWithAShortPaint) {
    FrameJournal journal = buildJournal();

    journal.recordDamage(0);
    journal.recordPaintStart(kTotalHangThreshold);
    journal.recordPaintEnd(kTotalHangThreshold + kMillisecond);
    const std::optional<FrameJournal::ClosedFrame> closed =
        journal.recordPresented(kTotalHangThreshold + 2 * kMillisecond);

    ASSERT_TRUE(closed.has_value());
    ASSERT_TRUE(closed->paintNanoseconds.has_value());
    EXPECT_LT(closed->paintNanoseconds.value(), kPaintHangThreshold);
    EXPECT_GT(closed->dirtyToPresentNanoseconds, kTotalHangThreshold);
    EXPECT_TRUE(closed->isHang);
}

TEST(FrameJournalTest, NeitherThresholdCrossedIsNotAHang) {
    FrameJournal journal = buildJournal();

    journal.recordDamage(0);
    journal.recordPaintStart(1 * kMillisecond);
    journal.recordPaintEnd(2 * kMillisecond);
    const std::optional<FrameJournal::ClosedFrame> closed = journal.recordPresented(3 * kMillisecond);

    ASSERT_TRUE(closed.has_value());
    EXPECT_FALSE(closed->isHang);
}

TEST(FrameJournalTest, PercentilesAndTheHangCountAccumulateOverMultipleClosedFrames) {
    FrameJournal journal = buildJournal();

    // Four ordinary frames, then one that hangs on the total trigger.
    for (uint64_t index = 0; index < 4; ++index) {
        const uint64_t base = index * 100 * kMillisecond;

        journal.recordDamage(base);
        journal.recordPresented(base + (index + 1) * kMillisecond);
    }

    journal.recordDamage(400 * kMillisecond);
    journal.recordPresented(400 * kMillisecond + kTotalHangThreshold + kMillisecond);

    const FrameJournal::Summary summary = journal.summarise();

    EXPECT_EQ(summary.frames, 5U);
    EXPECT_EQ(summary.hangs, 1U);
    EXPECT_EQ(summary.maximumNanoseconds, kTotalHangThreshold + kMillisecond);
}

TEST(FrameJournalTest, TheSampleRingKeepsOnlyTheMostRecentIntervals) {
    FrameJournal journal(kPaintHangThreshold, kTotalHangThreshold, 2);

    for (uint64_t index = 0; index < 4; ++index) {
        const uint64_t base = index * 100 * kMillisecond;

        journal.recordDamage(base);
        journal.recordPresented(base + (index + 1) * kMillisecond);
    }

    const FrameJournal::Summary summary = journal.summarise();

    // Cumulative counts see all four frames; the ring that feeds the percentiles keeps only the last two (3 ms
    // and 4 ms), so the maximum reported is 4 ms rather than the all-time 4 ms coincidentally equal here — see
    // the median below, which could only be 3.5 ms-rounding-down-to-3 if the first two samples had been evicted.
    EXPECT_EQ(summary.frames, 4U);
    EXPECT_EQ(summary.maximumNanoseconds, 4U * kMillisecond);
    EXPECT_EQ(summary.medianNanoseconds, 3U * kMillisecond);
}

TEST(FrameJournalTest, AClosedFrameLineOmitsThePaintFieldWithoutAPaintSpan) {
    const FrameJournal::ClosedFrame frame{
        .dirtyToPresentNanoseconds = 8 * kMillisecond, .paintNanoseconds = std::nullopt, .isHang = false};

    EXPECT_EQ(FrameJournal::formatClosedFrameLine(frame), "{\"journal\":true,\"dirtyToPresentNs\":8000000,\"hang\":false}");
}

TEST(FrameJournalTest, AClosedFrameLineCarriesThePaintFieldAndAHangFlag) {
    const FrameJournal::ClosedFrame frame{
        .dirtyToPresentNanoseconds = 40 * kMillisecond, .paintNanoseconds = 20 * kMillisecond, .isHang = true};

    EXPECT_EQ(FrameJournal::formatClosedFrameLine(frame),
             "{\"journal\":true,\"dirtyToPresentNs\":40000000,\"paintNs\":20000000,\"hang\":true}");
}

TEST(FrameJournalTest, ASummaryLineCarriesEveryField) {
    const FrameJournal::Summary summary{
        .frames = 238, .hangs = 1, .medianNanoseconds = 11 * kMillisecond,
        .percentile95Nanoseconds = 15 * kMillisecond, .maximumNanoseconds = 33 * kMillisecond};

    EXPECT_EQ(FrameJournal::formatSummaryLine(summary),
             "{\"journalSummary\":true,\"frames\":238,\"hangs\":1,\"p50Ns\":11000000,\"p95Ns\":15000000,"
             "\"maxNs\":33000000}");
}

} // namespace
