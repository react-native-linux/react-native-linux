#include "AnimationTestsBase.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <folly/dynamic.h>
#include <react/renderer/animated/nodes/AnimatedNode.h>
#include <react/renderer/animated/nodes/StyleAnimatedNode.h>
#include <string>
#include <vector>

namespace facebook::react {

// The upstream node-type lookup table, spelled out. Upstream exposes no iterable enumeration of the names -
// AnimatedNode::getNodeTypeByName is a string-to-enum chain over literals - so this table is the enumeration the
// conformance rows are written against, and the distinctness assertion below is the drift check it supports.
constexpr std::array<std::string_view, 15> kUpstreamNodeTypes{
    "style",          "value",   "color",     "props",     "interpolation", "addition", "subtraction", "division",
    "multiplication", "modulus", "diffclamp", "transform", "tracking",      "round",    "object"};

/**
 * The style-and-props tail of the graph both allowlist tests wire: a style node over `styleObject`, a props
 * node over the style, and the view connection. A helper because the same tail twice is a jscpd clone at
 * threshold 0.
 */
void wireStyleAndProps(NativeAnimatedNodesManager& manager, Tag styleTag, const folly::dynamic& styleObject,
                       Tag propsTag, Tag viewTag) {
    manager.createAnimatedNode(styleTag, folly::dynamic::object("type", "style")("style", styleObject));

    for (const auto& entry : styleObject.items()) {
        if (entry.second.isInt()) {
            manager.connectAnimatedNodes(static_cast<Tag>(entry.second.asInt()), styleTag);
        }
    }

    manager.createAnimatedNode(
        propsTag, folly::dynamic::object("type", "props")("props", folly::dynamic::object("style", styleTag)));

    manager.connectAnimatedNodes(styleTag, propsTag);
    manager.connectAnimatedNodeToView(propsTag, viewTag);
}

/**
 * The conformance suite of issue #75: the upstream shared animated node graph driven through the same
 * `NativeAnimatedNodesManager` the platform runs, with the frame clock injected by `runAnimationFrame`. Every
 * value the assertions read comes out of the manager itself, so a drift in the node arithmetic or in the
 * string-to-facade shapes fails here before it fails in an app.
 */
class AnimatedConformanceTest : public AnimationTestsBase {};

/**
 * The drift oracle: every node type upstream's lookup accepts is in the conformance table, and every name in the
 * table resolves. A node type added upstream without a row here is a coverage gap this test names.
 */
TEST_F(AnimatedConformanceTest, EveryUpstreamNodeTypeIsInTheConformanceTable) {
    std::vector<AnimatedNodeType> resolvedTypes;

    for (const std::string_view typeName : kUpstreamNodeTypes) {
        const auto type = AnimatedNode::getNodeTypeByName(std::string(typeName));

        ASSERT_TRUE(type.has_value()) << "node type " << typeName << " is not known to the manager";

        // Distinctness is the completeness proof the manager's lookup cannot give: upstream exposes no iterable
        // enumeration of node type names (getNodeTypeByName is a string-to-enum chain over literals), so a new
        // upstream type cannot fail this test - but a name that aliases another's enum value here would.
        EXPECT_EQ(std::count(resolvedTypes.begin(), resolvedTypes.end(), *type), 0)
            << "node type " << typeName << " resolves to an enum value another name already claimed";

        resolvedTypes.push_back(*type);
    }
}

TEST_F(AnimatedConformanceTest, AValueNodeCarriesItsOffsetAndFlattensItIntoTheValue) {
    initNodesManager();

    const auto valueTag = 1;
    const auto interpolationTag = 2;

    nodesManager_->createAnimatedNode(valueTag, folly::dynamic::object("type", "value")("value", 5)("offset", 3));

    nodesManager_->createAnimatedNode(
        interpolationTag,
        folly::dynamic::object("type", "interpolation")("input", valueTag)("inputRange", folly::dynamic::array(0, 100))(
            "outputType", "numeric")("outputRange", folly::dynamic::array(0, 100))("extrapolateLeft", "extend")(
            "extrapolateRight", "extend"));

    nodesManager_->connectAnimatedNodes(valueTag, interpolationTag);

    // value + offset is what the graph exposes.
    EXPECT_EQ(nodesManager_->getValue(valueTag).value(), 8);

    nodesManager_->setAnimatedNodeValue(valueTag, 7);
    runAnimationFrame(0);

    EXPECT_EQ(nodesManager_->getValue(valueTag).value(), 10);

    // Flattening folds the offset into the value and resets the offset, one operation, no drift.
    nodesManager_->flattenAnimatedNodeOffset(valueTag);
    runAnimationFrame(0);

    EXPECT_EQ(nodesManager_->getValue(valueTag).value(), 10);
}

TEST_F(AnimatedConformanceTest, InterpolationExtrapolatesByDefaultAndClampsWhenAsked) {
    initNodesManager();

    const auto valueTag = 1;
    const auto extendTag = 2;
    const auto clampTag = 3;

    nodesManager_->createAnimatedNode(valueTag, folly::dynamic::object("type", "value")("value", 0)("offset", 0));

    nodesManager_->createAnimatedNode(
        extendTag,
        folly::dynamic::object("type", "interpolation")("input", valueTag)("inputRange", folly::dynamic::array(0, 100))(
            "outputType", "numeric")("outputRange", folly::dynamic::array(0, 10))("extrapolateLeft", "extend")(
            "extrapolateRight", "extend"));

    nodesManager_->createAnimatedNode(
        clampTag,
        folly::dynamic::object("type", "interpolation")("input", valueTag)("inputRange", folly::dynamic::array(0, 100))(
            "outputType", "numeric")("outputRange", folly::dynamic::array(0, 10))("extrapolateLeft", "clamp")(
            "extrapolateRight", "clamp"));

    nodesManager_->connectAnimatedNodes(valueTag, extendTag);
    nodesManager_->connectAnimatedNodes(valueTag, clampTag);

    // Inside the range: linear.
    nodesManager_->setAnimatedNodeValue(valueTag, 50);
    runAnimationFrame(0);

    EXPECT_EQ(nodesManager_->getValue(extendTag).value(), 5);
    EXPECT_EQ(nodesManager_->getValue(clampTag).value(), 5);

    // Past the high end: extend follows the line, clamp stops at the last output.
    nodesManager_->setAnimatedNodeValue(valueTag, 150);
    runAnimationFrame(0);

    EXPECT_EQ(nodesManager_->getValue(extendTag).value(), 15);
    EXPECT_EQ(nodesManager_->getValue(clampTag).value(), 10);

    // Past the low end: both ends behave.
    nodesManager_->setAnimatedNodeValue(valueTag, -50);
    runAnimationFrame(0);

    EXPECT_EQ(nodesManager_->getValue(extendTag).value(), -5);
    EXPECT_EQ(nodesManager_->getValue(clampTag).value(), 0);
}

TEST_F(AnimatedConformanceTest, TheArithmeticNodesComputeFromTheirInputs) {
    initNodesManager();

    const auto firstTag = 1;
    const auto secondTag = 2;

    nodesManager_->createAnimatedNode(firstTag, folly::dynamic::object("type", "value")("value", 0)("offset", 0));
    nodesManager_->createAnimatedNode(secondTag, folly::dynamic::object("type", "value")("value", 0)("offset", 0));

    const auto makeArithmetic = [this](Tag tag, const std::string& type, Tag first, Tag second) {
        nodesManager_->createAnimatedNode(
            tag, folly::dynamic::object("type", type)("input", folly::dynamic::array(first, second)));
        nodesManager_->connectAnimatedNodes(first, tag);
        nodesManager_->connectAnimatedNodes(second, tag);
    };

    const auto additionTag = 3;
    const auto subtractionTag = 4;
    const auto multiplicationTag = 5;
    const auto divisionTag = 6;
    const auto modulusTag = 7;

    makeArithmetic(additionTag, "addition", firstTag, secondTag);
    makeArithmetic(subtractionTag, "subtraction", firstTag, secondTag);
    makeArithmetic(multiplicationTag, "multiplication", firstTag, secondTag);
    makeArithmetic(divisionTag, "division", firstTag, secondTag);

    // The modulus node takes a single input and its own modulus, unlike the two-input operators.
    nodesManager_->createAnimatedNode(modulusTag,
                                      folly::dynamic::object("type", "modulus")("input", firstTag)("modulus", 3));
    nodesManager_->connectAnimatedNodes(firstTag, modulusTag);

    nodesManager_->setAnimatedNodeValue(firstTag, 10);
    nodesManager_->setAnimatedNodeValue(secondTag, 3);
    runAnimationFrame(0);

    EXPECT_EQ(nodesManager_->getValue(additionTag).value(), 13);
    EXPECT_EQ(nodesManager_->getValue(subtractionTag).value(), 7);
    EXPECT_EQ(nodesManager_->getValue(multiplicationTag).value(), 30);
    EXPECT_EQ(nodesManager_->getValue(divisionTag).value(), 10.0 / 3.0);
    EXPECT_EQ(nodesManager_->getValue(modulusTag).value(), 1);
}

/**
 * The diffClamp contract: the clamped value follows the INPUT'S DELTAS, not the input's level, so an input that
 * overshot the max and then returns drags the clamped value back down instead of snapping to the input.
 */
TEST_F(AnimatedConformanceTest, ADiffClampFollowsDeltasAndStaysInsideItsBounds) {
    initNodesManager();

    const auto valueTag = 1;
    const auto clampTag = 2;

    nodesManager_->createAnimatedNode(valueTag, folly::dynamic::object("type", "value")("value", 0)("offset", 0));
    nodesManager_->createAnimatedNode(
        clampTag, folly::dynamic::object("type", "diffclamp")("input", valueTag)("min", 0)("max", 10));
    nodesManager_->connectAnimatedNodes(valueTag, clampTag);

    nodesManager_->setAnimatedNodeValue(valueTag, 5);
    runAnimationFrame(0);

    EXPECT_EQ(nodesManager_->getValue(clampTag).value(), 5);

    nodesManager_->setAnimatedNodeValue(valueTag, 15);
    runAnimationFrame(0);

    EXPECT_EQ(nodesManager_->getValue(clampTag).value(), 10);

    // The input fell ten points, so the clamped value falls ten points from the clamp, not to the input's five.
    nodesManager_->setAnimatedNodeValue(valueTag, 5);
    runAnimationFrame(0);

    EXPECT_EQ(nodesManager_->getValue(clampTag).value(), 0);

    nodesManager_->setAnimatedNodeValue(valueTag, 8);
    runAnimationFrame(0);

    EXPECT_EQ(nodesManager_->getValue(clampTag).value(), 3);
}

/**
 * Additive composition of two animations on one prop: two value nodes feeding one style's transform arrive in
 * the committed props together, through the allowlist gate that decides what the native driver may own.
 */
TEST_F(AnimatedConformanceTest, TwoAnimationsOnOnePropComposeAndTheAllowlistFiltersWhatCommits) {
    initNodesManager();

    const auto firstTag = 1;
    const auto secondTag = 2;
    const auto transformTag = 3;
    const auto styleTag = 4;
    const auto propsTag = 5;
    const auto viewTag = 6;

    nodesManager_->createAnimatedNode(firstTag, folly::dynamic::object("type", "value")("value", 10)("offset", 0));
    nodesManager_->createAnimatedNode(secondTag, folly::dynamic::object("type", "value")("value", 20)("offset", 0));

    nodesManager_->createAnimatedNode(
        transformTag,
        folly::dynamic::object("type", "transform")(
            "transforms",
            folly::dynamic::array(
                folly::dynamic::object("type", "animated")("property", "translateX")("nodeTag", firstTag),
                folly::dynamic::object("type", "animated")("property", "translateX")("nodeTag", secondTag))));

    nodesManager_->connectAnimatedNodes(firstTag, transformTag);
    nodesManager_->connectAnimatedNodes(secondTag, transformTag);

    const folly::dynamic styleObject =
        folly::dynamic::object("transform", transformTag)("opacity", firstTag)("notAnAllowlistedProp", secondTag);

    wireStyleAndProps(*nodesManager_, styleTag, styleObject, propsTag, viewTag);

    // The props commit is a change commit: the frame has to move something before there is anything to flush.
    nodesManager_->setAnimatedNodeValue(firstTag, 12);

    runAnimationFrame(0);

    const auto props = nodesManager_->getManagedProps(viewTag);

    ASSERT_FALSE(props.isNull());
    EXPECT_EQ(props["transform"][0]["translateX"].asDouble(), 12);
    EXPECT_EQ(props["transform"][1]["translateX"].asDouble(), 20);
    EXPECT_EQ(props["opacity"].asDouble(), 12);

    // The non-allowlisted prop rides along in the commit; the fallback is the layout-style flag the manager
    // reads off the style node, which is what routes the prop into a re-layout instead of dropping it.
    const auto* styleNode = nodesManager_->getAnimatedNode<StyleAnimatedNode>(styleTag);

    ASSERT_NE(styleNode, nullptr);
    EXPECT_TRUE(styleNode->isLayoutStyleUpdated());
}

/**
 * The clean-fallback half of the same contract: a style whose moving props are all in the direct-manipulation
 * allowlist does not raise the layout-style flag - the frame commits without a re-layout.
 */
TEST_F(AnimatedConformanceTest, AStyleWithOnlyAllowlistedPropsDoesNotRaiseTheLayoutFlag) {
    initNodesManager();

    const auto opacityTag = 1;
    const auto styleTag = 2;
    const auto propsTag = 3;
    const auto viewTag = 4;

    nodesManager_->createAnimatedNode(opacityTag, folly::dynamic::object("type", "value")("value", 1)("offset", 0));

    const folly::dynamic styleObject = folly::dynamic::object("opacity", opacityTag);

    wireStyleAndProps(*nodesManager_, styleTag, styleObject, propsTag, viewTag);

    nodesManager_->setAnimatedNodeValue(opacityTag, 0.5);

    runAnimationFrame(0);

    const auto props = nodesManager_->getManagedProps(viewTag);

    ASSERT_FALSE(props.isNull());
    EXPECT_EQ(props["opacity"].asDouble(), 0.5);

    const auto* styleNode = nodesManager_->getAnimatedNode<StyleAnimatedNode>(styleTag);

    ASSERT_NE(styleNode, nullptr);
    EXPECT_FALSE(styleNode->isLayoutStyleUpdated());
}

} // namespace facebook::react