#include "ModalHostComponent.h"

#include <memory>
#include <string_view>
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
    return std::make_shared<const facebook::react::ModalHostViewShadowNode::ConcreteState>(
        std::make_shared<const facebook::react::ModalHostViewState>(surfaceSizeOf(
            *contextContainer_->at<std::shared_ptr<const DimensionsSource>>(kDimensionsSourceContextKey))),
        family);
}

facebook::react::Size surfaceSizeOf(const DimensionsSource& dimensions) {
    const DisplayMetrics metrics = dimensions.metrics();

    return facebook::react::Size{.width = static_cast<facebook::react::Float>(metrics.width),
                                 .height = static_cast<facebook::react::Float>(metrics.height)};
}

void requestModalSize(const facebook::react::State::Shared& modalState, facebook::react::Size size,
                      facebook::react::EventQueue::UpdateMode updateMode) {
    const auto state =
        std::static_pointer_cast<const facebook::react::ModalHostViewShadowNode::ConcreteState>(modalState);

    if (state->getData().screenSize != size) {
        state->updateState(facebook::react::ModalHostViewState{size}, updateMode);
    }
}

void resizePresentedModals(const facebook::react::ShadowNode& root, facebook::react::Size surfaceSize) {
    if (std::string_view{root.getComponentName()} == facebook::react::ModalHostViewComponentName) {
        requestModalSize(root.getState(), surfaceSize, facebook::react::EventQueue::UpdateMode::unstable_Immediate);
    }

    for (const std::shared_ptr<const facebook::react::ShadowNode>& child : root.getChildren()) {
        resizePresentedModals(*child, surfaceSize);
    }
}

} // namespace react_native_linux
