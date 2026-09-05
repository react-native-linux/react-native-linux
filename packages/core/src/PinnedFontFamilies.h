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
std::optional<std::string> pinnedFontFamiliesFatalMessage(
    const std::vector<PinnedFontFamilyResolution>& resolutions);

}  // namespace react_native_linux
