#include "AutomationProtocol.h"
#include "FantomTester.h"
#include "ReactHost.h"

#include <chrono>
#include <cstddef>
#include <cxxreact/JSBigString.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

// #76's instance-teardown gate: construct and destroy a whole React Native instance repeatedly in one process and
// assert that nothing it started is still there afterwards.
//
// react-native-windows' entire `Area: Instance Management` label is one bug repeated — a runtime holder that
// outlives `UnloadInstance`, a UIManager destroyed on the wrong thread, a slowdown that is a leak wearing a
// performance costume — and every one of them appears only on the *second* instance, which is why the one-shot
// suites in this binary never saw any of it. Fast Refresh, reload-on-error and multi-window all create second
// instances.
//
// The gate is the loop, not new infrastructure: this binary already runs under the `asan` and `tsan` presets in
// CI, so LeakSanitizer grades every iteration's heap and ThreadSanitizer grades the teardown races, and the two
// counter probes below cover the leak neither sanitizer can see — the one a still-reachable process-global
// holds. Both probes are read after the instance is destroyed. The fault log is graded absolutely — nothing an
// unloaded instance recorded may still be there — while the thread count is graded against the steady state
// after a warm-up, because a process-wide font collection, an image-decode worker and Hermes' own one-time
// initialisation are all paid for by the first instance or two; that is the warm-up shape AllocationCostTest's
// frame ceilings use.

