#include <gtest/gtest.h>
#include <memory>

#include <react/featureflags/ReactNativeFeatureFlags.h>

namespace react_native_linux {
namespace {

/**
 * Issue #643: upstream's ReactInstanceTest, EventQueueProcessorTest and RuntimeExecutorShutdownTest read feature
 * flags and never reset them, because upstream runs every case in its own process. In one process the next case that
 * calls `ReactNativeFeatureFlags::override` — every `ReactHost`, and upstream's SchedulerDelegateInvalidationTest —
 * then throws "Feature flags were accessed before being overridden". Each case here starts from unaccessed flags.
 */
class FeatureFlagsResetListener : public testing::EmptyTestEventListener {
    void OnTestEnd(const testing::TestInfo& /*testInfo*/) override {
        facebook::react::ReactNativeFeatureFlags::dangerouslyReset();
    }
};

const bool kFeatureFlagsResetListenerAppended = [] {
    testing::UnitTest::GetInstance()->listeners().Append(std::make_unique<FeatureFlagsResetListener>().release());
    return true;
}();

} // namespace
} // namespace react_native_linux
