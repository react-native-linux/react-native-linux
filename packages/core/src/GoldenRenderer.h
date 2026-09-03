#pragma once

#include <react/renderer/graphics/Point.h>

#include <string>

namespace react_native_linux {

/**
 * Renders one bundle's final scene into an offscreen raster surface and writes it as a PNG.
 *
 * This is the golden-image rig's producer. It is deliberately CPU-only: `SkSurfaces::Raster` needs no GPU, no
 * Vulkan driver and no Wayland compositor, which is what lets a golden run on a machine where screen capture is
 * permission-gated and inside a CI container with nothing graphical installed. It is the same `paintScene` the
 * window calls, so the golden describes the window's picture rather than a second implementation of it.
 *
 * The full-window golden — lavapipe software Vulkan under a headless compositor, exercising the swapchain path of
 * `SkiaVulkanRenderer` — is a separate rig and is not this one.
 *
 * Returns a process exit status: 0 when the file was written and the bundle reported no fatal JavaScript error.
 */
int renderGolden(const std::string& bundlePath, const std::string& outputPath, int width, int height);

/**
 * The same rig, after a mouse wheel has been turned `wheelNotches` times over `surfacePoint`.
 *
 * A `<ScrollView>` renders nothing a static golden could not already prove until it has been scrolled: the
 * translation of its children, the clip at its edges and the deceleration curve that decided where they stopped
 * are all properties of a scroll position other than zero. This is how one gets into a checked-in PNG. See
 * *ScrollView* in docs/cpp-toolchain.md.
 */
int renderScrollGolden(const std::string& bundlePath, const std::string& outputPath,
                       facebook::react::Point surfacePoint, int wheelNotches, int width, int height);

/**
 * The same rig, after Tab has been pressed `tabPresses` times.
 *
 * A focus ring is the one part of the focus model that is a picture rather than an event trace, and where it
 * lands after N presses is the traversal order, the wrap and the focusable filtering all at once. See
 * *Focus and keyboard* in docs/cpp-toolchain.md.
 */
int renderFocusGolden(const std::string& bundlePath, const std::string& outputPath, int tabPresses, int width,
                      int height);

/**
 * The same rig, after Tab has focused the fixture's `<TextInput>` and `keySequence` has been typed into it.
 *
 * A text field renders nothing a `<Text>` could not until something has been typed: the caret, the selection
 * highlight and the composing run's underline are all properties of an editing state, and this is how one gets
 * into a checked-in PNG. See *TextInput* in docs/cpp-toolchain.md.
 */
int renderTypedGolden(const std::string& bundlePath, const std::string& outputPath, const std::string& keySequence,
                      int width, int height);

/**
 * Runs a bundle that commits twice and proves that painting only the damaged region produces the same picture as
 * repainting everything.
 *
 * The first commit's scene is painted in full onto two surfaces. The second commit's scene is then painted over
 * one of them in full and over the other clipped to the damage the scene accumulated, which is exactly what the
 * window does per frame minus the swapchain. The two are compared byte by byte; a single differing pixel fails the
 * run with its coordinate, because a partial redraw that is nearly right is a partial redraw that is wrong. The
 * damage-clipped surface is what gets written, so the checked-in golden is the partial redraw rather than a full
 * one that happens to match it.
 *
 * Returns a process exit status: 0 when the two redraws matched, the file was written, and the bundle reported no
 * fatal JavaScript error.
 */
int renderDamageGolden(const std::string& bundlePath, const std::string& outputPath, int width, int height);

/**
 * Runs a bundle and proves that the node a press lands on is the node whose pixels are visible at that point.
 *
 * This is issue #35's acceptance criterion turned into an assertion, and the PNG is a by-product, exactly as it
 * is for the damage proof. The fixture paints every node in a colour no other node uses, so a pixel names the
 * node that painted it; the run sampled the live scene's hit test over a grid of the same surface; and the two
 * answers have to be the same node at every sample where the picture is unambiguous. A sample whose pixel is a
 * blend of two colours — the anti-aliased edge between two nodes, where "the node visible here" has no answer —
 * is skipped, and the run fails if too few samples survived that filter for the comparison to mean anything.
 *
 * The failure names the point, the colour that was painted there and the tag the hit test answered, because
 * "hit-testing disagrees with the picture" is not actionable and "at (312, 208) the picture is #61AFEF, which is
 * tag 7, and the press lands on tag 5" is.
 *
 * Returns a process exit status: 0 when every sample agreed, the file was written, and the bundle reported no
 * fatal JavaScript error.
 */
int renderHitPaintGolden(const std::string& bundlePath, const std::string& outputPath, int width, int height);

/**
 * Runs a bundle and proves that every box a paragraph was measured into still holds the paragraph that is painted
 * in it, and that measuring it again answers the same thing.
 *
 * Issue #41's failure is a box measured for one paragraph and painted with another. Measurement and painting
 * cannot shape a string differently here — they call one `layoutParagraph` — but they are called with different
 * widths: measurement is given Yoga's constraint, painting is given the content box Yoga assigned from the
 * answer. A paragraph that re-wraps at that second width overflows its box, which is react-native-macos#2857.
 *
 * So for every text node in the scene this lays the paragraph out at the content-box width the painter uses, and
 * fails when it is taller or wider than that box. It then measures the same string through `TextLayoutManager`
 * twice — the second call is a cache hit — and fails when the two answers differ, or when they disagree with the
 * paragraph that was painted.
 *
 * Returns a process exit status: 0 when every paragraph fitted, the file was written, and the bundle reported no
 * fatal JavaScript error.
 */
int renderTextFitGolden(const std::string& bundlePath, const std::string& outputPath, int width, int height);

} // namespace react_native_linux
