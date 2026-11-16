#include "ModalHostComponent.h"

#include "DimensionsSource.h"

#include <memory>
#include <yoga/YGEnums.h>

#include <react/renderer/components/modal/ModalHostViewState.h>
#include <react/renderer/components/view/YogaLayoutableShadowNode.h>
#include <react/renderer/core/EventQueue.h>

namespace react_native_linux {

const char kDimensionsSourceContextKey[] = "react_native_linux::DimensionsSource";

void ModalHostComponentDescriptor::adopt(facebook::react::ShadowNode& shadowNode) const {
    auto& layoutableShadowNode = static_cast<facebook::react::YogaLayoutableShadowNode&>(shadowNode);
    const facebook::react::ModalHostViewState& stateData =
        static_cast<const facebook::react::ModalHostViewShadowNode::ConcreteState&>(*shadowNode.getState()).getData();

    layoutableShadowNode.setSize(stateData.screenSize);
    layoutableShadowNode.setPositionType(YGPositionTypeAbsolute);

    ConcreteComponentDescriptor::adopt(shadowNode);
}

facebook::react::State::Shared
ModalHostComponentDescriptor::createInitialState(const facebook::react::Props::Shared& /*props*/,
                                                 const facebook::react::ShadowNodeFamily::Shared& family) const {
    const DisplayMetrics metrics =
        contextContainer_->at<std::shared_ptr<const DimensionsSource>>(kDimensionsSourceContextKey)->metrics();

    return std::make_shared<const facebook::react::ModalHostViewShadowNode::ConcreteState>(
        std::make_shared<const facebook::react::ModalHostViewState>(
            facebook::react::Size{.width = static_cast<facebook::react::Float>(metrics.width),
                                  .height = static_cast<facebook::react::Float>(metrics.height)}),
        family);
}

void resizePresentedModals(const facebook::react::ShadowNode& root, facebook::react::Size surfaceSize) {
    const auto* modal = dynamic_cast<const facebook::react::ModalHostViewShadowNode*>(&root);

    if (modal != nullptr && modal->getStateData().screenSize != surfaceSize) {
        std::static_pointer_cast<const facebook::react::ModalHostViewShadowNode::ConcreteState>(modal->getState())
            ->updateState(facebook::react::ModalHostViewState{surfaceSize},
                          facebook::react::EventQueue::UpdateMode::unstable_Immediate);
    }

    for (const std::shared_ptr<const facebook::react::ShadowNode>& child : root.getChildren()) {
        resizePresentedModals(*child, surfaceSize);
    }
}

} // namespace react_native_linux
