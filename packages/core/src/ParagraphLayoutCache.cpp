#include "ParagraphLayoutCache.h"

#include <mutex>
#include <utility>

namespace react_native_linux {

ParagraphLayoutCache::ParagraphLayoutCache(size_t capacity) : capacity_(capacity) {}

ParagraphLayoutCache::Metrics ParagraphLayoutCache::lookup(const Key& key, const std::function<Metrics()>& makeEntry) {
    {
        const std::lock_guard<std::mutex> guard(mutex_);

        for (const Entry& entry : currentFrame_) {
            if (entry.key == key) {
                ++hitCount_;

                return entry.metrics;
            }
        }

        for (const Entry& entry : previousFrame_) {
            if (entry.key == key) {
                ++hitCount_;

                // Promoted: the entry was unused last frame, so it moves into the current frame to survive the
                // next swap. The previous copy stays — the same string measured twice across the boundary still
                // only shapes once, and the previous frame's copy dies at the swap regardless.
                const Metrics metrics = entry.metrics;

                insertBounded(Entry{.key = key, .metrics = metrics});

                return metrics;
            }
        }

        ++missCount_;
    }

    // Outside the lock on purpose: this shapes a paragraph, which takes the layout mutex, and holding the cache
    // mutex across it would invert the two locks' order against a frame thread that holds the layout mutex.
    // Clang attributes a tail call's counter to the callee's entry line, so llvm-cov reports this one unhit
    // while `shapeAndInsert` itself is covered by every miss-path test.
    return shapeAndInsert(key, makeEntry); // COV_EXCL: tail-call line attribution, see above.
}

ParagraphLayoutCache::Metrics ParagraphLayoutCache::shapeAndInsert(const Key& key,
                                                                   const std::function<Metrics()>& makeEntry) {
    const Metrics shaped = makeEntry();

    const std::lock_guard<std::mutex> guard(mutex_);

    // Re-checked: a racing lookup of the same key may have shaped and inserted while this one was unlocked, and
    // its entry is as good as this one.
    for (const Entry& entry : currentFrame_) {
        if (entry.key == key) {
            return entry.metrics;
        }
    }

    insertBounded(Entry{.key = key, .metrics = shaped});

    return shaped;
}

void ParagraphLayoutCache::endFrame() {
    const std::lock_guard<std::mutex> guard(mutex_);

    // The current frame's untouched entries drop here: they survive exactly one frame of non-use, which is the
    // whole eviction policy — no LRU bookkeeping, no per-entry timestamps, just the swap.
    previousFrame_.clear();
    previousFrame_.swap(currentFrame_);
}

void ParagraphLayoutCache::insertBounded(Entry entry) {
    // The oldest entry leaves when a frame is at capacity, so a miss is always cached rather than silently
    // dropped once the capacity is reached, and the frame never grows past it.
    if (currentFrame_.size() == capacity_) {
        currentFrame_.erase(currentFrame_.begin());
    }

    currentFrame_.push_back(std::move(entry));
}

size_t ParagraphLayoutCache::currentFrameEntryCount() const {
    const std::lock_guard<std::mutex> guard(mutex_);

    return currentFrame_.size();
}

uint64_t ParagraphLayoutCache::hitCount() const {
    const std::lock_guard<std::mutex> guard(mutex_);

    return hitCount_;
}

uint64_t ParagraphLayoutCache::missCount() const {
    const std::lock_guard<std::mutex> guard(mutex_);

    return missCount_;
}

} // namespace react_native_linux
