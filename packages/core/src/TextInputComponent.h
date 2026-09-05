#pragma once

#include <react/renderer/components/textinput/BaseTextInputProps.h>
#include <react/renderer/components/textinput/BaseTextInputShadowNode.h>
#include <react/renderer/components/textinput/TextInputEventEmitter.h>
#include <react/renderer/components/textinput/TextInputState.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include <react/renderer/core/PropsParserContext.h>
#include <react/renderer/core/RawProps.h>
#include <react/renderer/core/ShadowNode.h>
#include <react/renderer/textlayoutmanager/TextLayoutManager.h>

#include <memory>

namespace react_native_linux {

/**
 * `"TextInput"`, which is the component name React Native's own `TextInput.js` registers on every platform and
 * the name `isTextInputComponent` already looks for when it decides whether focus should enable the compositor's
 * text input.
 */
extern const char kTextInputComponentName[];

/**
 * The `<TextInput>` props for this platform.
 *
 * Everything a text field has in common across platforms is `BaseTextInputProps`, which upstream ships in
 * `react/renderer/components/textinput` with no platform directory of its own. What upstream does **not** ship
 * there is `secureTextEntry`: iOS carries it inside `TextInputTraits` on its own `TextInputProps` and Android
 * carries it on `AndroidTextInputProps`, and both of those live under a `platform/` directory that this platform
 * has no business compiling. So this class is the platform half, and it is deliberately three booleans rather
 * than a traits struct — the second consumer that would justify one does not exist.
 */
class TextInputProps final : public facebook::react::BaseTextInputProps {
public:
    TextInputProps() = default;
    TextInputProps(const facebook::react::PropsParserContext& context, const TextInputProps& sourceProps,
                   const facebook::react::RawProps& rawProps);

    bool secureTextEntry{false};
    bool caretHidden{false};
    bool selectTextOnFocus{false};
};

/**
 * The `<TextInput>` shadow node for this platform: upstream's `BaseTextInputShadowNode` with our props on it.
 *
 * The whole of the measurement, the state and the placeholder handling is upstream's — `measureContent` measures
 * the state's attributed string through the `TextLayoutManager`, `getTextConstraints` gives a single-line field
 * an infinite measuring width so it scrolls rather than wraps, and `updateStateIfNeeded` replaces the state when
 * and only when the string built from the React tree changed. That last rule is what makes the platform's own
 * state writes survive: an edit publishes a new `attributedStringBox` and leaves `reactTreeAttributedString`
 * alone, so the next layout does not overwrite it.
 */
class TextInputShadowNode final
    : public facebook::react::BaseTextInputShadowNode<kTextInputComponentName, TextInputProps,
                                                     facebook::react::TextInputEventEmitter,
                                                     facebook::react::TextInputState> {
public:
    using BaseTextInputShadowNode::BaseTextInputShadowNode;

    /**
     * The initial state carries the effective text attributes on its `reactTreeAttributedString`, with the
     * font-size multiplier the root's default `LayoutContext` measures with, so the first platform-side edit
     * makes the state "meaningful" to upstream's `attributedStringBoxToMeasure`.
     *
     * Upstream's `updateStateIfNeeded` is what normally writes those attributes, but it skips a field whose
     * React-tree text is empty — `AttributedString::appendFragment` drops an empty fragment, so the fresh string
     * is content-equal to the empty one the state starts with — and the multiplier then stays NaN. NaN compares
     * unequal to everything, so the state is never consulted and the field measures its placeholder no matter
     * what is typed: an uncontrolled multiline field never grows, which is core#54570
     * (https://github.com/facebook/react-native/issues/54570).
     */
    static facebook::react::TextInputState initialStateData(const facebook::react::Props::Shared& props,
                                                            const facebook::react::ShadowNodeFamily::Shared& family,
                                                            const facebook::react::ComponentDescriptor& descriptor);
};

/**
 * The descriptor, which is also what constructs the `TextLayoutManager` a field measures with — the same
 * arrangement `TextInputComponentDescriptor` makes on iOS and `ParagraphComponentDescriptor` makes for
 * `<Paragraph>`, so a field and a paragraph measure through one implementation of SkParagraph.
 */
class TextInputComponentDescriptor final : public facebook::react::ConcreteComponentDescriptor<TextInputShadowNode> {
public:
    explicit TextInputComponentDescriptor(const facebook::react::ComponentDescriptorParameters& parameters);

protected:
    void adopt(facebook::react::ShadowNode& shadowNode) const override;

private:
    const std::shared_ptr<facebook::react::TextLayoutManager> textLayoutManager_;
};

} // namespace react_native_linux
