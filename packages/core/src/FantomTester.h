#pragma once

#include "FabricHost.h"
#include "ReactHost.h"

#include <cstddef>
#include <memory>
#include <string>

#include <react/renderer/graphics/Size.h>

namespace react_native_linux {

/**
 * A headless Fabric host a GoogleTest case drives directly, modelled on upstream's Fantom tester
 * (`private/react-native-fantom/tester`): real Hermes, bridgeless, real Fabric, and no window — so the tree a
 * bundle commits can be asserted on in a plain CI container with no compositor, no GPU and no golden image.
 *
 * The surface is the same empty surface `BundleRunner` starts, at a size the caller picks, and a task is a
 * script evaluated in the one runtime this host owns. Tasks compose: each one runs on top of everything the
 * previous ones left in the runtime and in the shadow tree, which is what lets a case commit, assert, mutate and
 * assert again — upstream's `Fantom.runTask`.
 *
 * Threading contract: every member is called from the thread that constructed the tester, exactly as
 * `ReactHost`'s and `FabricHost`'s are. `runTask` returns once the script, the event beat it induced and every
 * timer either of them armed have run on the JavaScript thread, so an assertion that follows it reads a tree
 * nothing is still writing to.
 *
 * Shutdown contract: the surface is stopped and the JavaScript thread drained before the Fabric host is
 * destroyed, because stopping the surface queues the resulting unmount onto that thread and the queued update
 * holds a raw pointer to the scheduler delegate. `ReactHost` is destroyed after it, by member order.
 */
class FantomTester final {
public:
    explicit FantomTester(facebook::react::Size surfaceSize);
    FantomTester(const FantomTester&) = delete;
    FantomTester(FantomTester&&) = delete;
    FantomTester& operator=(const FantomTester&) = delete;
    FantomTester& operator=(FantomTester&&) = delete;
    ~FantomTester() noexcept;

    /** Evaluates `script` in the tester's runtime and settles the JavaScript thread around what it committed. */
    void runTask(const std::string& script);

    /** The committed mount tree, rendered by `renderMountTree`. */
    std::string mountTreeText() const;

    bool hasReportedFatalError() const;

private:
    ReactHost reactHost_;
    std::unique_ptr<FabricHost> fabricHost_;
    size_t taskCount_{0};
};

} // namespace react_native_linux
