#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {

/**
 * The paragraph-layout cache of #342, in GPUI's two-frame shape (`crates/gpui/src/text_system/line_layout.rs:454`)
 * reduced to what our measure path actually re-asks for: `measureParagraphMetrics` re-shapes an unchanged string
 * on every measure pass, and a scrolled list of unchanged rows re-measures on every frame.
 *
 * The key is the display text, the paragraph-attribute signature and the wrap width. Colour is deliberately
 * excluded — the metrics answer no colour question, so re-tinting a row reuses its cached measurement, exactly
 * as GPUI's key excludes colour so re-tinting reuses the layout.
 *
 * The two-frame swap is the whole eviction policy: a hit in the current frame is a hit; a hit in the previous
 * frame is promoted into it; a frame end swaps and drops everything the current frame never touched, so an
 * entry survives exactly one frame of non-use. `endFrame` is called by the frame thread once per frame; the
 * cache is otherwise read from whatever thread measures.
 *
 * Pure and lib-counting: the caller supplies the metrics through `makeEntry`, so the cache never touches Skia,
 * and that is what puts it under the 100% gate without a Skia fixture. Threads: reads and hits race
 * `endFrame`'s swap only in the sense GPUI's does — the contract below states it, and the TSan job runs the
 * concurrent case.
 *
 * Threading contract: `lookup` is called from the commit thread or the frame thread; `endFrame` from the frame
 * thread between frames. The frame-index guard in `lookup` makes a hit on the previous frame promote instead of
 * read, and a `std::mutex` guards the two deques; the lock is per cache, and the layout mutex it reduces
 * contention on is the one `TextPipelineState` holds for the whole shape.
 */
class ParagraphLayoutCache final {
public:
    struct Key {
        std::string text;
        std::string attributes;
        float maximumWidth{0.0F};

        bool operator==(const Key&) const = default;
    };

    struct Metrics {
        float longestLineWidth{0.0F};
        float height{0.0F};
        /** Per line, as the measure path reports them: `.first` the width, `.second` the height. */
        std::vector<std::pair<float, float>> lines;

        bool operator==(const Metrics&) const = default;
    };

    explicit ParagraphLayoutCache(size_t capacity);

    /**
     * The cached metrics for `key`, or the result of `makeEntry` — which the caller pays the shape for — stored
     * into the current frame and returned. `makeEntry` is invoked at most once per (frame, key) pair, never
     * while the current frame already holds the key.
     */
    Metrics lookup(const Key& key, const std::function<Metrics()>& makeEntry);

    /** Swaps current to previous and clears current, dropping every entry that just went unused. */
    void endFrame();

    [[nodiscard]] size_t currentFrameEntryCount() const noexcept;
    [[nodiscard]] uint64_t hitCount() const noexcept;
    [[nodiscard]] uint64_t missCount() const noexcept;

private:
    struct Entry {
        Key key;
        Metrics metrics;
    };

    size_t capacity_;
    std::vector<Entry> currentFrame_;
    std::vector<Entry> previousFrame_;
    uint64_t frameIndex_{0};
    uint64_t hitCount_{0};
    uint64_t missCount_{0};
};

} // namespace react_native_linux
