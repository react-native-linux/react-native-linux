#include "FabricHost.h"
#include "MountTreeText.h"
#include "ReactHost.h"

#include <cxxreact/JSBigString.h>
#include <gtest/gtest.h>
#include <memory>
#include <string>

namespace react_native_linux {
namespace {

constexpr facebook::react::Size kSurfaceSize{.width = 200, .height = 100};
constexpr facebook::react::SurfaceId kAdditionalSurfaceId = 11;
constexpr facebook::react::SurfaceId kNeverStartedSurfaceId = 21;

// One view committed into surface 11, its instance handle retained the way React retains one on its fiber.
constexpr char kCommitIntoAdditionalSurface[] = R"JAVASCRIPT(
const fabric = globalThis.nativeFabricUIManager;
globalThis.instanceHandle = {};
const panel = fabric.createNode(12, 'View', 11, {
  collapsable: false,
  height: 40,
  left: 10,
  position: 'absolute',
  testID: 'panel',
  top: 20,
  width: 30,
}, globalThis.instanceHandle);
const childSet = fabric.createChildSet();
fabric.appendChildToSet(childSet, panel);
fabric.completeRoot(11, childSet);
)JAVASCRIPT";

constexpr char kMountedTree[] =
    "<rn-rootview layoutMetrics-frame=\"{x:0,y:0,width:200,height:100}\" />\n"
    "<rn-rootview layoutMetrics-frame=\"{x:0,y:0,width:200,height:100}\">\n"
    "  <rn-view layoutMetrics-frame=\"{x:10,y:20,width:30,height:40}\" testID=\"panel\" />\n"
    "</rn-rootview>\n";

constexpr char kEmptiedTree[] = "<rn-rootview layoutMetrics-frame=\"{x:0,y:0,width:200,height:100}\" />\n"
                                "<rn-rootview layoutMetrics-frame=\"{x:0,y:0,width:200,height:100}\" />\n";

/**
 * Issue #210, `Fantom.createRoot`'s surfaces: a surface started beside the host's own takes a tree committed under
 * its id into the retained scene under its own root, and stopping it commits it empty. Stopping it a second time,
 * or stopping one that never started, is left alone. Upstream's `SurfaceManager::stopSurface` would erase its end
 * iterator there, which the sanitizer jobs would report.
 */
TEST(AdditionalSurfaceTest, ATreeCommittedToAnAdditionalSurfaceMountsUnderItsOwnRootAndStoppingItEmptiesIt) {
    ReactHost reactHost;
    auto fabricHost = std::make_unique<FabricHost>(reactHost.reactInstance(), kSurfaceSize);

    fabricHost->startAdditionalSurface(kAdditionalSurfaceId, kSurfaceSize, 1.0F);
    reactHost.loadScript(std::make_unique<facebook::react::JSBigStdString>(kCommitIntoAdditionalSurface),
                         "AdditionalSurfaceTest.js");
    reactHost.drainJavaScriptThread();

    EXPECT_EQ(renderMountTree(fabricHost->visualTreeNodes()), kMountedTree);

    fabricHost->stopAdditionalSurface(kAdditionalSurfaceId);
    reactHost.drainJavaScriptThread();

    EXPECT_EQ(renderMountTree(fabricHost->visualTreeNodes()), kEmptiedTree);

    fabricHost->stopAdditionalSurface(kAdditionalSurfaceId);
    fabricHost->stopAdditionalSurface(kNeverStartedSurfaceId);

    fabricHost->stopSurface();
    reactHost.drainJavaScriptThread();
    fabricHost.reset();

    EXPECT_FALSE(reactHost.hasReportedFatalError());
}

} // namespace
} // namespace react_native_linux
