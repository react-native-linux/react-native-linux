#include "AnimatedPropAllowlist.h"
#include "LinuxMountingManager.h"
#include "SceneTestSupport.h"

#include <gtest/gtest.h>

#include <folly/dynamic.h>
#include <react/renderer/animated/NativeAnimatedNodesManager.h>
#include <react/renderer/animated/internal/NativeAnimatedAllowlist.h>
#include <react/renderer/core/ReactPrimitives.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Issue #122: the set of props the Linux native driver can animate is written down once, in `kAnimatableProps`,
// and this file is the boundary that keeps it honest in both directions. Upstream's
// `getDirectManipulationAllowlist()` is what the JavaScript side advertises to the native driver, so a prop we
// accept that is not on it is a claim JavaScript never made, and a prop on it that we do not paint is a gap that
// has to be enumerated rather than discovered at runtime. A React Native version bump that changes either side
// fails here rather than in an app. See *Native-driver allowlist* in docs/cpp-toolchain.md.

namespace {

using facebook::react::NativeAnimatedNodesManager;
using react_native_linux::AnimatableProp;
using react_native_linux::AnimatablePropEntry;
using react_native_linux::AnimatedPropRejection;
using react_native_linux::kAnimatableProps;
using react_native_linux::MountDiagnostics;

constexpr Tag kAnimatedTag = 2;
constexpr Tag kValueNodeTag = 101;
constexpr Tag kStyleNodeTag = 102;
constexpr Tag kPropsNodeTag = 103;

// Every prop upstream advertises that this platform does not paint on the fast path, in sorted order. Border and
// shadow colours live inside `BorderMetrics`, which `resolveBorderMetrics` cascades out of nine props, so a
// single animated edge is a re-cascade rather than a field write; the radii, `elevation` and `zIndex` are the
// same story for a different struct; the legacy `scaleX`/`translateY` style operations are Android's pre-Fabric
// spelling of a transform. None of them are refusals in principle — each is a paintable prop nobody has needed
// yet, and moving one into `kAnimatableProps` means deleting its line here.
std::vector<std::string> expectedUnpaintedUpstreamProps() {
    return {"borderBottomColor",       "borderBottomEndRadius", "borderBottomLeftRadius",
            "borderBottomRightRadius", "borderBottomStartRadius", "borderColor",
            "borderEndColor",          "borderEndEndRadius",    "borderEndStartRadius",
            "borderLeftColor",         "borderRadius",          "borderRightColor",
            "borderStartColor",        "borderStartEndRadius",  "borderStartStartRadius",
            "borderTopColor",          "borderTopEndRadius",    "borderTopLeftRadius",
            "borderTopRightRadius",    "borderTopStartRadius",  "color",
            "elevation",               "scaleX",                "scaleY",
            "shadowOpacity",           "shadowRadius",          "tintColor",
            "translateX",              "translateY",            "zIndex"};
}

std::vector<std::string> upstreamPropsWeDoNotPaint() {
    std::vector<std::string> difference;

    for (const std::string& upstreamProp : facebook::react::getDirectManipulationAllowlist()) {
        if (!react_native_linux::animatablePropFor(upstreamProp).has_value()) {
            difference.push_back(upstreamProp);
        }
    }

    std::sort(difference.begin(), difference.end());

    return difference;
}

TEST(AnimatedPropAllowlistTest, EveryPropWePaintIsOneTheJavaScriptSideAdvertises) {
    for (const AnimatablePropEntry& entry : kAnimatableProps) {
        EXPECT_TRUE(facebook::react::getDirectManipulationAllowlist().contains(std::string{entry.name}))
            << entry.name << " is not in upstream's direct-manipulation allowlist";
    }
}

TEST(AnimatedPropAllowlistTest, EveryAdvertisedPropWeDoNotPaintIsAnEnumeratedDifference) {
    EXPECT_EQ(upstreamPropsWeDoNotPaint(), expectedUnpaintedUpstreamProps());
}

TEST(AnimatedPropAllowlistTest, TheTableIsWhatDecidesWhetherTheFastPathAppliesAProp) {
    EXPECT_EQ(react_native_linux::animatablePropFor("opacity"), AnimatableProp::Opacity);
    EXPECT_EQ(react_native_linux::animatablePropFor("backgroundColor"), AnimatableProp::BackgroundColor);
    EXPECT_EQ(react_native_linux::animatablePropFor("transform"), AnimatableProp::Transform);
    EXPECT_FALSE(react_native_linux::animatablePropFor("borderRadius").has_value());
    EXPECT_FALSE(react_native_linux::animatablePropFor("").has_value());
}

TEST(AnimatedPropAllowlistTest, TheUnsupportedPropDiagnosticNamesThePropTheSupportedSetAndTheWayOut) {
    EXPECT_EQ(react_native_linux::rejectedAnimatedPropMessage("shadowRadius", AnimatedPropRejection::Unsupported),
              "[mounting] synchronous update carries prop shadowRadius, which the Linux native driver cannot "
              "animate; supported: opacity, backgroundColor, transform — animate it with useNativeDriver: false "
              "or file an issue");
}

TEST(AnimatedPropAllowlistTest, TheNonFinitePropDiagnosticNamesTheInterpolationThatProducedIt) {
    EXPECT_EQ(react_native_linux::rejectedAnimatedPropMessage("opacity", AnimatedPropRejection::NonFinite),
              "[mounting] synchronous update carries prop opacity with a non-finite value, which the Linux native "
              "driver cannot paint; supported: opacity, backgroundColor, transform — check the interpolation "
              "output range or animate it with useNativeDriver: false");
}

// The #73 boundary rule applied to animation, driven through the real graph rather than a hand-written payload:
// a value node holding NaN reaches `synchronouslyUpdateViewOnUIThread` as `opacity: NaN`, because nothing between
// the node and the callback rejects it — `std::clamp` propagates NaN through both of its comparisons — and the
// alpha channel of every colour the subtree paints is the next thing that would multiply by it. It is refused at
// the boundary and counted with the props the driver may send and this cannot paint.
TEST(AnimatedPropAllowlistTest, ANonFiniteOpacityFromTheRealNodesManagerIsRejectedAndCounted) {
    LinuxMountingManager mountingManager;

    mountChildAndTakeFrame(mountingManager, makePaintedView(kAnimatedTag, makeRect(0, 0, 100, 100), blue()));

    const std::shared_ptr<NativeAnimatedNodesManager> nodesManager = std::make_shared<NativeAnimatedNodesManager>(
        [&mountingManager](Tag tag, const folly::dynamic& props) {
            mountingManager.synchronouslyUpdateViewOnUIThread(tag, props);
        },
        [](std::unordered_map<Tag, folly::dynamic>& /*updates*/) {}, nullptr);
    NativeAnimatedNodesManager* manager = nodesManager.get();

    nodesManager->scheduleOnUI([manager]() {
        manager->createAnimatedNode(
            kValueNodeTag,
            folly::dynamic::object("type", "value")("value", std::numeric_limits<double>::quiet_NaN())("offset", 0));
        manager->createAnimatedNode(
            kStyleNodeTag,
            folly::dynamic::object("type", "style")("style", folly::dynamic::object("opacity", kValueNodeTag)));
        manager->createAnimatedNode(
            kPropsNodeTag,
            folly::dynamic::object("type", "props")("props", folly::dynamic::object("style", kStyleNodeTag)));
        manager->connectAnimatedNodes(kValueNodeTag, kStyleNodeTag);
        manager->connectAnimatedNodes(kStyleNodeTag, kPropsNodeTag);
        manager->connectAnimatedNodeToView(kPropsNodeTag, kAnimatedTag);
    });

    nodesManager->onRender();

    const MountDiagnostics diagnostics = mountingManager.mountDiagnostics();

    EXPECT_EQ(diagnostics.rejectedAnimatedProps, 1U);
    EXPECT_EQ(diagnostics.firstRejectedAnimatedProp, "opacity");
    EXPECT_EQ(mountingManager.snapshotScene().at(0).backgroundColorArgb, kBlueArgb);
}

} // namespace
