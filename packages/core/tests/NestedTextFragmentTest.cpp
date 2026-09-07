#include <folly/dynamic.h>
#include <gtest/gtest.h>
#include <memory>
#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/TextAttributes.h>
#include <react/renderer/attributedstring/primitives.h>
#include <react/renderer/componentregistry/ComponentDescriptorProviderRegistry.h>
#include <react/renderer/componentregistry/ComponentDescriptorRegistry.h>
#include <react/renderer/components/text/BaseTextShadowNode.h>
#include <react/renderer/components/text/ParagraphComponentDescriptor.h>
#include <react/renderer/components/text/ParagraphShadowNode.h>
#include <react/renderer/components/text/RawTextComponentDescriptor.h>
#include <react/renderer/components/text/TextComponentDescriptor.h>
#include <react/renderer/components/view/ViewComponentDescriptor.h>
#include <react/renderer/core/ComponentDescriptor.h>
#include <react/renderer/core/PropsParserContext.h>
#include <react/renderer/core/RawProps.h>
#include <react/renderer/core/ShadowNode.h>
#include <react/renderer/core/ShadowNodeFragment.h>
#include <react/utils/ContextContainer.h>
#include <string>
#include <utility>
#include <vector>

namespace {

using facebook::react::AttributedString;
using facebook::react::BaseTextShadowNode;
using facebook::react::ParagraphShadowNode;
using facebook::react::ShadowNode;

constexpr facebook::react::SurfaceId kSurfaceId = 1;
constexpr float kParagraphFontSize = 16;
constexpr float kNestedFontSize = 20;

/**
 * Nodes created the way `nativeFabricUIManager.createNode` creates them: by component name, through
 * `ComponentDescriptorRegistry::at`, which passes every name through `componentNameByReactViewName` before it
 * looks the descriptor up. That mapping is the subject of issue #312 — it rewrites `"Text"` to `"Paragraph"`,
 * so the name a nested `<Text>` has to be created with is the React view name `"RCTVirtualText"` and not the
 * unified C++ name the other components answer to.
 */
class BundleNodeFactory final {
public:
    BundleNodeFactory() {
        providerRegistry_.add(
            facebook::react::concreteComponentDescriptorProvider<facebook::react::ViewComponentDescriptor>());
        providerRegistry_.add(
            facebook::react::concreteComponentDescriptorProvider<facebook::react::ParagraphComponentDescriptor>());
        providerRegistry_.add(
            facebook::react::concreteComponentDescriptorProvider<facebook::react::TextComponentDescriptor>());
        providerRegistry_.add(
            facebook::react::concreteComponentDescriptorProvider<facebook::react::RawTextComponentDescriptor>());

        registry_ = providerRegistry_.createComponentDescriptorRegistry(facebook::react::ComponentDescriptorParameters{
            .eventDispatcher = {}, .contextContainer = contextContainer_, .flavor = nullptr});
    }

    std::shared_ptr<const ShadowNode> makeNode(const std::string& componentName, folly::dynamic props,
                                               std::vector<std::shared_ptr<const ShadowNode>> children) {
        const facebook::react::ComponentDescriptor& descriptor = registry_->at(componentName);
        const auto family =
            descriptor.createFamily({.tag = nextTag_++, .surfaceId = kSurfaceId, .instanceHandle = nullptr});
        const facebook::react::PropsParserContext parserContext{kSurfaceId, *contextContainer_};

        return descriptor.createShadowNode(
            facebook::react::ShadowNodeFragment{
                .props = descriptor.cloneProps(parserContext, nullptr,
                                               facebook::react::RawProps{folly::dynamic(std::move(props))}),
                .children =
                    std::make_shared<const std::vector<std::shared_ptr<const ShadowNode>>>(std::move(children))},
            family);
    }

    std::shared_ptr<const ShadowNode> makeRawText(const std::string& text) {
        return makeNode("RawText", folly::dynamic::object("text", text), {});
    }

