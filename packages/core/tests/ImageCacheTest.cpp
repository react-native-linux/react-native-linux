#include "ImageContent.h"

#include <cstdint>
#include <gtest/gtest.h>
#include <string>
#include <utility>
#include <vector>

namespace {

using react_native_linux::ImageCache;

/**
 * A payload whose destruction is observable, which is how "the decoded bitmap dies with its node" is measured:
 * the cache is handed bytes, and the test asserts the bytes' lifetime ended exactly when the cache and every
 * holder dropped the entry.
 */
struct TrackedImage {
    explicit TrackedImage(bool* destroyedFlag) : destroyed(destroyedFlag) {}

    ~TrackedImage() {
        if (destroyed != nullptr) {
            *destroyed = true;
        }
    }

    bool* destroyed;
};

/**
 * An image handed to the cache with a deleter that flips the destroyed flag instead of deleting: the test holds
 * the flag and observes the drop.
 */
std::shared_ptr<void> trackedImage(bool* destroyedFlag) {
    return std::shared_ptr<void>(new TrackedImage(destroyedFlag), [](TrackedImage* image) { delete image; });
}

TEST(ImageCacheTest, AdmitsAnImageAndAccountsItsBytes) {
    ImageCache cache{1000};

    cache.insert("a", trackedImage(nullptr), 300);

    EXPECT_NE(cache.find("a"), nullptr);
    EXPECT_EQ(cache.byteCount(), 300U);
    EXPECT_EQ(cache.entryCount(), 1U);
}

TEST(ImageCacheTest, AFindingRefreshesRecencySoTheOtherEndEvictsFirst) {
    ImageCache cache{1000};

    cache.insert("first", trackedImage(nullptr), 400);
    cache.insert("second", trackedImage(nullptr), 400);

    cache.find("first");

    cache.insert("third", trackedImage(nullptr), 400);

    EXPECT_NE(cache.find("first"), nullptr);
    EXPECT_EQ(cache.find("second"), nullptr);
    EXPECT_NE(cache.find("third"), nullptr);
    EXPECT_EQ(cache.byteCount(), 800U);
}

TEST(ImageCacheTest, EvictionDropsTheLastReferenceAndFreesThePixels) {
    ImageCache cache{1000};
    bool destroyed = false;

    cache.insert("big", trackedImage(&destroyed), 900);
    cache.insert("next", trackedImage(nullptr), 900);

    EXPECT_TRUE(destroyed);
    EXPECT_EQ(cache.find("big"), nullptr);
    EXPECT_EQ(cache.byteCount(), 900U);
}

TEST(ImageCacheTest, AnImageLargerThanTheWholeCacheIsNeverAdmitted) {
    ImageCache cache{1000};
    bool destroyed = false;

    cache.insert("huge", trackedImage(&destroyed), 5000);

    EXPECT_TRUE(destroyed);
    EXPECT_EQ(cache.find("huge"), nullptr);
    EXPECT_EQ(cache.byteCount(), 0U);
    EXPECT_EQ(cache.entryCount(), 0U);
}

TEST(ImageCacheTest, ReinsertingTheSameSourceReplacesTheBytes) {
    ImageCache cache{1000};
    bool firstDestroyed = false;

    cache.insert("a", trackedImage(&firstDestroyed), 400);
    cache.insert("a", trackedImage(nullptr), 100);

    EXPECT_TRUE(firstDestroyed);
    EXPECT_EQ(cache.byteCount(), 100U);
    EXPECT_EQ(cache.entryCount(), 1U);
}

TEST(ImageCacheTest, ReferenceDropIsByteAccountedWhenAHolderOutlivesTheCache) {
    bool destroyed = false;

    std::shared_ptr<void> held;

    {
        ImageCache cache{1000};

        cache.insert("held", trackedImage(&destroyed), 500);

        held = cache.find("held");
    }

    EXPECT_FALSE(destroyed);

    held.reset();

    EXPECT_TRUE(destroyed);
}

TEST(ImageCacheTest, AnEmptyCacheFindsNothingAndCountsNothing) {
    ImageCache cache{1000};

    EXPECT_EQ(cache.find("nothing"), nullptr);
    EXPECT_EQ(cache.byteCount(), 0U);
    EXPECT_EQ(cache.entryCount(), 0U);
}

} // namespace
