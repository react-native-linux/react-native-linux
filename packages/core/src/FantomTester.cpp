#include "FantomTester.h"

#include "DimensionsSource.h"
#include "MountTreeText.h"

#include <chrono>
#include <cxxreact/JSBigString.h>
#include <memory>
#include <string>
#include <utility>

#include <react/featureflags/ReactNativeFeatureFlags.h>

namespace react_native_linux {

namespace {

constexpr std::chrono::milliseconds kQuiescenceBudget{30000};

} // namespace

FantomTester::FantomTester(facebook::react::Size surfaceSize)
    : fabricHost_(std::make_unique<FabricHost>(reactHost_.reactInstance(), surfaceSize)) {
    reactHost_.dimensions().configure(static_cast<double>(surfaceSize.width), static_cast<double>(surfaceSize.height),
                                      DimensionsSource::kDefaultScale);
}

FantomTester::~FantomTester() noexcept {
    fabricHost_->stopSurface();
    reactHost_.drainJavaScriptThread();
    fabricHost_.reset();

    // `ReactHost`'s constructor installs the platform's feature-flag overrides, and upstream throws on a second
    // `override` for the process. One tester per process would be the alternative, so the overrides are dropped
    // here rather than at the next construction: this is the object whose lifetime the host's is nested inside.
    facebook::react::ReactNativeFeatureFlags::dangerouslyReset();
}

void FantomTester::runTask(const std::string& script) {
    ++taskCount_;

    reactHost_.loadScript(std::make_unique<facebook::react::JSBigStdString>(script),
                          "fantom-task-" + std::to_string(taskCount_) + ".js");
    reactHost_.drainJavaScriptThread();
    fabricHost_->induceEventBeat();
    reactHost_.runUntilQuiescent(kQuiescenceBudget);
}

std::string FantomTester::mountTreeText() const { return renderMountTree(fabricHost_->visualTreeNodes()); }

bool FantomTester::hasReportedFatalError() const { return reactHost_.hasReportedFatalError(); }

} // namespace react_native_linux
