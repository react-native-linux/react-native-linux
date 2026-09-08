#pragma once

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <react/renderer/attributedstring/primitives.h>

namespace react_native_linux {

/**
 * Which end of the text `ellipsizeMode` takes away. `tail` is not here: SkParagraph truncates at the tail itself,
 * through `ParagraphStyle::setEllipsis`, and `clip` takes nothing away at all.
 */
enum class EllipsizeSide { Head, Middle };

/**
 * A run of one fragment's string that survives the truncation, as byte offsets into that fragment.
 */
struct EllipsizePiece {
    size_t fragmentIndex;
    size_t beginUtf8;
    size_t endUtf8;
};

/**
 * What to rebuild the attributed string from: the pieces before the ellipsis, the pieces after it, and the
 * fragment whose style the ellipsis itself is drawn in.
 *
 * `head` leaves `leadingPieces` empty, `middle` fills both. The ellipsis belongs to the fragment holding the
 * first byte the truncation removes, which is react/react-native#37926: an ellipsis drawn in the paragraph's
 * base style rather than in the style of the run it stands for.
 */
struct EllipsizePlan {
    std::vector<EllipsizePiece> leadingPieces;
    std::vector<EllipsizePiece> trailingPieces;
    size_t ellipsisFragmentIndex;
};

/**
 * The plan that keeps `keptGraphemeCount` graphemes of the concatenated fragment strings — the last ones for
 * `head`, and half at each end (the odd one going to the front, as iOS and Android both place it) for `middle`.
 *
 * `graphemeStarts` is the byte offset of every grapheme in the concatenation plus its total length, exactly what
 * `TextSegments::graphemeStarts` carries. Every cut lands on one of those offsets, so no plan can ever split a
 * cluster, a surrogate pair or a multi-byte sequence: the byte offsets it produces are grapheme boundaries or
 * they are fragment boundaries, and a fragment boundary is already a grapheme boundary in an attributed string.
 *
 * The search is over the *logical* order of the text, not the visual one. For a right-to-left run that means the
 * ellipsis stands where the removed characters were in memory, which is not where iOS puts it on screen; the
 * paragraph direction is hardcoded left-to-right anyway (see *Fidelity limits* in docs/cpp-toolchain.md).
 */
/**
 * The facts about a paragraph that decide whether a `head` or `middle` cut may be searched for it at all.
 *
 * Two of them are refusals rather than settings. A paragraph carrying an **inline attachment** is left alone
 * because `ParagraphShadowNode::layout` requires one measured attachment per attachment fragment and
 * `TextLayoutManager` pairs them with `getRectsForPlaceholders` in order, a pairing that only survives drops at
 * the tail. An **editor field** is left alone because a `<TextInput>` is a window onto its whole text, not a
 * truncated view of it: its caret, selection, composing run and hit testing are all UTF-16 offsets into the
 * string React gave us, and a searched cut rebuilds that string, so every one of those offsets would address a
 * different character than the one on screen. A field scrolls its text instead, which is what makes those
 * offsets keep meaning what they say.
 */
struct EllipsizeCandidate {
    facebook::react::EllipsizeMode ellipsizeMode;
    int maximumNumberOfLines;
    bool hasInlineAttachment;
    bool isEditorField;
};

/**
 * Which end of the text this paragraph's `ellipsizeMode` takes away, or nothing at all when the mode is one
 * SkParagraph answers itself (`tail`, `clip`), when there is no line limit, or when the paragraph is one of the
 * two the search must not rebuild.
 */
std::optional<EllipsizeSide> searchedEllipsizeSide(const EllipsizeCandidate& candidate);

EllipsizePlan planEllipsize(EllipsizeSide side, const std::vector<std::string>& fragmentStrings,
                            const std::vector<size_t>& graphemeStarts, size_t keptGraphemeCount);

/**
 * The widest plan that still fits, found by bisecting the grapheme count.
 *
 * `fits` measures a candidate plan — in the renderer it lays the rebuilt string out with the same shaper the
 * paragraph is drawn with and asks whether it still exceeds the line limit. Dropping a grapheme can never turn a
 * paragraph that fits into one that does not, so the predicate is monotonic in the kept count and a bisection
 * over it is exact: `log2(graphemes)` measurements rather than one per grapheme.
 *
 * When even the ellipsis alone does not fit, the plan that keeps nothing is returned rather than an error, which
 * is what leaves a box too narrow for one glyph showing an ellipsis instead of showing nothing.
 */
EllipsizePlan searchEllipsizePlan(EllipsizeSide side, const std::vector<std::string>& fragmentStrings,
                                  const std::vector<size_t>& graphemeStarts,
                                  const std::function<bool(const EllipsizePlan&)>& fits);

} // namespace react_native_linux
