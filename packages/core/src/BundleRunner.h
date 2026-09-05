#pragma once

#include "LinuxMountingManager.h"
#include "RetainedScene.h"

#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Size.h>

#include <optional>
#include <string>

namespace react_native_linux {

enum class BundleMode {
    Script,
    Fabric,
};

/**
 * What a headless Fabric run leaves behind once the JavaScript thread has gone quiet and the surface has been
 * stopped: the scene as a flat list of absolute rectangles, the same scene as the human-readable dump, and whether
 * the run reported a fatal JavaScript error.
 *
 * Both are captured before teardown because stopping the surface commits an empty tree.
 */
struct FabricRunResult {
    SceneSnapshot scene;
    std::string sceneDump;
    bool hasReportedFatalError{};
};

/**
 * What a headless run of a bundle that commits twice leaves behind: the scene as it stood after the first commit,
 * the scene after the second, and the damage the second commit accumulated. Everything a partial redraw needs, and
 * everything an equivalence proof needs to compare it against a full one.
 *
 * `failure` is empty when the run produced all three. A bundle that never commits, or that commits only once,
 * fills it instead of leaving a caller to guess why the damage is empty.
 */
struct FabricDamageRunResult {
    SceneSnapshot firstScene;
    SceneSnapshot secondScene;
    SceneDamage damage;
    std::string failure;
    bool hasReportedFatalError{};
};

int runBundle(const std::optional<std::string>& bundlePath, BundleMode bundleMode);
int runInjectedClick(const std::string& bundlePath, facebook::react::Point surfacePoint);

/**
 * Runs a bundle, turns `wheelNotches` of a mouse wheel over `surfacePoint` into a scroll, and drives the whole
 * glide one frame at a time in the window loop's order: dispatch, `advanceScroll` — which pushes the animation
 * backend for the frame it dispatched an `onScroll` in — beat, animation tick.
 *
 * This is the acceptance harness for issue #131, and the beat is induced per frame rather than once at the end,
 * because what is being proved is per-frame agreement between the scroll offset and the animated value it drives.
 * `--scroll-to`, which only cares where the content came to rest, deliberately does the opposite. See
 * *Event-driven Animated* in docs/cpp-toolchain.md.
 */
int runAnimatedScroll(const std::string& bundlePath, facebook::react::Point surfacePoint, int wheelNotches);

/**
 * Runs a bundle at the headless surface size, then resizes the surface to `resizedSurfaceSize` the way a
 * compositor's `xdg_toplevel.configure` would, and lets JavaScript observe both.
 *
 * This is the acceptance harness for issue #50: it is the only path that reaches `didUpdateDimensions` without a
 * compositor, so a bundle can print the constants it booted with and the change event that followed. The window
 * rig cannot drive a resize from outside the compositor, and a `Dimensions` bug is arithmetic rather than pixels.
 */
int runResizedFabricBundle(const std::string& bundlePath, facebook::react::Size resizedSurfaceSize);

FabricRunResult runFabricBundle(const std::optional<std::string>& bundlePath, facebook::react::Size surfaceSize);

/**
 * One sampled point and the node a press there would land on.
 */
struct FabricHitSample {
    facebook::react::Point point;
    facebook::react::Tag tag;
};

/**
 * What the hit-versus-paint proof of issue #35 needs: the scene, and the hit test's answer at every point of a
 * grid over the surface, taken while the host is still alive.
 *
 * The answers are collected here rather than recomputed from the snapshot because a snapshot is the paint side.
 * Asking the live scene is asking the same thing the input dispatcher asks, and the caller compares those answers
 * against the pixels the same scene rasterises to.
 */
struct FabricHitPaintRunResult {
    SceneSnapshot scene;
    std::vector<FabricHitSample> hits;
    bool hasReportedFatalError{};
};

FabricHitPaintRunResult runHitSampledFabricBundle(const std::string& bundlePath, facebook::react::Size surfaceSize,
                                                  int sampleStep);

/**
 * Runs a bundle, turns `wheelNotches` of a mouse wheel over `surfacePoint` into a scroll, and returns the scene
 * once that scroll has come to rest.
 *
 * The physics is integrated at a fixed 60 Hz step rather than at whatever rate this machine happens to loop at, so
 * the settled position is a property of the notch count and the deceleration curve and of nothing else. That is
 * what makes a scrolled golden reproducible; see *ScrollView* in docs/cpp-toolchain.md.
 */
FabricRunResult runScrolledFabricBundle(const std::string& bundlePath, facebook::react::Size surfaceSize,
                                        facebook::react::Point surfacePoint, int wheelNotches);

/**
 * Runs a bundle and presses Tab `tabPresses` times, one press per frame, through the same `InputQueue` and
 * `InputDispatcher` a window uses — so what lands on the Nth focusable is the traversal order and the focusable
 * filtering rather than a list this function built.
 *
 * The returned scene carries the focus ring, because a keyboard-driven focus is the case that draws one. See
 * *Focus and keyboard* in docs/cpp-toolchain.md.
 */
FabricRunResult runFocusTabbedFabricBundle(const std::string& bundlePath, facebook::react::Size surfaceSize,
                                           int tabPresses);

/**
 * Runs a bundle, presses Tab once to land on the first focusable — which the fixture makes the `<TextInput>` —
 * and then types `keySequence` through the same `InputQueue` and `InputDispatcher` a window uses.
 *
 * The sequence is `parseKeySequence`'s notation: literal characters, `{Left}`, `{Shift+Left}`, `{Ctrl+A}`,
 * `{Backspace}`, `{Enter}` and the composition tokens `{Preedit:...}` and `{Commit:...}`. Every event is
 * delivered on its own frame, so what reaches JavaScript is the ordered trace a real typing session produces.
 *
 * The caret blink is never advanced, so the caret in the returned scene is always in its visible phase and a
 * checked-in picture of it is reproducible. See *TextInput* in docs/cpp-toolchain.md.
 */
FabricRunResult runTypedFabricBundle(const std::string& bundlePath, facebook::react::Size surfaceSize,
                                     const std::string& keySequence);
FabricDamageRunResult runFabricBundleAcrossCommits(const std::string& bundlePath, facebook::react::Size surfaceSize);

/**
 * The scene at the first commit and the scene once everything that settles late has settled — timers drained,
 * image decodes finished, the headless frames run — with nothing required to have changed between them.
 *
 * This is the shape of react-native-macos#2857 turned into a pair of snapshots: a scrollable whose content size
 * came from a measurement that was not yet final shows up as geometry that is different at the second snapshot
 * from what it was at the first. `runFabricBundleAcrossCommits` is the same pair with the opposite expectation,
 * that a second commit *did* change something; this one exists because "nothing moved" is an assertion too.
 */
struct FabricFrameRunResult {
    SceneSnapshot firstScene;
    SceneSnapshot settledScene;
    bool hasReportedFatalError{};
};

FabricFrameRunResult runFabricBundleAcrossFrames(const std::string& bundlePath, facebook::react::Size surfaceSize);

} // namespace react_native_linux
