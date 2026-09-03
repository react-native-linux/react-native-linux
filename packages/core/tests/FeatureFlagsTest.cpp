#include "ReactNativeFeatureFlagsOverridesLinux.h"

#include <gtest/gtest.h>
#include <react/featureflags/ReactNativeFeatureFlags.h>

#include <memory>

namespace {

/**
 * The upstream defaults these guard against, all from ReactNativeFeatureFlagsDefaults.h: cxxNativeAnimatedEnabled
 * false, useSharedAnimatedBackend false, optimizedAnimatedPropUpdates false. The first two are ours to turn on and
 * the third is ours to leave alone, so a React Native bump that changes any of them fails here rather than
 * silently changing how animations run.
 *
 * `override` throws once any flag has been read, and the accessor is process-global, so each test starts from a
 * fresh accessor and leaves one behind for whatever runs next in the same process.
 */
class FeatureFlagsTest : public testing::Test {
protected:
    void SetUp() override {
        facebook::react::ReactNativeFeatureFlags::dangerouslyReset();
        facebook::react::ReactNativeFeatureFlags::override(
            std::make_unique<react_native_linux::ReactNativeFeatureFlagsOverridesLinux>());
    }

    void TearDown() override { facebook::react::ReactNativeFeatureFlags::dangerouslyReset(); }
};

TEST_F(FeatureFlagsTest, EnablesTheCxxNativeAnimatedDriver) {
    EXPECT_TRUE(facebook::react::ReactNativeFeatureFlags::cxxNativeAnimatedEnabled());
}

TEST_F(FeatureFlagsTest, EnablesTheSharedAnimationBackend) {
    EXPECT_TRUE(facebook::react::ReactNativeFeatureFlags::useSharedAnimatedBackend());
}

TEST_F(FeatureFlagsTest, LeavesOptimizedAnimatedPropUpdatesOff) {
    EXPECT_FALSE(facebook::react::ReactNativeFeatureFlags::optimizedAnimatedPropUpdates());
}

TEST_F(FeatureFlagsTest, KeepsTheOssStableOverridesItInherits) {
    EXPECT_TRUE(facebook::react::ReactNativeFeatureFlags::enableBridgelessArchitecture());
    EXPECT_TRUE(facebook::react::ReactNativeFeatureFlags::useNativeViewConfigsInBridgelessMode());
}

} // namespace
