#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace react_native_linux {

/**
 * What the Linux native driver can paint without a Fabric commit.
 *
 * This enumeration and `kAnimatableProps` are the one place that set is written down.
 * `RetainedScene::applyAnimatedProps` switches over it, so a prop cannot be painted on the fast path without an
 * entry here, and `rejectedAnimatedPropMessage` names it, so the diagnostic a user reads cannot drift from what
 * the fast path actually applies.
 *
 * It is a subset of what the JavaScript side advertises: upstream's `getDirectManipulationAllowlist()` in
 * `react/renderer/animated/internal/NativeAnimatedAllowlist.h` is the oracle, and
 * `packages/core/tests/AnimatedPropAllowlistTest.cpp` holds this table against it in both directions — nothing
 * here may be absent from upstream's list, and everything on upstream's list that this does not paint is an
 * enumerated, expected difference. See *Native-driver allowlist* in docs/cpp-toolchain.md.
 */
enum class AnimatableProp : std::uint8_t {
    Opacity,
    BackgroundColor,
    Transform,
};

struct AnimatablePropEntry {
    std::string_view name;
    AnimatableProp prop;
};

inline constexpr std::array<AnimatablePropEntry, 3> kAnimatableProps{{
    {"opacity", AnimatableProp::Opacity},
    {"backgroundColor", AnimatableProp::BackgroundColor},
    {"transform", AnimatableProp::Transform},
}};

constexpr std::optional<AnimatableProp> animatablePropFor(std::string_view propName) {
    for (const AnimatablePropEntry& entry : kAnimatableProps) {
        if (entry.name == propName) {
            return entry.prop;
        }
    }

    return std::nullopt;
}

/**
 * Why the fast path dropped a prop an animation frame carried: it is outside `kAnimatableProps`, or its value is
 * not a finite number. The second is issue #73's boundary rule applied to animation — a `NaN` that reaches the
 * scene is an alpha channel and a matrix of unspecified bits, so it is refused where it arrives and counted.
 */
enum class AnimatedPropRejection : std::uint8_t {
    Unsupported,
    NonFinite,
};

struct RejectedAnimatedProp {
    std::string name;
    AnimatedPropRejection rejection;
};

} // namespace react_native_linux
