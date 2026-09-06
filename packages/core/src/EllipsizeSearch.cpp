#include "EllipsizeSearch.h"

#include <algorithm>

namespace react_native_linux {

namespace {

constexpr size_t kSmallestSearchableGraphemeStarts = 2;

std::vector<EllipsizePiece> piecesInRange(const std::vector<std::string>& fragmentStrings, size_t beginUtf8,
                                          size_t endUtf8) {
    std::vector<EllipsizePiece> pieces;
    size_t fragmentBeginUtf8 = 0;

    for (size_t fragmentIndex = 0; fragmentIndex < fragmentStrings.size(); ++fragmentIndex) {
        const size_t fragmentEndUtf8 = fragmentBeginUtf8 + fragmentStrings[fragmentIndex].size();
        const size_t pieceBeginUtf8 = std::max(beginUtf8, fragmentBeginUtf8);
        const size_t pieceEndUtf8 = std::min(endUtf8, fragmentEndUtf8);

        if (pieceBeginUtf8 < pieceEndUtf8) {
            pieces.push_back(EllipsizePiece{.fragmentIndex = fragmentIndex,
                                            .beginUtf8 = pieceBeginUtf8 - fragmentBeginUtf8,
                                            .endUtf8 = pieceEndUtf8 - fragmentBeginUtf8});
        }

        fragmentBeginUtf8 = fragmentEndUtf8;
    }

    return pieces;
}

/**
 * The fragment holding `offsetUtf8`, clamped to the last fragment when the offset is the end of the text and to
 * zero when there are no fragments at all, so the ellipsis always has a style to take.
 */
size_t fragmentIndexAtOffset(const std::vector<std::string>& fragmentStrings, size_t offsetUtf8) {
    size_t fragmentIndex = 0;
    size_t fragmentBeginUtf8 = 0;

    while (fragmentIndex + 1 < fragmentStrings.size() &&
           fragmentBeginUtf8 + fragmentStrings[fragmentIndex].size() <= offsetUtf8) {
        fragmentBeginUtf8 += fragmentStrings[fragmentIndex].size();
        ++fragmentIndex;
    }

    return fragmentIndex;
}

} // namespace

std::optional<EllipsizeSide> searchedEllipsizeSide(const EllipsizeCandidate& candidate) {
    if (candidate.maximumNumberOfLines <= 0 || candidate.hasInlineAttachment || candidate.isEditorField) {
        return std::nullopt;
    }

    if (candidate.ellipsizeMode == facebook::react::EllipsizeMode::Head) {
        return EllipsizeSide::Head;
    }

    if (candidate.ellipsizeMode == facebook::react::EllipsizeMode::Middle) {
        return EllipsizeSide::Middle;
    }

    return std::nullopt;
}

EllipsizePlan planEllipsize(EllipsizeSide side, const std::vector<std::string>& fragmentStrings,
                            const std::vector<size_t>& graphemeStarts, size_t keptGraphemeCount) {
    if (graphemeStarts.size() < kSmallestSearchableGraphemeStarts) {
        return EllipsizePlan{.leadingPieces = {}, .trailingPieces = {}, .ellipsisFragmentIndex = 0};
    }

    const size_t totalGraphemes = graphemeStarts.size() - 1;
    const size_t keptGraphemes = std::min(keptGraphemeCount, totalGraphemes);
    const size_t leadingGraphemes = side == EllipsizeSide::Middle ? (keptGraphemes + 1) / 2 : 0;
    const size_t leadingEndUtf8 = graphemeStarts[leadingGraphemes];
    const size_t trailingBeginUtf8 = graphemeStarts[totalGraphemes - (keptGraphemes - leadingGraphemes)];

    return EllipsizePlan{.leadingPieces = piecesInRange(fragmentStrings, 0, leadingEndUtf8),
                         .trailingPieces =
                             piecesInRange(fragmentStrings, trailingBeginUtf8, graphemeStarts[totalGraphemes]),
                         .ellipsisFragmentIndex = fragmentIndexAtOffset(fragmentStrings, leadingEndUtf8)};
}

EllipsizePlan searchEllipsizePlan(EllipsizeSide side, const std::vector<std::string>& fragmentStrings,
                                  const std::vector<size_t>& graphemeStarts,
                                  const std::function<bool(const EllipsizePlan&)>& fits) {
    size_t keptGraphemes = 0;
    size_t widestKeptGraphemes =
        graphemeStarts.size() < kSmallestSearchableGraphemeStarts ? 0 : graphemeStarts.size() - 1;

    while (keptGraphemes < widestKeptGraphemes) {
        const size_t candidateGraphemes = keptGraphemes + ((widestKeptGraphemes - keptGraphemes + 1) / 2);

        if (fits(planEllipsize(side, fragmentStrings, graphemeStarts, candidateGraphemes))) {
            keptGraphemes = candidateGraphemes;
        } else {
            widestKeptGraphemes = candidateGraphemes - 1;
        }
    }

    return planEllipsize(side, fragmentStrings, graphemeStarts, keptGraphemes);
}

} // namespace react_native_linux
