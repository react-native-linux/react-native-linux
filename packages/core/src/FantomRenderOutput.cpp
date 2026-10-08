#include "FantomRenderOutput.h"

#include <folly/dynamic.h>
#include <folly/json.h>
#include <string>
#include <string_view>

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/components/text/ParagraphState.h>
#include <react/renderer/core/ConcreteState.h>
#include <react/renderer/debug/flags.h>

namespace react_native_linux {

namespace {

#if RN_DEBUG_STRING_CONVERTIBLE
folly::dynamic renderProps(const facebook::react::SharedDebugStringConvertibleList& propsList) {
    folly::dynamic props = folly::dynamic::object;

    for (const auto& prop : propsList) {
        if (prop) {
            props[prop->getDebugName()] = prop->getDebugValue();
        }
    }

    return props;
}
#endif

folly::dynamic renderAttributedString(facebook::react::Tag selfTag,
                                      const facebook::react::AttributedString& attributedString) {
    folly::dynamic fragments = folly::dynamic::array;

    for (const auto& fragment : attributedString.getFragments()) {
        if (fragment.parentShadowView.tag == selfTag) {
            fragments.push_back(fragment.string);
            continue;
        }

        folly::dynamic text = folly::dynamic::object;

        text["type"] = "Text";
        text["children"] = fragment.string;
#if RN_DEBUG_STRING_CONVERTIBLE
        text["props"] = renderProps(fragment.textAttributes.getDebugProps());
#else
        text["props"] = folly::dynamic::object;
#endif
        fragments.push_back(text);
    }

    return fragments;
}

folly::dynamic renderView(const facebook::react::StubView& view, bool includeLayoutMetrics) {
    folly::dynamic element = folly::dynamic::object;

    element["type"] = view.componentName;
#if RN_DEBUG_STRING_CONVERTIBLE
    folly::dynamic props = renderProps(view.props->getDebugProps());
#else
    folly::dynamic props = folly::dynamic::object;
#endif

    if (std::string_view(view.componentName) == "Paragraph") {
        const auto& state =
            static_cast<const facebook::react::ConcreteState<facebook::react::ParagraphState>&>(*view.state);

        element["children"] = renderAttributedString(view.tag, state.getData().attributedString);
    } else {
        element["children"] = folly::dynamic::array;

        for (const auto& child : view.children) {
            element["children"].push_back(renderView(*child, includeLayoutMetrics));
        }
    }

#if RN_DEBUG_STRING_CONVERTIBLE
    if (includeLayoutMetrics) {
        for (const auto& prop : facebook::react::getDebugProps(view.layoutMetrics, {})) {
            props["layoutMetrics-" + prop.name] = prop.value;
        }
    }
#endif

    element["props"] = props;

    return element;
}

} // namespace

std::string renderFantomOutput(const facebook::react::StubViewTree& tree, bool includeRoot, bool includeLayoutMetrics) {
    const facebook::react::StubView& root = tree.getRootStubView();

    if (includeRoot) {
        return folly::toJson(renderView(root, includeLayoutMetrics));
    }

    if (root.children.size() == 1) {
        return folly::toJson(renderView(*root.children.front(), includeLayoutMetrics));
    }

    folly::dynamic children = folly::dynamic::array;

    for (const auto& child : root.children) {
        children.push_back(renderView(*child, includeLayoutMetrics));
    }

    return folly::toJson(children);
}

} // namespace react_native_linux
