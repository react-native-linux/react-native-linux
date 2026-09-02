#pragma once

#include <react/renderer/graphics/BackgroundImage.h>
#include <react/renderer/graphics/Rect.h>

#include "include/core/SkRefCnt.h"
#include "include/core/SkShader.h"

namespace react_native_linux {

/**
 * The Skia shader one CSS `background-image` layer fills `frame` with, in the absolute surface coordinates the
 * painter already draws that node's rounded border box in.
 *
 * `nullptr` when the layer cannot produce a ramp: no colour stops, or a frame with no area. The painter skips
 * those layers rather than filling them with an undefined colour.
 *
 * The CSS formulas — the gradient line, the colour-stop fix-up, and the radial sizing keywords — are ports of
 * React Native's own Android implementation, which is itself a port of Blink's. See *Gradients* in
 * docs/cpp-toolchain.md.
 */
sk_sp<SkShader> makeGradientShader(const facebook::react::BackgroundImage& backgroundImage,
                                   const facebook::react::Rect& frame);

} // namespace react_native_linux
