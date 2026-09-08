#include "MountTreeText.h"

#include <gtest/gtest.h>
#include <string>

namespace react_native_linux {
namespace {

SceneNode makeNode(facebook::react::Tag tag, facebook::react::Tag parentTag, std::string componentName,
                   facebook::react::Rect frame) {
    SceneNode node;

    node.tag = tag;
    node.parentTag = parentTag;
    node.componentName = std::move(componentName);
    node.layoutMetrics.frame = frame;

    return node;
}

TEST(MountTreeTextTest, RendersAnEmptySceneAsAnEmptyString) { EXPECT_EQ(renderMountTree(SceneNodes{}), ""); }

TEST(MountTreeTextTest, RendersALeafAsASelfClosingElementNamedAfterItsComponent) {
    SceneNodes nodes;

    nodes[1] = makeNode(1, 0, "View", {.origin = {.x = 4, .y = 8}, .size = {.width = 100, .height = 50}});

    EXPECT_EQ(renderMountTree(nodes), "<rn-view layoutMetrics-frame=\"{x:4,y:8,width:100,height:50}\" />\n");
}

TEST(MountTreeTextTest, KeepsTheDigitsOfAFractionalFrame) {
    SceneNodes nodes;

    nodes[1] = makeNode(1, 0, "View", {.origin = {.x = 0.5, .y = 0}, .size = {.width = 10.25, .height = 0}});

    EXPECT_EQ(renderMountTree(nodes), "<rn-view layoutMetrics-frame=\"{x:0.5,y:0,width:10.25,height:0}\" />\n");
}

// Six fractional digits are what `std::to_string` prints, so these two coordinates would render identically
// under it and a node that moved would assert as one that had not.
TEST(MountTreeTextTest, RendersTwoCoordinatesThatDifferBeyondSixDigitsDifferently) {
    SceneNodes nodes;

    nodes[1] = makeNode(1, 0, "View", {.origin = {.x = 0.1234567F, .y = 0.1234568F}, .size = {}});

    EXPECT_EQ(renderMountTree(nodes),
              "<rn-view layoutMetrics-frame=\"{x:0.1234567,y:0.1234568,width:0,height:0}\" />\n");
}

TEST(MountTreeTextTest, EscapesTheCharactersATestIdCannotCarryInAnAttribute) {
    SceneNodes nodes;

    nodes[1] = makeNode(1, 0, "View", {});
    nodes[1].testId = "a&b<c\"d";

    EXPECT_EQ(renderMountTree(nodes),
              "<rn-view layoutMetrics-frame=\"{x:0,y:0,width:0,height:0}\" testID=\"a&amp;b&lt;c&quot;d\" />\n");
}

TEST(MountTreeTextTest, RendersTheTestIdWhenTheNodeCarriesOne) {
    SceneNodes nodes;

    nodes[1] = makeNode(1, 0, "View", {});
    nodes[1].testId = "panel";

    EXPECT_EQ(renderMountTree(nodes),
              "<rn-view layoutMetrics-frame=\"{x:0,y:0,width:0,height:0}\" testID=\"panel\" />\n");
}

TEST(MountTreeTextTest, IndentsChildrenUnderAnOpenAndCloseElement) {
    SceneNodes nodes;

    nodes[1] = makeNode(1, 0, "RootView", {.origin = {}, .size = {.width = 400, .height = 300}});
    nodes[1].childTags = {2};
    nodes[2] = makeNode(2, 1, "ScrollView", {.origin = {}, .size = {.width = 100, .height = 100}});
    nodes[2].childTags = {3};
    nodes[3] = makeNode(3, 2, "Paragraph", {.origin = {}, .size = {.width = 100, .height = 20}});

    EXPECT_EQ(renderMountTree(nodes), "<rn-rootview layoutMetrics-frame=\"{x:0,y:0,width:400,height:300}\">\n"
                                      "  <rn-scrollview layoutMetrics-frame=\"{x:0,y:0,width:100,height:100}\">\n"
                                      "    <rn-paragraph layoutMetrics-frame=\"{x:0,y:0,width:100,height:20}\" />\n"
                                      "  </rn-scrollview>\n"
                                      "</rn-rootview>\n");
}

TEST(MountTreeTextTest, RendersEveryRootInTagOrderRatherThanInHashOrder) {
    SceneNodes nodes;

    nodes[11] = makeNode(11, 0, "View", {});
    nodes[2] = makeNode(2, 0, "Image", {});

    EXPECT_EQ(renderMountTree(nodes), "<rn-image layoutMetrics-frame=\"{x:0,y:0,width:0,height:0}\" />\n"
                                      "<rn-view layoutMetrics-frame=\"{x:0,y:0,width:0,height:0}\" />\n");
}

// A child tag naming a node the scene no longer holds cannot arise from a mounting transaction — the scene
// erases a node and its parent's reference to it together — so it is asserted here rather than through a host.
TEST(MountTreeTextTest, SkipsAChildTagThatNamesNoNode) {
    SceneNodes nodes;

    nodes[1] = makeNode(1, 0, "View", {});
    nodes[1].childTags = {404};

    EXPECT_EQ(renderMountTree(nodes), "<rn-view layoutMetrics-frame=\"{x:0,y:0,width:0,height:0}\">\n"
                                      "</rn-view>\n");
}

} // namespace
} // namespace react_native_linux
