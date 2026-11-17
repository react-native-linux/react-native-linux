#include "FantomRenderOutput.h"

#include <gtest/gtest.h>
#include <memory>
#include <string>

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/mounting/ShadowView.h>
#include <react/renderer/mounting/stubs/StubViewTree.h>

namespace {

TEST(FantomRenderOutputTest, AnUnnamedViewRendersWithAnEmptyType) {
    facebook::react::ShadowView root;

    root.props = std::make_shared<const facebook::react::ViewProps>();

    const std::string output = react_native_linux::renderFantomOutput(facebook::react::StubViewTree{root}, true, false);

    EXPECT_NE(output.find(R"("type":"")"), std::string::npos);
}

} // namespace
