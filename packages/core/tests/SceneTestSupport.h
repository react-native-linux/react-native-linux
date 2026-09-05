#pragma once

#include "LinuxMountingManager.h"
#include "RetainedScene.h"
#include "TextInputComponent.h"

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/AttributedStringBox.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>
#include <react/renderer/attributedstring/TextAttributes.h>
#include <react/renderer/components/image/ImageProps.h>
#include <react/renderer/components/image/ImageState.h>
#include <react/renderer/components/scrollview/ScrollViewState.h>
#include <react/renderer/components/text/ParagraphState.h>
#include <react/renderer/components/textinput/TextInputState.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteState.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/core/ShadowNodeFamily.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/graphics/Size.h>
#include <react/renderer/graphics/Transform.h>
#include <react/renderer/graphics/ValueUnit.h>
#include <react/renderer/imagemanager/ImageRequest.h>
#include <react/renderer/imagemanager/ImageRequestParams.h>
#include <react/renderer/imagemanager/primitives.h>
#include <react/renderer/mounting/MountingTransaction.h>
#include <react/renderer/mounting/ShadowView.h>
#include <react/renderer/mounting/ShadowViewMutation.h>
#include <react/renderer/telemetry/TransactionTelemetry.h>

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Shared fixture builders for the `packages/core/tests` GoogleTest suites that exercise `RetainedScene` and
// `LinuxMountingManager` directly — `SceneTest.cpp`, `SceneReuseTest.cpp` and
// `LinuxMountingManagerDiagnosticsTest.cpp`. They build the same handful of
// `ShadowView` shapes (a plain view, a painted view, a paragraph, an image, a ScrollView, a TextInput) off the
// same upstream state types, so this header is the one definition of each rather than two that could drift.
//
// Anonymous namespace rather than `inline` functions in a named one: every translation unit that includes this
// header gets its own internal-linkage copy, exactly as if the `.cpp` still wrote these itself, so there is
// nothing new to reason about across the two object files this links into.

