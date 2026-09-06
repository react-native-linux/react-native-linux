#include "SwitchComponent.h"

#include "SwitchContent.h"

namespace react_native_linux {

const char kSwitchComponentName[] = "Switch";

facebook::react::ShadowNodeTraits SwitchShadowNode::BaseTraits() {
    facebook::react::ShadowNodeTraits traits = ConcreteViewShadowNode::BaseTraits();

    traits.set(facebook::react::ShadowNodeTraits::Trait::LeafYogaNode);
    traits.set(facebook::react::ShadowNodeTraits::Trait::MeasurableYogaNode);

    return traits;
}

facebook::react::Size SwitchShadowNode::measureContent(
    const facebook::react::LayoutContext& /*layoutContext*/,
    const facebook::react::LayoutConstraints& /*layoutConstraints*/) const {
    return kSwitchSize;
}

} // namespace react_native_linux
