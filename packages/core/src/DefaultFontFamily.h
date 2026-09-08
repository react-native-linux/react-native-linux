#pragma once

#include <string>

namespace react_native_linux {

/**
 * Where a `fontFamily` request is resolved from, per #372.
 *
 * `VendoredDefault` is an unset `fontFamily`, `sans-serif` and `system-ui`: React Native's own unspecified
 * default and the two CSS spellings of "the platform's interface font". All three are asked for directly from
 * the vendored Noto Sans in `packages/core/fonts` rather than from fontconfig, because a host's own answer for
 * `sans-serif` can be wrong — electron/electron#53499 is an entire KDE Wayland interface rendered in monospace
 * because the generic family the engine asked for resolved to the wrong face. Resolving from the vendored asset
 * instead means the interface never depends on the host's alias being correct, and a golden built against it
 * stays reproducible across hosts.
 *
 * `FontconfigGeneric` is `serif`, `monospace`, `cursive` and `fantasy`: fontconfig's own answer for the generic,
 * which is the point of asking for one rather than a failure to find it — the exemption `isGenericFamily`
 * establishes for the #70 diagnostic in `TextPipeline.cpp`. Unlike `VendoredDefault`, there is no vendored serif
 * or monospace face to prefer, so the host's own answer is the correct one and is logged once rather than
 * substituted.
 *
 * `Named` is anything else: a family an application asked for by name, covered by the #70 diagnostic. The text
 * still draws — substituted by the next family in the list — and the substitution is reported once per name,
 * including for a family nobody registered at all.
 */
enum class FontFamilyRequestKind {
    VendoredDefault,
    FontconfigGeneric,
    Named,
};

/**
 * Classifies a requested `fontFamily` into the source #372 resolves it from. Pure and Skia-free so the table is
 * asserted directly, under the 100% coverage gate, against every generic CSS keyword `<Text>` can carry.
 */
FontFamilyRequestKind classifyFontFamilyRequest(const std::string& fontFamily);

} // namespace react_native_linux
