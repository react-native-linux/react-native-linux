#include "TextInputComponent.h"

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
                                                        sourceProps.selectTextOnFocus, {false})) {}

TextInputComponentDescriptor::TextInputComponentDescriptor(
    const facebook::react::ComponentDescriptorParameters& parameters)
    : facebook::react::ConcreteComponentDescriptor<TextInputShadowNode>(parameters),
      textLayoutManager_(std::make_shared<facebook::react::TextLayoutManager>(contextContainer_)) {}

void TextInputComponentDescriptor::adopt(facebook::react::ShadowNode& shadowNode) const {
    facebook::react::ConcreteComponentDescriptor<TextInputShadowNode>::adopt(shadowNode);

    static_cast<TextInputShadowNode&>(shadowNode).setTextLayoutManager(textLayoutManager_);
}

} // namespace react_native_linux
