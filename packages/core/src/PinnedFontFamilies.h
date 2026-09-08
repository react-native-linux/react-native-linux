#pragma once

#include <optional>
#include <string>
#include <vector>

namespace react_native_linux {

/**
 * One family `TextPipeline.cpp`'s family list names, paired with whether the asset font manager
 * (`SkFontMgr_New_Custom_Directory(RNL_BUNDLED_FONT_DIR)`) resolved it — a non-empty `matchFamily` result.
 */
struct PinnedFontFamilyResolution {
    std::string family;
    bool resolvedFromAssetManager;
};

/**
 * The fatal diagnostic for #314, naming every pinned family the asset manager did not resolve, or
 * `std::nullopt` when all of them did.
 *
 * `TextPipeline.cpp`'s `TextPipelineState` calls this once, at construction, with the real
 * `SkFontMgr::matchFamily` results for `kBundledFontFamily` and `kEmojiFontFamily` — the same two names
 * `scripts/fonts.lock.json` pins. Pulled into its own Skia-free file, per #307/#314: a missing vendored face
 * used to resolve silently through fontconfig's system font instead of failing, which is the #70 diagnostic's
 * sibling for "resolved, but from the wrong source" rather than "did not resolve at all". Kept pure so the
 * message text is table-tested against a fake resolution list without a live `SkFontMgr`.
 */
std::optional<std::string> pinnedFontFamiliesFatalMessage(const std::vector<PinnedFontFamilyResolution>& resolutions);

/**
 * Whether a resolved face's style is exactly the upright, normal-weight, normal-width style
 * `FontFamilyRequestKind::VendoredDefault` requests and `scripts/fonts.lock.json` pins `NotoSans-Regular.ttf`
 * for — Skia's own `SkFontStyle()` default encoding (`weight`/`width`/`slant` mirror
 * `SkFontStyle::weight()`/`width()`/`slant()`; normal is 400/5/0). `matchFamily(...)->count() > 0` alone passes
 * as long as *any* style resolves, and `SkFontMgr::matchFamilyStyle`'s nearest-match fallback does not enforce
 * an exact style either: with `NotoSans-Regular.ttf` missing and only `NotoSans-Bold.ttf`/`NotoSans-Italic.ttf`
 * left, it silently returns the italic face — weight 400, but slant 1, not upright — and a family-only or
 * weight-only check would still call that resolved while every plain-weight, upright text run drew italic.
 * Kept pure and Skia-free so the wrong-face regression (#372/CodeRabbit) is a table test against fake style
 * data, not a live `SkFontMgr`.
 */
bool resolvedStyleIsPinnedDefault(int weight, int width, int slant);

} // namespace react_native_linux
