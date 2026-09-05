#include "TextInputComponent.h"

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/AttributedStringBox.h>
#include <react/renderer/core/ComponentDescriptor.h>
#include <react/renderer/core/propsConversions.h>

#include <memory>

namespace react_native_linux {

const char kTextInputComponentName[] = "TextInput";

TextInputProps::TextInputProps(const facebook::react::PropsParserContext& context, const TextInputProps& sourceProps,
                               const facebook::react::RawProps& rawProps)
    : facebook::react::BaseTextInputProps(context, sourceProps, rawProps),
      secureTextEntry(
          facebook::react::convertRawProp(context, rawProps, "secureTextEntry", sourceProps.secureTextEntry, {false})),
      caretHidden(facebook::react::convertRawProp(context, rawProps, "caretHidden", sourceProps.caretHidden, {false})),
      selectTextOnFocus(facebook::react::convertRawProp(context, rawProps, "selectTextOnFocus",
                                                        sourceProps.selectTextOnFocus, {false})),
      scrollEnabled(
          facebook::react::convertRawProp(context, rawProps, "scrollEnabled", sourceProps.scrollEnabled, {true})) {}

// `LayoutContext::fontSizeMultiplier` defaults to 1 and this host never sets another; upstream compares the
// state's multiplier against the root's, so the initial state has to carry the same value.
constexpr facebook::react::Float kDefaultFontSizeMultiplier = 1.0F;

facebook::react::TextInputState TextInputShadowNode::initialStateData(
    const facebook::react::Props::Shared& props, const facebook::react::ShadowNodeFamily::Shared& /*family*/,
    const facebook::react::ComponentDescriptor& /*descriptor*/) {
    const auto& textInputProps = static_cast<const TextInputProps&>(*props);
    facebook::react::AttributedString reactTreeAttributedString;

    reactTreeAttributedString.setBaseTextAttributes(
        textInputProps.getEffectiveTextAttributes(kDefaultFontSizeMultiplier));

    return facebook::react::TextInputState{facebook::react::AttributedStringBox{}, reactTreeAttributedString,
                                           textInputProps.paragraphAttributes, textInputProps.mostRecentEventCount};
}

TextInputComponentDescriptor::TextInputComponentDescriptor(
    const facebook::react::ComponentDescriptorParameters& parameters)
    : facebook::react::ConcreteComponentDescriptor<TextInputShadowNode>(parameters),
      textLayoutManager_(std::make_shared<facebook::react::TextLayoutManager>(contextContainer_)) {}

void TextInputComponentDescriptor::adopt(facebook::react::ShadowNode& shadowNode) const {
    facebook::react::ConcreteComponentDescriptor<TextInputShadowNode>::adopt(shadowNode);

    static_cast<TextInputShadowNode&>(shadowNode).setTextLayoutManager(textLayoutManager_);
}

} // namespace react_native_linux