namespace {

// The names every suite that includes this header spells unqualified. They live here for the reason the builders
// below do: one list rather than one per file, which is also the only way the duplication check stays quiet about
// a block that is identical by definition.
using facebook::react::MountingTransaction;
using facebook::react::Point;
using facebook::react::Rect;
using facebook::react::SharedColor;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::ShadowViewMutationList;
using facebook::react::Size;
using facebook::react::Tag;
using facebook::react::Transform;
using facebook::react::UnitType;
using facebook::react::ValueUnit;
using facebook::react::ViewProps;
using react_native_linux::LinuxMountingManager;
using react_native_linux::RetainedScene;
using react_native_linux::SceneDamage;
using react_native_linux::SceneEditorState;
using react_native_linux::SceneFrame;
using react_native_linux::findDisplacedPrimitive;
using react_native_linux::ScenePrimitive;
using react_native_linux::ScenePrimitiveDisplacement;
using react_native_linux::SceneSnapshot;

constexpr facebook::react::Tag kSurfaceTag = 1;
constexpr uint32_t kBlueArgb = 0xFF3366CCU;
constexpr uint32_t kRedArgb = 0xFFCC3333U;

facebook::react::Rect makeRect(float x, float y, float width, float height) {
    return facebook::react::Rect{.origin = facebook::react::Point{.x = x, .y = y},
                                 .size = facebook::react::Size{.width = width, .height = height}};
}

facebook::react::SharedColor blue() {
    return facebook::react::colorFromRGBA(51, 102, 204, 255);
}

facebook::react::SharedColor red() {
    return facebook::react::colorFromRGBA(204, 51, 51, 255);
}

facebook::react::ShadowView makeView(facebook::react::Tag tag, facebook::react::Rect frame) {
    facebook::react::ShadowView shadowView;

    shadowView.tag = tag;
    shadowView.componentName = "View";
    shadowView.layoutMetrics.frame = frame;

    return shadowView;
}

facebook::react::ShadowView makeStyledView(facebook::react::Tag tag, facebook::react::Rect frame,
                                           const std::shared_ptr<facebook::react::ViewProps>& viewProps) {
    facebook::react::ShadowView shadowView = makeView(tag, frame);

    shadowView.props = viewProps;

    return shadowView;
}

std::shared_ptr<facebook::react::ViewProps> propsWithBackground(facebook::react::SharedColor backgroundColor) {
    const std::shared_ptr<facebook::react::ViewProps> viewProps = std::make_shared<facebook::react::ViewProps>();

    viewProps->backgroundColor = backgroundColor;

    return viewProps;
}

facebook::react::ShadowView makePaintedView(facebook::react::Tag tag, facebook::react::Rect frame,
                                            facebook::react::SharedColor backgroundColor) {
    return makeStyledView(tag, frame, propsWithBackground(backgroundColor));
}

/**
 * A `<Paragraph>` as it reaches the mounting layer: the nested `<Text>` and `<RawText>` nodes never do, so the
 * flattened `AttributedString` arrives inside `ParagraphState`. The family is an empty weak pointer because
 * nothing here dispatches a state update; `ConcreteState` only locks it for that. `maximumNumberOfLines` defaults
 * to unlimited, which is every caller but the ones asserting on it directly.
 */
facebook::react::ShadowView makeParagraph(facebook::react::Tag tag, facebook::react::Rect frame,
                                          const std::string& text, int maximumNumberOfLines = 0) {
    facebook::react::AttributedString attributedString;

    if (!text.empty()) {
        facebook::react::AttributedString::Fragment fragment;

        fragment.string = text;
        fragment.textAttributes = facebook::react::TextAttributes::defaultTextAttributes();
        attributedString.appendFragment(std::move(fragment));
    }

    facebook::react::ParagraphAttributes paragraphAttributes;

    paragraphAttributes.maximumNumberOfLines = maximumNumberOfLines;

    facebook::react::ShadowView shadowView;

    shadowView.tag = tag;
    shadowView.componentName = "Paragraph";
    shadowView.layoutMetrics.frame = frame;
    shadowView.state = std::make_shared<const facebook::react::ConcreteState<facebook::react::ParagraphState>>(
        std::make_shared<const facebook::react::ParagraphState>(
            facebook::react::ParagraphState{attributedString, paragraphAttributes, {}}),
        facebook::react::ShadowNodeFamily::Weak{});

    return shadowView;
}

/**
 * An `<Image>` as it reaches the mounting layer: the fit and the tint stay on `ImageProps`, and the source is on
 * `ImageState`, because `ImageShadowNode` is what chooses it and what hands it to `ImageManager::requestImage`.
 */
facebook::react::ShadowView makeImage(facebook::react::Tag tag, facebook::react::Rect frame,
                                      const std::string& uri, facebook::react::ImageResizeMode resizeMode,
                                      facebook::react::SharedColor tintColor) {
    const std::shared_ptr<facebook::react::ImageProps> imageProps =
        std::make_shared<facebook::react::ImageProps>();

    imageProps->resizeMode = resizeMode;
    imageProps->tintColor = tintColor;

    facebook::react::ImageSource imageSource;

    imageSource.type = facebook::react::ImageSource::Type::Local;
    imageSource.uri = uri;

    facebook::react::ShadowView shadowView;

    shadowView.tag = tag;
    shadowView.componentName = "Image";
    shadowView.layoutMetrics.frame = frame;
    shadowView.props = imageProps;
    shadowView.state = std::make_shared<const facebook::react::ConcreteState<facebook::react::ImageState>>(
        std::make_shared<const facebook::react::ImageState>(
            imageSource, facebook::react::ImageRequest{imageSource, nullptr},
            facebook::react::ImageRequestParams{}),
        facebook::react::ShadowNodeFamily::Weak{});

    return shadowView;
}

// The `resizeMode`-less shape every caller that never varies it wants — every `SceneReuseTest.cpp` image case.
facebook::react::ShadowView makeImage(facebook::react::Tag tag, facebook::react::Rect frame,
                                      const std::string& uri, facebook::react::SharedColor tintColor) {
    return makeImage(tag, frame, uri, facebook::react::ImageResizeMode::Cover, tintColor);
}

/**
 * A `<ScrollView>` as it reaches the mounting layer. `ScrollViewState` is where the scroll position lives — the
 * platform writes it and Fabric mounts it — so the state pointer is what makes a node one, exactly as
 * `ParagraphState` is what makes a node a paragraph.
 */
facebook::react::ShadowView makeScrollView(facebook::react::Tag tag, facebook::react::Rect frame,
                                           facebook::react::Point contentOffset,
                                           facebook::react::Rect contentBoundingRect) {
    facebook::react::ShadowView shadowView;

    shadowView.tag = tag;
    shadowView.componentName = "ScrollView";
    shadowView.layoutMetrics.frame = frame;
    shadowView.state = std::make_shared<const facebook::react::ConcreteState<facebook::react::ScrollViewState>>(
        std::make_shared<const facebook::react::ScrollViewState>(
            facebook::react::ScrollViewState{contentOffset, contentBoundingRect, 0}),
        facebook::react::ShadowNodeFamily::Weak{});

    return shadowView;
}

std::shared_ptr<react_native_linux::TextInputProps> textInputProps() {
    return std::make_shared<react_native_linux::TextInputProps>();
}

/**
 * A `<TextInput>` as it reaches the mounting layer. The value lives in `TextInputState` for the reason a
 * ScrollView's offset lives in its own state: the platform writes it back there after every edit, so it is the
 * one description of the field that the picture and React share. See *TextInput* in docs/cpp-toolchain.md.
 */
facebook::react::ShadowView makeTextInput(facebook::react::Tag tag, facebook::react::Rect frame,
                                          const std::string& value,
                                          const std::shared_ptr<react_native_linux::TextInputProps>& props) {
    facebook::react::AttributedString attributedString;

    if (!value.empty()) {
        facebook::react::AttributedString::Fragment fragment;

        fragment.string = value;
        fragment.textAttributes = facebook::react::TextAttributes::defaultTextAttributes();
        attributedString.appendFragment(std::move(fragment));
    }

    facebook::react::ShadowView shadowView;

    shadowView.tag = tag;
    shadowView.componentName = "TextInput";
    shadowView.layoutMetrics.frame = frame;
    shadowView.props = props;
    shadowView.state = std::make_shared<const facebook::react::ConcreteState<facebook::react::TextInputState>>(
        std::make_shared<const facebook::react::TextInputState>(
            facebook::react::TextInputState{facebook::react::AttributedStringBox{attributedString}, attributedString,
                                            facebook::react::ParagraphAttributes{}, 0}),
        facebook::react::ShadowNodeFamily::Weak{});

    return shadowView;
}

void addChild(react_native_linux::RetainedScene& scene, facebook::react::Tag parentTag,
             const facebook::react::ShadowView& child) {
    scene.createNode(child);
    scene.insertChild(parentTag, child, 0);
}

facebook::react::MountingTransaction transactionOf(facebook::react::ShadowViewMutationList&& mutations) {
    return facebook::react::MountingTransaction{kSurfaceTag, 1, std::move(mutations),
                                                facebook::react::TransactionTelemetry{}};
}

void mountChildAndTakeFrame(react_native_linux::LinuxMountingManager& mountingManager,
                            const facebook::react::ShadowView& child) {
    facebook::react::ShadowViewMutationList mutations{
        facebook::react::ShadowViewMutation::CreateMutation(child),
        facebook::react::ShadowViewMutation::InsertMutation(kSurfaceTag, child, 0)};

    mountingManager.startSurface(kSurfaceTag, facebook::react::Size{.width = 800, .height = 600});
    mountingManager.executeMount(kSurfaceTag, transactionOf(std::move(mutations)));
    mountingManager.takeFrame();
}

} // namespace