    std::string resolvedComponentName(const std::string& componentName) {
        return registry_->at(componentName).getComponentName();
    }

private:
    const std::shared_ptr<const facebook::react::ContextContainer> contextContainer_ =
        std::make_shared<const facebook::react::ContextContainer>();
    facebook::react::ComponentDescriptorProviderRegistry providerRegistry_;
    facebook::react::ComponentDescriptorRegistry::Shared registry_;
    facebook::react::Tag nextTag_ = 1;
};

/**
 * The prose-styled-prose paragraph every text bundle builds, flattened into the `AttributedString` a paragraph
 * measures and paints: `nestedComponentName` is the name the styled run in the middle is created with.
 */
struct FlattenedParagraph final {
    AttributedString attributedString;
    BaseTextShadowNode::Attachments attachments;
};

FlattenedParagraph flattenParagraphWithNestedRun(BundleNodeFactory& factory, const std::string& nestedComponentName) {
    const std::shared_ptr<const ShadowNode> nested =
        factory.makeNode(nestedComponentName, folly::dynamic::object("fontWeight", "bold")("fontSize", kNestedFontSize),
                         {factory.makeRawText("a styled run")});
    const std::shared_ptr<const ShadowNode> paragraph =
        factory.makeNode("Paragraph", folly::dynamic::object("fontSize", kParagraphFontSize),
                         {factory.makeRawText("Prose with "), nested, factory.makeRawText(" inside it.")});

    const auto* paragraphShadowNode = dynamic_cast<const ParagraphShadowNode*>(paragraph.get());

    EXPECT_NE(paragraphShadowNode, nullptr);

    FlattenedParagraph flattened;

    if (paragraphShadowNode != nullptr) {
        BaseTextShadowNode::buildAttributedString(paragraphShadowNode->getConcreteProps().textAttributes,
                                                  *paragraphShadowNode, flattened.attributedString,
                                                  flattened.attachments);
    }

    return flattened;
}

/**
 * #312: a nested `<Text>` is a styled run of the paragraph, not something embedded in it. Attachments are what
 * make a paragraph unsearchable — `EllipsizeSearch`'s `hasInlineAttachment` refusal — and what make the line
 * limit the only truncation it gets, so "zero attachments" is what `numberOfLines` and every `ellipsizeMode`
 * applying across nested runs rests on.
 */
TEST(NestedTextFragmentTest, NestedTextIsFlattenedIntoStyledFragments) {
    BundleNodeFactory factory;
    const FlattenedParagraph flattened = flattenParagraphWithNestedRun(factory, "RCTVirtualText");
    const AttributedString::Fragments& fragments = flattened.attributedString.getFragments();

    EXPECT_TRUE(flattened.attachments.empty());
    ASSERT_EQ(fragments.size(), 3);

    for (const AttributedString::Fragment& fragment : fragments) {
        EXPECT_FALSE(fragment.isAttachment());
    }

    EXPECT_EQ(fragments[0].string, "Prose with ");
    EXPECT_EQ(fragments[1].string, "a styled run");
    EXPECT_EQ(fragments[2].string, " inside it.");

    EXPECT_EQ(fragments[0].textAttributes.fontSize, kParagraphFontSize);
    EXPECT_EQ(fragments[1].textAttributes.fontSize, kNestedFontSize);
    EXPECT_EQ(fragments[1].textAttributes.fontWeight, facebook::react::FontWeight::Bold);
    EXPECT_EQ(fragments[2].textAttributes.fontSize, kParagraphFontSize);
}

/**
 * The bug itself, pinned so a bundle cannot reintroduce it: `componentNameByReactViewName` rewrites the unified
 * name `"Text"` to `"Paragraph"`, so a node created with it is a nested paragraph — a view-forming node, which
 * `BaseTextShadowNode::buildAttributedString` can only take the attachment branch for.
 */
TEST(NestedTextFragmentTest, UnifiedTextNameResolvesToParagraphAndBecomesAnAttachment) {
    BundleNodeFactory factory;

    EXPECT_EQ(factory.resolvedComponentName("Text"), "Paragraph");
    EXPECT_EQ(factory.resolvedComponentName("RCTVirtualText"), "Text");

    const FlattenedParagraph flattened = flattenParagraphWithNestedRun(factory, "Text");

    EXPECT_EQ(flattened.attachments.size(), 1);
    ASSERT_EQ(flattened.attributedString.getFragments().size(), 3);
    EXPECT_TRUE(flattened.attributedString.getFragments()[1].isAttachment());
}

} // namespace
