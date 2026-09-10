#include "ParagraphLayoutCache.h"

#include <utility>

namespace react_native_linux {

ParagraphLayoutCache::ParagraphLayoutCache(size_t capacity) : capacity_(capacity) {}

ParagraphLayoutCache::Metrics ParagraphLayoutCache::lookup(const Key& key, const std::function<Metrics()>& makeEntry) {
    for (const Entry& entry : currentFrame_) {
        if (entry.key == key) {
            ++hitCount_;

            return entry.metrics;
        }
    }

    for (Entry& entry : previousFrame_) {
        if (entry.key == key) {
            ++hitCount_;

            // Promoted: the entry was unused last frame, so it moves into the current frame to survive the
            // next swap. The previous copy stays — the same string measured twice across the boundary still
            // only shapes once, and the previous frame's copy dies at the swap regardless.
            const Metrics metrics = entry.metrics;

            if (currentFrame_.size() < capacity_) {
                currentFrame_.push_back(Entry{.key = key, .metrics = metrics});
            }

            return metrics;
        }
    }

    ++missCount_;

    const Metrics shaped = makeEntry();

    if (currentFrame_.size() < capacity_) {
        currentFrame_.push_back(Entry{.key = key, .metrics = shaped});
    }

    return shaped;
}

void ParagraphLayoutCache::endFrame() {
    // The current frame's untouched entries drop here: they survive exactly one frame of non-use, which is the
    // whole eviction policy — no LRU bookkeeping, no per-entry timestamps, just the swap.
    previousFrame_.clear();
    previousFrame_.swap(currentFrame_);
}

size_t ParagraphLayoutCache::currentFrameEntryCount() const noexcept { return currentFrame_.size(); }

uint64_t ParagraphLayoutCache::hitCount() const noexcept { return hitCount_; }

uint64_t ParagraphLayoutCache::missCount() const noexcept { return missCount_; }

} // namespace react_native_linux