namespace react_native_linux {
namespace {

constexpr facebook::react::Size kSurfaceSize{.width = 400, .height = 300};

constexpr size_t kInstanceCount = 4;
constexpr size_t kWarmUpInstances = 2;

// Short on purpose: the timer below is armed for a minute, so this budget always expires with the callback still
// pending and the host is always destroyed in that state.
constexpr std::chrono::milliseconds kQuiescenceBudget{50};

// A `setTimeout` distant enough that the budget above can never let it fire, so the destructor that follows
// always tears down a `TimerManager` still holding a real `jsi::Function` for it — the state that used to abort a
// debug Hermes with "This PointerValue was left dangling after the Runtime was destroyed" (see the shutdown
// contract in ReactHost.h).
constexpr char kScriptWithPendingTimer[] = "setTimeout(function () {}, 60000);";

// One instance's worth of work, and every kind of it the issue names as still open when the instance goes away: a
// committed Fabric tree, an `<Image>` whose decode is handed to the process-wide worker thread, and a
// `requestAnimationFrame` callback that is never given a frame — so `AnimationFrameQueue` still holds its
// `jsi::Function` when the runtime is destroyed.
//
// The frames are authored absolutely so what this asserts is the mount rather than the measurement of a font this
// container may or may not have, exactly as FantomTesterTest does. The image source names a file that does not
// exist: where there is a decoder the decode still goes to the worker thread and still completes, and a source that
// decoded would instead
// leave pixels in the process-wide 64 MiB image cache, which is a deliberate cross-instance cache and would make
// the probes below measure it rather than the instance.
constexpr char kInstanceTask[] = R"JAVASCRIPT(
const fabric = globalThis.nativeFabricUIManager;

// C++ holds instance handles weakly, so React retains them on its fibers and a task that commits has to do the
// same.
globalThis.instanceHandles = [];

const createNode = (tag, componentName, props) => {
  const instanceHandle = {};

  globalThis.instanceHandles.push(instanceHandle);

  return fabric.createNode(tag, componentName, 1, props, instanceHandle);
};
const box = (left, top, width, height) => ({ height, left, position: 'absolute', top, width });

// `collapsable: false` is what keeps the panel a parent: Fabric flattens a view that forms no stacking context
// and the tree would mount flat without it.
const panel = createNode(2, 'View', { collapsable: false, testID: 'panel', ...box(20, 20, 360, 260) });
const image = createNode(3, 'Image', {
  source: [{ uri: 'file:///nonexistent/rnl-instance-teardown.png' }],
  testID: 'image',
  ...box(10, 10, 40, 40),
});

fabric.appendChild(panel, image);

const childSet = fabric.createChildSet();

fabric.appendChildToSet(childSet, panel);
fabric.completeRoot(1, childSet);

requestAnimationFrame(function () {});

// The one fault each instance records on purpose, through the same non-fatal path React Native's own error
// reporting takes, so it is recorded in every build configuration, with or without an image decoder.
globalThis.RN$handleException(new Error('rnl-instance-teardown probe'), false, false);
)JAVASCRIPT";

constexpr char kExpectedMountTree[] =
    "<rn-rootview layoutMetrics-frame=\"{x:0,y:0,width:400,height:300}\">\n"
    "  <rn-view layoutMetrics-frame=\"{x:20,y:20,width:360,height:260}\" testID=\"panel\">\n"
    "    <rn-image layoutMetrics-frame=\"{x:10,y:10,width:40,height:40}\" testID=\"image\" />\n"
    "  </rn-view>\n"
    "</rn-rootview>\n";

/**
 * Every thread alive in this process, which is what a leaked JavaScript thread, timer dispatch thread or decode
 * worker shows up as. Linux publishes one directory per thread under `/proc/self/task`, so this needs no
 * bookkeeping of ours and therefore cannot agree with a buggy teardown about what was started.
 */
size_t liveThreadCount() {
    return static_cast<size_t>(
        std::distance(std::filesystem::directory_iterator{"/proc/self/task"}, std::filesystem::directory_iterator{}));
}

/**
 * The process-wide fault log the automation channel's `ListErrors` answers from — the still-reachable cache the
 * issue's third acceptance criterion is about, and one no sanitizer can grade, because a `std::vector` a live
 * global owns is not leaked memory by any definition LeakSanitizer uses.
 *
 * The instance below records exactly one fault while it is alive, deliberately — a non-fatal JavaScript error —
 * so that reading zero here after it is destroyed means the log was emptied rather than never written to.
 */
size_t recordedErrorCount() { return automationErrorLog().list().size(); }

// The fault `kInstanceTask` provokes on purpose. A build with an image decoder records a second one, the missing
// image source, which is why the count read while the instance is alive is a floor. See kInstanceTask.
constexpr size_t kFaultsPerInstance = 1;

struct InstanceProbe {
    size_t threadCount;
    size_t errorCount;
};

InstanceProbe probeAfterTeardown() {
    return InstanceProbe{.threadCount = liveThreadCount(), .errorCount = recordedErrorCount()};
}

/**
 * The error count is absolute rather than relative: nothing an unloaded instance recorded may still be there, so
 * the value every iteration has to read is zero. The thread count is compared against the steady state instead,
 * because the process-wide image-decode worker, the font collection's threads and Hermes' own are started once by
 * whichever instance needed them first and are not this loop's to account for.
 */
void expectNothingSurvivedTheUnload(const std::vector<InstanceProbe>& probes) {
    ASSERT_EQ(probes.size(), kInstanceCount);

    const size_t steadyStateThreadCount = probes[kWarmUpInstances].threadCount;

    for (size_t instance = 0; instance < kInstanceCount; ++instance) {
        EXPECT_EQ(probes[instance].errorCount, 0U) << "the fault log still holds " << probes[instance].errorCount
                                                   << " entries after instance " << instance << " was unloaded";
    }

    for (size_t instance = kWarmUpInstances + 1; instance < kInstanceCount; ++instance) {
        EXPECT_EQ(probes[instance].threadCount, steadyStateThreadCount)
            << "instance " << instance << " left a thread behind: " << steadyStateThreadCount << " threads were "
            << "alive after instance " << kWarmUpInstances << " and " << probes[instance].threadCount << " after it";
    }
}

TEST(InstanceTeardownLeakTest, RepeatedFabricInstancesLeaveNoThreadOrFaultBehind) {
    std::vector<InstanceProbe> probes;
    std::string firstMountTree;

    for (size_t instance = 0; instance < kInstanceCount; ++instance) {
        std::string mountTree;

        {
            FantomTester tester{kSurfaceSize};

            tester.runTask(kInstanceTask);

            mountTree = tester.mountTreeText();

            EXPECT_FALSE(tester.hasReportedFatalError()) << "instance " << instance << " reported a fatal error";
            // Read while the instance is alive, so that the zero this loop asserts after teardown is the log
            // having been emptied rather than the fault sites having gone quiet.
            EXPECT_GE(recordedErrorCount(), kFaultsPerInstance)
                << "instance " << instance << " did not record the fault the task provokes";
        }

        probes.push_back(probeAfterTeardown());

        if (instance == 0) {
            firstMountTree = mountTree;
        } else {
            // The react-native-windows failure shape in one assertion: the second instance renders what the first
            // one did, or something the first one left behind is deciding what the second one sees.
            EXPECT_EQ(mountTree, firstMountTree) << "instance " << instance << " rendered a different tree";
        }
    }

    EXPECT_EQ(firstMountTree, kExpectedMountTree);
    expectNothingSurvivedTheUnload(probes);
}

TEST(InstanceTeardownLeakTest, RepeatedHostsDestroyCleanlyWithATimerCallbackStillPending) {
    std::vector<InstanceProbe> probes;

    for (size_t instance = 0; instance < kInstanceCount; ++instance) {
        {
            ReactHost reactHost;

            reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kScriptWithPendingTimer),
                                 "InstanceTeardownLeakTest.js");

            EXPECT_FALSE(reactHost.runUntilQuiescent(kQuiescenceBudget))
                << "instance " << instance << " ran the timer that this case needs to still be pending";

            // `reactHost` goes out of scope here with the timer still pending. The assertion this loop exists for
            // is that the second host can be constructed at all — `ReactHost`'s constructor installs this
            // platform's feature-flag overrides, upstream throws on a second `override` in a process, and it is
            // the destructor's job to hand that back — and that no iteration aborts the binary on its way out.
        }

        probes.push_back(probeAfterTeardown());
    }

    expectNothingSurvivedTheUnload(probes);
}

} // namespace
} // namespace react_native_linux
