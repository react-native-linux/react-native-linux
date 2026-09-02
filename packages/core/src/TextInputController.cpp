#include "TextInputController.h"

#include "Clipboard.h"

#ifdef RNL_ENABLE_TEXT_GEOMETRY
#include "TextGeometry.h"
#endif

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/AttributedStringBox.h>
#include <react/renderer/attributedstring/TextAttributes.h>
#include <react/renderer/components/textinput/TextInputState.h>
#include <react/renderer/components/textinput/basePrimitives.h>
#include <react/renderer/core/ConcreteState.h>
#include <react/renderer/core/LayoutMetrics.h>
#include <react/renderer/core/LayoutPrimitives.h>
#include <react/renderer/core/StateData.h>
#include <react/renderer/graphics/Float.h>
#include <react/renderer/graphics/Size.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

namespace react_native_linux {

namespace {

// Roughly the blink period every desktop toolkit uses; GTK's own default is 1200 ms for a full cycle.
constexpr double kCaretBlinkMilliseconds = 600.0;
constexpr char kSelectAllKey[] = "a";
constexpr char kCopyKey[] = "c";
constexpr char kCutKey[] = "x";
constexpr char kPasteKey[] = "v";
constexpr char kLeftKey[] = "ArrowLeft";
constexpr char kRightKey[] = "ArrowRight";
constexpr char kHomeKey[] = "Home";
constexpr char kEndKey[] = "End";
constexpr char kBackspaceKey[] = "Backspace";
constexpr char kDeleteKey[] = "Delete";
constexpr char kEnterKey[] = "Enter";
constexpr char kEscapeKey[] = "Escape";
// The text upstream's `TextInputEventEmitter` turns back into the DOM names `onKeyPress` reports: an empty
// string becomes `Backspace` and a line feed becomes `Enter`. See `keyPressMetricsPayload`.
constexpr char kBackspaceKeyPressText[] = "";
constexpr char kNewlineText[] = "\n";

facebook::react::AttributedString makeAttributedString(const TextInputProps& props, const std::string& text) {
    facebook::react::AttributedString attributedString;
    const facebook::react::TextAttributes textAttributes = props.getEffectiveTextAttributes(1.0F);

    attributedString.setBaseTextAttributes(textAttributes);

    if (!text.empty()) {
        attributedString.appendFragment(facebook::react::AttributedString::Fragment{
            .string = text, .textAttributes = textAttributes, .parentShadowView = {}});
    }

    return attributedString;
}

CaretMotion horizontalMotion(const std::string& key, bool isWordMotion) {
    if (key == kLeftKey) {
        return isWordMotion ? CaretMotion::WordLeft : CaretMotion::Left;
    }

    return isWordMotion ? CaretMotion::WordRight : CaretMotion::Right;
}

/**
 * The field's own event emitter. Every `<TextInput>` has one, but the cast is what says so, and a shadow view
 * built by a test fixture or a bundle that never registered a handler has none.
 */
std::shared_ptr<const facebook::react::TextInputEventEmitter> emitterOf(const TextInputShadowNode& shadowNode) {
    return std::dynamic_pointer_cast<const facebook::react::TextInputEventEmitter>(shadowNode.getEventEmitter());
}

bool isPointInside(const facebook::react::Rect& box, facebook::react::Point point) {
    return point.x >= box.origin.x && point.y >= box.origin.y && point.x <= box.origin.x + box.size.width &&
           point.y <= box.origin.y + box.size.height;
}

/**
 * ICU's boundaries when Skia is linked, and the code-point fallback when it is not. Without Skia every paragraph
 * already measures as zero, so a field has no geometry either way; what a caret still does correctly without it
 * is refuse to split a UTF-8 sequence.
 */
TextSegmenter fieldSegmenter() {
#ifdef RNL_ENABLE_TEXT_GEOMETRY
    return segmentText;
#else
    return {};
#endif
}

} // namespace

TextInputController::TextInputController(std::shared_ptr<facebook::react::UIManager> uiManager,
                                         std::shared_ptr<LinuxMountingManager> mountingManager)
    : uiManager_(std::move(uiManager)), mountingManager_(std::move(mountingManager)) {}

void TextInputController::setMountedFields(
    const std::vector<std::shared_ptr<const TextInputShadowNode>>& shadowNodes) {
    std::unordered_map<facebook::react::Tag, TextInputField> mounted;

    mounted.reserve(shadowNodes.size());

    for (const std::shared_ptr<const TextInputShadowNode>& shadowNode : shadowNodes) {
        const auto existing = fields_.find(shadowNode->getTag());

        if (existing != fields_.end()) {
            existing->second.shadowNode = shadowNode;
            mounted.emplace(shadowNode->getTag(), std::move(existing->second));

            continue;
        }

        // A field is seeded from its own state rather than from zero, so a `defaultValue` and a mounted `value`
        // are there to be edited rather than replaced by the first keystroke.
        const std::string reactTreeText = shadowNode->getStateData().reactTreeAttributedString.getString();
        TextInputField field;

        field.shadowNode = shadowNode;
        field.editor.setSegmenter(fieldSegmenter());
        field.editor.setText(reactTreeText);
        field.editor.setSelectionRange(reactTreeText.size(), reactTreeText.size());
        field.reactTreeText = reactTreeText;
        field.emittedText = reactTreeText;
        mounted.emplace(shadowNode->getTag(), std::move(field));
    }

    fields_ = std::move(mounted);
}

void TextInputController::setFocusedNode(const std::shared_ptr<const facebook::react::ShadowNode>& shadowNode) {
    const std::shared_ptr<const TextInputShadowNode> textInput =
        std::dynamic_pointer_cast<const TextInputShadowNode>(shadowNode);

    isSelectingByPointer_ = false;
    isCaretVisible_ = true;
    blinkMilliseconds_ = 0.0;
    focusedTag_ = textInput == nullptr ? 0 : textInput->getTag();
}

bool TextInputController::isComposing() const {
    const TextInputField* field = focusedField();

    return field != nullptr && field->editor.isComposing();
}

TextInputKeyResult TextInputController::handleKey(const InputEvent& event) {
    if (event.kind != InputEventKind::KeyPress) {
        return TextInputKeyResult::Ignored;
    }

    TextInputField* field = focusedField();

    if (field == nullptr) {
        return TextInputKeyResult::Ignored;
    }

    const TextInputProps& props = field->shadowNode->getConcreteProps();
    const bool isEditable = props.editable && !props.readOnly;

    isCaretVisible_ = true;
    blinkMilliseconds_ = 0.0;

    // Ctrl and an arrow key is word motion rather than a shortcut, so it goes to the motion rules below.
    if (event.modifiers.control && !props.disableKeyboardShortcuts && event.key != kLeftKey &&
        event.key != kRightKey) {
        return handleShortcut(event, *field, isEditable);
    }

    const TextInputKeyResult namedKeyResult = handleNamedKey(event, *field, isEditable);

    if (namedKeyResult != TextInputKeyResult::Ignored) {
        return namedKeyResult;
    }

    if (!isTextKey(event.key) || event.modifiers.control || event.modifiers.meta) {
        return TextInputKeyResult::Ignored;
    }

    if (isEditable) {
        emitKeyPress(*field, event.key);
        field->editor.insertText(event.key);
    }

    return TextInputKeyResult::Consumed;
}

/**
 * Ctrl and a letter, which on this platform reaches the focused field and never the application —
 * react-native-macos#2075 is the bug filed when it reached neither.
 *
 * Ctrl+Z is deliberately **not** consumed. There is no undo stack yet, and swallowing a key that does nothing
 * would make undo look implemented and broken rather than absent; see the deferrals in *TextInput* in
 * docs/cpp-toolchain.md.
 */
TextInputKeyResult TextInputController::handleShortcut(const InputEvent& event, TextInputField& field,
                                                       bool isEditable) {
    if (event.key == kSelectAllKey) {
        field.editor.selectAll();

        return TextInputKeyResult::Consumed;
    }

    if (event.key == kCopyKey) {
        setClipboardText(field.editor.selectedText());

        return TextInputKeyResult::Consumed;
    }

    if (event.key == kCutKey) {
        setClipboardText(field.editor.selectedText());

        if (isEditable) {
            field.editor.deleteBackward();
        }

        return TextInputKeyResult::Consumed;
    }

    if (event.key == kPasteKey) {
        if (isEditable) {
            field.editor.insertText(clipboardText());
        }

        return TextInputKeyResult::Consumed;
    }

    return TextInputKeyResult::Ignored;
}

/**
 * The keys whose DOM value is a name rather than a character.
 *
 * Tab is absent on purpose, and that absence is the decision: Tab always moves focus, in a multiline field as
 * well as a single-line one, because a field that inserted a tab would be a field with no keyboard way out.
 * Escape blurs, which is the other half of the same decision, and the arrow keys that move by line are not here
 * because vertical motion needs line geometry — both are written down in *TextInput* in docs/cpp-toolchain.md,
 * which is what react-native-macos#1082 asked for.
 */
TextInputKeyResult TextInputController::handleNamedKey(const InputEvent& event, TextInputField& field,
                                                       bool isEditable) {
    if (event.key == kLeftKey || event.key == kRightKey) {
        field.editor.moveCaret(horizontalMotion(event.key, event.modifiers.control), event.modifiers.shift);

        return TextInputKeyResult::Consumed;
    }

    if (event.key == kHomeKey || event.key == kEndKey) {
        field.editor.moveCaret(event.key == kHomeKey ? CaretMotion::LineStart : CaretMotion::LineEnd,
                               event.modifiers.shift);

        return TextInputKeyResult::Consumed;
    }

    if (event.key == kBackspaceKey) {
        if (isEditable) {
            emitKeyPress(field, kBackspaceKeyPressText);
            field.editor.deleteBackward();
        }

        return TextInputKeyResult::Consumed;
    }

    if (event.key == kDeleteKey) {
        if (isEditable) {
            field.editor.deleteForward();
        }

        return TextInputKeyResult::Consumed;
    }

    if (event.key == kEscapeKey) {
        return TextInputKeyResult::ConsumedAndBlurred;
    }

    if (event.key != kEnterKey) {
        return TextInputKeyResult::Ignored;
    }

    const facebook::react::SubmitBehavior submitBehavior =
        field.shadowNode->getConcreteProps().getNonDefaultSubmitBehavior();

    if (submitBehavior == facebook::react::SubmitBehavior::Newline) {
        if (isEditable) {
            emitKeyPress(field, kNewlineText);
            field.editor.insertText(kNewlineText);
        }

        return TextInputKeyResult::Consumed;
    }

    emitKeyPress(field, kNewlineText);
    emitSubmit(field);

    return submitBehavior == facebook::react::SubmitBehavior::BlurAndSubmit ? TextInputKeyResult::ConsumedAndBlurred
                                                                           : TextInputKeyResult::Consumed;
}

void TextInputController::handlePointer(const InputEvent& event) {
    if (event.kind == InputEventKind::PointerButtonRelease) {
        isSelectingByPointer_ = false;

        return;
    }

    const bool isPress = event.kind == InputEventKind::PointerButtonPress;
    const bool isDrag = event.kind == InputEventKind::PointerMotion && isSelectingByPointer_;

    if (!isPress && !isDrag) {
        return;
    }

    TextInputField* field = focusedField();

    if (field == nullptr) {
        return;
    }

    const facebook::react::Rect box = contentBox(*field->shadowNode);

    if (isPress && !isPointInside(box, event.surfacePoint)) {
        return;
    }

    isCaretVisible_ = true;
    blinkMilliseconds_ = 0.0;
    isSelectingByPointer_ = isSelectingByPointer_ || isPress;
    placeCaretAtPoint(*field, box, event.surfacePoint, isDrag);
}

#ifdef RNL_ENABLE_TEXT_GEOMETRY

void TextInputController::placeCaretAtPoint(TextInputField& field, const facebook::react::Rect& box,
                                            facebook::react::Point surfacePoint, bool isExtending) {
    const std::string displayed = field.editor.displayText();
    const facebook::react::Point localPoint{.x = surfacePoint.x - box.origin.x + field.scrollOffsetX,
                                            .y = surfacePoint.y - box.origin.y};
    const TextInputProps& props = field.shadowNode->getConcreteProps();
    // The width the last frame measured this field against, so a click lands on the glyph that was drawn rather
    // than on the one a differently wrapped layout would have put there. A field that has never been published
    // has no such width yet, and its content box is the closest thing to one.
    const float layoutWidth =
        field.layoutWidth > 0.0F ? field.layoutWidth : static_cast<float>(box.size.width);
    const size_t utf16Index = utf16IndexAtPoint(makeAttributedString(props, displayed), props.paragraphAttributes,
                                                layoutWidth, localPoint);
    const size_t byteOffset = field.editor.byteForDisplayOffset(utf8OffsetForUtf16Index(displayed, utf16Index));

    field.editor.setSelectionRange(isExtending ? field.editor.selection().anchorByte : byteOffset, byteOffset);
}

#else

void TextInputController::placeCaretAtPoint(TextInputField& /*field*/, const facebook::react::Rect& /*box*/,
                                            facebook::react::Point /*surfacePoint*/, bool /*isExtending*/) {}

#endif

void TextInputController::synchronize() {
    for (auto& entry : fields_) {
        // The newest clone rather than the node the commit walk handed over: a state write this loop made last
        // frame produced a new one, and reconciling against the old revision would replay the same write.
        const std::shared_ptr<const TextInputShadowNode> shadowNode =
            std::dynamic_pointer_cast<const TextInputShadowNode>(
                uiManager_->getNewestCloneOfShadowNode(*entry.second.shadowNode));

        if (shadowNode == nullptr) {
            continue;
        }

        entry.second.shadowNode = shadowNode;
        reconcile(entry.second);
        publish(entry.second);
    }
}

bool TextInputController::advanceCaretBlink(double frameMilliseconds) {
    if (focusedField() == nullptr) {
        return false;
    }

    blinkMilliseconds_ += frameMilliseconds;

    if (blinkMilliseconds_ < kCaretBlinkMilliseconds) {
        return false;
    }

    blinkMilliseconds_ = 0.0;
    isCaretVisible_ = !isCaretVisible_;

    return true;
}

void TextInputController::setTextInputFocusSink(TextInputFocusSink* textInputFocusSink) noexcept {
    textInputFocusSink_ = textInputFocusSink;
}

void TextInputController::onImePreedit(const std::string& text, int32_t cursorBegin, int32_t cursorEnd) {
    TextInputField* field = focusedField();

    if (field != nullptr) {
        field->editor.applyPreedit(text, cursorBegin, cursorEnd);
    }
}

void TextInputController::onImeCommit(const std::string& text) {
    TextInputField* field = focusedField();

    if (field != nullptr) {
        field->editor.insertText(text);
    }
}

void TextInputController::onImeDeleteSurrounding(uint32_t beforeLength, uint32_t afterLength) {
    TextInputField* field = focusedField();

    if (field != nullptr) {
        field->editor.deleteSurrounding(beforeLength, afterLength);
    }
}

TextInputController::TextInputField* TextInputController::focusedField() {
    const auto field = fields_.find(focusedTag_);

    return field == fields_.end() ? nullptr : &field->second;
}

const TextInputController::TextInputField* TextInputController::focusedField() const {
    const auto field = fields_.find(focusedTag_);

    return field == fields_.end() ? nullptr : &field->second;
}

void TextInputController::reconcile(TextInputField& field) {
    const facebook::react::TextInputState& stateData = field.shadowNode->getStateData();
    const std::string reactTreeText = stateData.reactTreeAttributedString.getString();

    if (reactTreeText == field.reactTreeText) {
        return;
    }

    field.reactTreeText = reactTreeText;
    field.writtenEventCount = -1;
    field.editor.reconcileProps(reactTreeText, static_cast<int>(stateData.mostRecentEventCount));
}

void TextInputController::publish(TextInputField& field) {
    const TextInputProps& props = field.shadowNode->getConcreteProps();

    field.editor.setMaximumLength(props.maxLength);
    field.editor.setMultiline(props.multiline);
    field.editor.setSecure(props.secureTextEntry);

    const std::string displayed = field.editor.displayText();
    const facebook::react::TextInputState& stateData = field.shadowNode->getStateData();
    const bool isStateCurrent = stateData.attributedStringBox.getValue().getString() == displayed &&
                                stateData.mostRecentEventCount == field.editor.mostRecentEventCount();

    // The second test is what stops a commit per frame while the first one is still in flight: a state write is
    // asynchronous, so the shadow node this loop reads is the pre-write one until the commit lands.
    if (!isStateCurrent && field.writtenEventCount != field.editor.mostRecentEventCount()) {
        writeState(field);
        field.writtenEventCount = field.editor.mostRecentEventCount();
    }

    const bool isFocused = field.shadowNode->getTag() == focusedTag_;
    SceneEditorState editorState{
        .caretUtf16 =
            utf16LengthOfUtf8(displayed, field.editor.displayOffsetForByte(field.editor.selection().caretByte)),
        .selectionBeginUtf16 =
            utf16LengthOfUtf8(displayed, field.editor.displayOffsetForByte(field.editor.selectionBeginByte())),
        .selectionEndUtf16 =
            utf16LengthOfUtf8(displayed, field.editor.displayOffsetForByte(field.editor.selectionEndByte())),
        .compositionBeginUtf16 =
            utf16LengthOfUtf8(displayed, field.editor.displayOffsetForByte(field.editor.compositionBeginByte())),
        .compositionEndUtf16 =
            utf16LengthOfUtf8(displayed, field.editor.displayOffsetForByte(field.editor.compositionEndByte())),
        .scrollOffsetX = field.scrollOffsetX,
        .isCaretVisible = isFocused && isCaretVisible_};

#ifdef RNL_ENABLE_TEXT_GEOMETRY
    const facebook::react::Rect box = contentBox(*field.shadowNode);
    const EditorGeometryRequest request{.caretUtf16 = editorState.caretUtf16,
                                        .selectionBeginUtf16 = editorState.selectionBeginUtf16,
                                        .selectionEndUtf16 = editorState.selectionEndUtf16,
                                        .compositionBeginUtf16 = editorState.compositionBeginUtf16,
                                        .compositionEndUtf16 = editorState.compositionEndUtf16,
                                        .isMultiline = props.multiline};
    const EditorGeometry geometry =
        measureEditorGeometry(makeAttributedString(props, displayed), props.paragraphAttributes,
                              static_cast<float>(box.size.width), request);

    field.layoutWidth = geometry.layoutWidth;

    // A single-line field is a window onto a line that is allowed to be longer than it is: a caret leaving
    // either edge moves the window rather than the caret. A multiline field wraps instead, so it never scrolls
    // horizontally at all; vertical scrolling is the deferral in docs/cpp-toolchain.md.
    if (!props.multiline) {
        const float boxWidth = static_cast<float>(box.size.width);
        const float caretLeft = static_cast<float>(geometry.caret.origin.x);
        const float caretRight = caretLeft + static_cast<float>(geometry.caret.size.width);
        const float followedOffset = std::max(std::min(field.scrollOffsetX, caretLeft), caretRight - boxWidth);

        field.scrollOffsetX = std::clamp(followedOffset, 0.0F, std::max(0.0F, geometry.contentWidth - boxWidth));
        editorState.scrollOffsetX = field.scrollOffsetX;
    }

    if (isFocused && textInputFocusSink_ != nullptr) {
        textInputFocusSink_->setSurroundingText(
            displayed, static_cast<int32_t>(field.editor.displayOffsetForByte(field.editor.selection().caretByte)),
            static_cast<int32_t>(field.editor.displayOffsetForByte(field.editor.selection().anchorByte)));
        textInputFocusSink_->setCursorRectangle(
            static_cast<int32_t>(box.origin.x + geometry.caret.origin.x - field.scrollOffsetX),
            static_cast<int32_t>(box.origin.y + geometry.caret.origin.y),
            static_cast<int32_t>(geometry.caret.size.width), static_cast<int32_t>(geometry.caret.size.height));
    }
#endif

    mountingManager_->setEditorState(field.shadowNode->getTag(), editorState);
    emitEvents(field);
}

/**
 * Publishes the platform's buffer into the shadow tree, which is what makes Yoga remeasure the field and the
 * scene repaint it. The transforming form of `updateState` is used for the reason `ScrollController` uses it: a
 * commit may land between reading the state and applying this update, and only two of its fields are ours.
 *
 * `reactTreeAttributedString` is deliberately not one of them. It is React's description of the value, and
 * upstream's `updateStateIfNeeded` compares against it to decide whether the tree changed — overwriting it here
 * would tell the shadow node that React's value is whatever the user just typed, and the next controlled update
 * would then be indistinguishable from no update at all.
 */
void TextInputController::writeState(const TextInputField& field) {
    const std::shared_ptr<const facebook::react::ConcreteState<facebook::react::TextInputState>> state =
        std::dynamic_pointer_cast<const facebook::react::ConcreteState<facebook::react::TextInputState>>(
            field.shadowNode->getState());

    if (state == nullptr) {
        return;
    }

    const facebook::react::AttributedStringBox displayedBox{
        makeAttributedString(field.shadowNode->getConcreteProps(), field.editor.displayText())};
    const int64_t eventCount = field.editor.mostRecentEventCount();

    state->updateState([displayedBox, eventCount](const facebook::react::TextInputState& previousData)
                           -> facebook::react::StateData::Shared {
        facebook::react::TextInputState data = previousData;

        data.attributedStringBox = displayedBox;
        data.mostRecentEventCount = eventCount;

        return std::make_shared<const facebook::react::TextInputState>(data);
    });
}

void TextInputController::emitEvents(TextInputField& field) {
    const std::string& text = field.editor.text();
    const size_t selectionBegin = utf16LengthOfUtf8(text, field.editor.selectionBeginByte());
    const size_t selectionEnd = utf16LengthOfUtf8(text, field.editor.selectionEndByte());
    const bool hasTextChanged = text != field.emittedText;
    const bool hasSelectionChanged =
        selectionBegin != field.emittedSelectionBegin || selectionEnd != field.emittedSelectionEnd;

    field.emittedText = text;
    field.emittedSelectionBegin = selectionBegin;
    field.emittedSelectionEnd = selectionEnd;

    if (!hasTextChanged && !hasSelectionChanged) {
        return;
    }

    const std::shared_ptr<const facebook::react::TextInputEventEmitter> emitter = emitterOf(*field.shadowNode);

    if (emitter == nullptr) {
        return;
    }

    const facebook::react::TextInputEventEmitter::Metrics metrics = makeMetrics(field);

    // `onChange` first, because `onChangeText` is derived from it in JavaScript and an `onSelectionChange` that
    // arrived ahead of it would describe a string React has not been told about yet.
    if (hasTextChanged) {
        emitter->onChange(metrics);
    }

    if (hasSelectionChanged) {
        emitter->onSelectionChange(metrics);
    }
}

void TextInputController::emitKeyPress(const TextInputField& field, const std::string& text) {
    const std::shared_ptr<const facebook::react::TextInputEventEmitter> emitter = emitterOf(*field.shadowNode);

    if (emitter != nullptr) {
        emitter->onKeyPress(facebook::react::TextInputEventEmitter::KeyPressMetrics{
            .text = text, .eventCount = field.editor.mostRecentEventCount()});
    }
}

void TextInputController::emitSubmit(const TextInputField& field) {
    const std::shared_ptr<const facebook::react::TextInputEventEmitter> emitter = emitterOf(*field.shadowNode);

    if (emitter != nullptr) {
        const facebook::react::TextInputEventEmitter::Metrics metrics = makeMetrics(field);

        emitter->onSubmitEditing(metrics);
        emitter->onEndEditing(metrics);
    }
}

facebook::react::TextInputEventEmitter::Metrics TextInputController::makeMetrics(const TextInputField& field) const {
    const std::string& text = field.editor.text();
    const size_t selectionBegin = utf16LengthOfUtf8(text, field.editor.selectionBeginByte());
    const size_t selectionEnd = utf16LengthOfUtf8(text, field.editor.selectionEndByte());
    const facebook::react::Size containerSize = field.shadowNode->getLayoutMetrics().frame.size;

    return facebook::react::TextInputEventEmitter::Metrics{
        .text = text,
        .selectionRange = facebook::react::AttributedString::Range{.location = static_cast<int>(selectionBegin),
                                                                   .length = static_cast<int>(selectionEnd -
                                                                                              selectionBegin)},
        .contentSize = containerSize,
        .contentOffset = facebook::react::Point{.x = field.scrollOffsetX, .y = 0},
        .contentInset = {},
        .containerSize = containerSize,
        .eventCount = field.editor.mostRecentEventCount(),
        .layoutMeasurement = containerSize,
        .zoomScale = 1,
        .target = field.shadowNode->getTag()};
}

/**
 * The box the paragraph is laid out and drawn in, in surface coordinates: the field's absolute frame inset by
 * its borders and padding. It is the same box `RetainedScene` resolves for the text, which is what makes a click
 * land on the glyph it looks like it landed on.
 */
facebook::react::Rect TextInputController::contentBox(const TextInputShadowNode& shadowNode) const {
    const facebook::react::LayoutMetrics layoutMetrics = shadowNode.getLayoutMetrics();
    const facebook::react::Point origin =
        uiManager_->getRelativeLayoutMetrics(shadowNode, nullptr, {.includeTransform = true}).frame.origin;
    const facebook::react::EdgeInsets& insets = layoutMetrics.contentInsets;

    return facebook::react::Rect{
        .origin = facebook::react::Point{.x = origin.x + insets.left, .y = origin.y + insets.top},
        .size = facebook::react::Size{.width = layoutMetrics.frame.size.width - insets.left - insets.right,
                                      .height = layoutMetrics.frame.size.height - insets.top - insets.bottom}};
}

} // namespace react_native_linux
