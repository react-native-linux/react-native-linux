#include "ParagraphLayoutCache.h"

#include <gtest/gtest.h>
#include <string>

namespace {

using react_native_linux::ParagraphLayoutCache;

const ParagraphLayoutCache::Key kKey{.text = "hello", .attributes = "size=14", .maximumWidth = 200.0F};

ParagraphLayoutCache::Metrics metricsOf(float width, float height) {
    return ParagraphLayoutCache::Metrics{.longestLineWidth = width, .height = height};
}

/** A maker that counts, so a test can tell a shape from a hit without mocking Skia. */
ParagraphLayoutCache::Metrics countingShape(int& calls) {
    calls += 1;

    return ParagraphLayoutCache::Metrics{.longestLineWidth = static_cast<float>(calls), .height = 10.0F};
}

TEST(ParagraphLayoutCacheTest, AHitInTheCurrentFrameDoesNotShapeAgain) {
    ParagraphLayoutCache cache(8);
    int calls = 0;

    (void)cache.lookup(kKey, [&calls] { return countingShape(calls); });
    const ParagraphLayoutCache::Metrics second = cache.lookup(kKey, [&calls] { return countingShape(calls); });

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(second.longestLineWidth, 1.0F);
    EXPECT_EQ(cache.hitCount(), 1U);
    EXPECT_EQ(cache.missCount(), 1U);
}

TEST(ParagraphLayoutCacheTest, AHitPromotedFromThePreviousFrameSurvivesTheNextSwap) {
    ParagraphLayoutCache cache(8);
    int calls = 0;

    (void)cache.lookup(kKey, [&calls] { return countingShape(calls); });
    cache.endFrame();

    // One frame of non-use: the promote path, still no second shape. The promoted copy re-enters the current
    // frame, so touching it every other frame keeps it alive indefinitely — a list scrolled one row at a time
    // re-measures free.
    const ParagraphLayoutCache::Metrics promoted = cache.lookup(kKey, [&calls] { return countingShape(calls); });

    EXPECT_EQ(calls, 1);
    EXPECT_EQ(promoted.height, 10.0F);

    cache.endFrame();
}

TEST(ParagraphLayoutCacheTest, TwoConsecutivelyUntouchedFramesEvictTheEntry) {
    ParagraphLayoutCache cache(8);
    int calls = 0;

    (void)cache.lookup(kKey, [&calls] { return countingShape(calls); });
    cache.endFrame();

    // Frame 2 never asks for the key...
    cache.endFrame();

    // ...so frame 3's lookup is a shape again: the entry survived exactly one frame of non-use.
    const ParagraphLayoutCache::Metrics reshaped = cache.lookup(kKey, [&calls] { return countingShape(calls); });

    EXPECT_EQ(calls, 2);
    EXPECT_EQ(reshaped.longestLineWidth, 2.0F);
}

TEST(ParagraphLayoutCacheTest, EndFrameSwapsAndDropsEveryEntryTheFrameNeverTouched) {
    ParagraphLayoutCache cache(8);

    (void)cache.lookup(kKey, [] { return ParagraphLayoutCache::Metrics{}; });

    const ParagraphLayoutCache::Key otherKey{.text = "other", .attributes = "", .maximumWidth = 100.0F};

    // A second entry the frame touches keeps both alive through this swap...
    (void)cache.lookup(otherKey,
                       [] { return ParagraphLayoutCache::Metrics{.longestLineWidth = 2.0F, .height = 2.0F}; });
    cache.endFrame();
    EXPECT_EQ(cache.currentFrameEntryCount(), 0U);

    // ...and the first key is one frame stale now, so it comes back as a promote rather than a shape.
    int calls = 0;
    (void)cache.lookup(kKey, [&calls] { return countingShape(calls); });
    EXPECT_EQ(calls, 0);
}

TEST(ParagraphLayoutCacheTest, AKeyThatDiffersOnlyInWrapWidthIsAMiss) {
    ParagraphLayoutCache cache(8);
    int calls = 0;

    (void)cache.lookup(kKey, [&calls] { return countingShape(calls); });

    const ParagraphLayoutCache::Key narrower{.text = kKey.text, .attributes = kKey.attributes, .maximumWidth = 150.0F};

    (void)cache.lookup(narrower, [&calls] { return countingShape(calls); });

    EXPECT_EQ(calls, 2);
}

TEST(ParagraphLayoutCacheTest, AKeyThatDiffersOnlyInAttributesIsAMiss) {
    ParagraphLayoutCache cache(8);
    int calls = 0;

    (void)cache.lookup(kKey, [&calls] { return countingShape(calls); });

    const ParagraphLayoutCache::Key otherAttributes{
        .text = kKey.text, .attributes = "fontSize=14", .maximumWidth = kKey.maximumWidth};

    (void)cache.lookup(otherAttributes, [&calls] { return countingShape(calls); });

    EXPECT_EQ(calls, 2);
}

const ParagraphLayoutCache::Key kOtherKey{.text = "other", .attributes = "", .maximumWidth = 100.0F};

ParagraphLayoutCache::Metrics otherMetrics() {
    return ParagraphLayoutCache::Metrics{.longestLineWidth = 2.0F, .height = 2.0F};
}

ParagraphLayoutCache::Metrics keyMetrics() {
    return ParagraphLayoutCache::Metrics{.longestLineWidth = 1.0F, .height = 1.0F};
}

TEST(ParagraphLayoutCacheTest, APromoteAtCapacityStillReturnsTheMetricsWithoutReShaping) {
    ParagraphLayoutCache cache(1);

    (void)cache.lookup(kKey, keyMetrics);
    cache.endFrame();

    // A different key fills the current frame first, so the promoted entry cannot re-enter it — but the hit
    // still returns the metrics, and still costs no shape.
    (void)cache.lookup(kOtherKey, otherMetrics);

    int calls = 0;
    const ParagraphLayoutCache::Metrics promoted = cache.lookup(kKey, [&calls] { return countingShape(calls); });

    EXPECT_EQ(calls, 0);
    EXPECT_EQ(promoted.longestLineWidth, 1.0F);
}

TEST(ParagraphLayoutCacheTest, TheCapacityCapsTheCurrentFrameAndNeverEvictsThePreviousOnePrematurely) {
    ParagraphLayoutCache cache(1);

    (void)cache.lookup(kKey, keyMetrics);
    (void)cache.lookup(kOtherKey, otherMetrics);

    // The first entry was pushed out of the current frame by the capacity; the previous frame still holds it.
    cache.endFrame();
    cache.endFrame();

    int calls = 0;
    (void)cache.lookup(kKey, [&calls] { return countingShape(calls); });

    EXPECT_EQ(calls, 1);
}

} // namespace
