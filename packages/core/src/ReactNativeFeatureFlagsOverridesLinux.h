#pragma once

#include <react/featureflags/ReactNativeFeatureFlagsOverridesOSSStable.h>

namespace react_native_linux {

/**
 * The feature flags this platform runs with: upstream's OSS-stable set plus the two that turn on the C++ Native
 * Animated driver (#128).
 *
 * `ReactNativeFeatureFlagsDefaults` answers false to all three flags below, and
 * `ReactNativeFeatureFlagsOverridesOSSCanary` — the upstream class that turns `cxxNativeAnimatedEnabled` on — is
 * `@generated` and carries four unrelated experiments, so this subclasses OSS-stable instead of adopting it.
 * `packages/core/tests/FeatureFlagsTest.cpp` asserts all three values, so a default that changes on the next React
 * Native bump is a test failure rather than a behaviour change nobody notices.
 *
 * `optimizedAnimatedPropUpdates` stays off deliberately: its documentation describes Android JNI batching and an
 * iOS `cloneProps` path, neither of which is this platform's.
 *
 * `useSharedAnimatedBackend` is load-bearing beyond Animated itself. `Scheduler`'s constructor reads it and, when
 * it is true, dereferences `SchedulerToolbox::animationChoreographer` unconditionally, so every host that builds a
 * `Scheduler` has to supply one. See *Animated backend* in docs/cpp-toolchain.md.
 */
class ReactNativeFeatureFlagsOverridesLinux final
    : public facebook::react::ReactNativeFeatureFlagsOverridesOSSStable {
public:
    bool cxxNativeAnimatedEnabled() override { return true; }

    bool useSharedAnimatedBackend() override { return true; }
};

} // namespace react_native_linux
