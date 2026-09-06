#pragma once

#include <react/renderer/components/FBReactNativeSpec/EventEmitters.h>
#include <react/renderer/components/FBReactNativeSpec/Props.h>
#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include <react/renderer/core/LayoutConstraints.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/renderer/core/ShadowNode.h>
#include <react/renderer/graphics/Size.h>

namespace react_native_linux {

/**
 * `"Switch"`, which is the name `SwitchNativeComponent.js` registers on every platform that is not Android — and
 * this one is not, so React Native's own `Switch.js` takes that branch and sends `value`, `disabled`,
 * `tintColor`, `onTintColor` and `thumbTintColor`.
 */
extern const char kSwitchComponentName[];

/**
 * The `<Switch>` shadow node for this platform: upstream's generated `SwitchProps` and `SwitchEventEmitter` on a
 * leaf node that measures at the control's own size.
 *
 * Codegen stops at the props and the emitter for this component, because `SwitchNativeComponent.js` declares it
 * `interfaceOnly` — so upstream generates no shadow node and every platform writes this much itself.
 * `AppleSwitchShadowNode` is the same seven lines with `RCTSwitchSize()` where this has a constant.
 *
 * The node is a measurable leaf because `Switch.js` gives it `alignSelf: flex-start` and no size at all: without
 * an intrinsic measurement Yoga would lay a switch out as a zero-width box, which is react-native-macos#1699.
 */
class SwitchShadowNode final
    : public facebook::react::ConcreteViewShadowNode<kSwitchComponentName, facebook::react::SwitchProps,
                                                     facebook::react::SwitchEventEmitter> {
public:
    using ConcreteViewShadowNode::ConcreteViewShadowNode;

    static facebook::react::ShadowNodeTraits BaseTraits();

    facebook::react::Size measureContent(const facebook::react::LayoutContext& layoutContext,
                                         const facebook::react::LayoutConstraints& layoutConstraints) const override;
};

using SwitchComponentDescriptor = facebook::react::ConcreteComponentDescriptor<SwitchShadowNode>;

} // namespace react_native_linux
