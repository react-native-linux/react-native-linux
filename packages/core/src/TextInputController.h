#pragma once

#include "EditorModel.h"
#include "InputPipeline.h"
#include "LinuxMountingManager.h"
#include "TextInputComponent.h"

#include <react/renderer/components/textinput/TextInputEventEmitter.h>
#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/core/ShadowNode.h>
#include <react/renderer/graphics/Point.h>
#include <react/renderer/graphics/Rect.h>
#include <react/renderer/uimanager/UIManager.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace react_native_linux {

/**
 * What a key press did to the focused field, and therefore what the input dispatcher must not also do with it.
 *
 * `Ignored` leaves the key to the traversal and activation rules, which is what keeps Tab moving focus out of a
 * field. `Consumed` stops there, which is what keeps Space from activating the field as if it were a
 * `<Pressable>`. `ConsumedAndBlurred` additionally asks for focus to leave, which is Escape and a submit that
 * blurs — react-native-macos#1082 is what happens when the field swallows Enter and does neither.
 */
enum class TextInputKeyResult : uint8_t { Ignored, Consumed, ConsumedAndBlurred };

/**
 * The `<TextInput>` half of the platform: one `EditorModel` per mounted field, the reconciliation between those
 * buffers and React's controlled `value`, and every event a field sends back.
 *
 * The split against `EditorModel` is the one this codebase makes everywhere: everything that can be
 * arithmetically wrong — where the caret lands, what a selection covers, what a paste does to it, what
 * `maxLength` truncates, which byte range is composing — is in `EditorModel`, inside the coverage gate. What is
 * left here is a `UIManager` lookup, a state write, an event emitter and a scene mark, none of which exists
 * without a committed shadow tree; `hello_react --type` is the test for it, exactly as `--scroll-to` is the test
 * for `ScrollController`.
 *
 * The text lives in **three** places on purpose, and the direction of travel between them is the whole
 * controlled-value contract:
 *
 * - `EditorModel` holds the buffer the user is editing, and the event count of the last edit.
 * - `TextInputState` holds what is displayed and measured — the masked string for a `secureTextEntry` field —
 *   and is written by this class after every edit, so Yoga remeasures and the scene repaints without React
 *   being involved.
 * - `props.text`, reaching us as the state's `reactTreeAttributedString`, holds what React believes the value
 *   is. It is adopted only when it **changed** and when the event count React echoed back matches the buffer's,
 *   which together mean "React has seen every edit and this is a new value". Either half alone is a bug:
 *   without the change test an uncontrolled field is emptied on every re-render, and without the count test a
 *   render that is one keystroke behind drags the caret backwards mid-word — react-native-macos#2066 and #2127.
 *
 * Threading contract: the frame thread owns this object, like everything else the input dispatcher holds. It
 * writes shadow state through `ConcreteState::updateState`, which is documented as callable from any thread and
 * is the same call `ScrollController` makes per frame.
 */
class TextInputController final : public ImeSink {
public:
    TextInputController(std::shared_ptr<facebook::react::UIManager> uiManager,
                        std::shared_ptr<LinuxMountingManager> mountingManager);

    /**
     * The `<TextInput>` nodes of the committed tree, in mount order, refreshed once per commit.
     *
     * Every mounted field gets a buffer, not only the focused one, and that is what makes `secureTextEntry`
     * structural rather than conditional: the masked string is published into the state on the first frame a
     * field exists, so no paragraph is ever built from the buffer — react-native-macos#423 in a golden. A field
     * whose node has left the tree loses its entry here and nowhere else.
     */
    void setMountedFields(const std::vector<std::shared_ptr<const TextInputShadowNode>>& shadowNodes);

    /**
     * Follows the focus model. A field that loses focus keeps its buffer, so tabbing away and back does not
     * clear what was typed.
     */
    void setFocusedNode(const std::shared_ptr<const facebook::react::ShadowNode>& shadowNode);

    /**
     * Whether the focused field is mid-composition, in which case no key reaches React and no editing command
     * runs. That is the ordering rule from *IME* in docs/cpp-toolchain.md: text arrives as a commit and only as
     * a commit, so a field that also inserted the key events an input method leaves behind would double every
     * character it composes.
     */
    bool isComposing() const;

    TextInputKeyResult handleKey(const InputEvent& event);

    /**
     * Places the caret where the pointer went down inside the focused field, and extends the selection while the
     * button stays down. A press outside the field does nothing here; the focus model has already blurred it.
     */
    void handlePointer(const InputEvent& event);

    /**
     * Reconciles every live field with React and republishes what the scene draws. Called once per frame, after
     * the frame's input, so an edit and the props update that answers it settle in the same frame.
     */
    void synchronize();

    /**
     * Advances the caret blink by one frame, and reports whether the caret changed phase.
     *
     * The blink is frame-driven rather than timer-driven because there is no timer in the frame path and
     * ADR-0001 says there will not be one. A headless run never calls it, so a caret in a golden is always in
     * its visible phase and a checked-in picture is reproducible.
     */
    bool advanceCaretBlink(double frameMilliseconds);

    /**
     * Registers the compositor's text input, which is what receives the surrounding text and the caret rectangle
     * an input method places its candidate window beside. Borrowed, never owned.
     */
    void setTextInputFocusSink(TextInputFocusSink* textInputFocusSink) noexcept;

    void onImePreedit(const std::string& text, int32_t cursorBegin, int32_t cursorEnd) override;
    void onImeCommit(const std::string& text) override;
    void onImeDeleteSurrounding(uint32_t beforeLength, uint32_t afterLength) override;

private:
    /**
     * One field's platform-side state: the buffer, the node it belongs to, what was last reported to React, and
     * how far a single-line field has scrolled to keep the caret in view.
     */
    struct TextInputField {
        std::shared_ptr<const TextInputShadowNode> shadowNode;
        EditorModel editor;
        std::string reactTreeText;
        std::string emittedText;
        size_t emittedSelectionBegin{0};
        size_t emittedSelectionEnd{0};
        float scrollOffsetX{0.0F};
        float scrollOffsetY{0.0F};
        float layoutWidth{0.0F};
        int writtenEventCount{-1};
    };

    TextInputField* focusedField();
    const TextInputField* focusedField() const;
    void reconcile(TextInputField& field);
    void publish(TextInputField& field);
    void writeState(const TextInputField& field);
    void emitEvents(TextInputField& field);
    void emitKeyPress(const TextInputField& field, const std::string& text);
    void emitSubmit(const TextInputField& field);
    void placeCaretAtPoint(TextInputField& field, const facebook::react::Rect& box,
                           facebook::react::Point surfacePoint, bool isExtending);
    facebook::react::TextInputEventEmitter::Metrics makeMetrics(const TextInputField& field) const;
    facebook::react::Rect contentBox(const TextInputShadowNode& shadowNode) const;
    TextInputKeyResult handleShortcut(const InputEvent& event, TextInputField& field, bool isEditable);
    TextInputKeyResult handleNamedKey(const InputEvent& event, TextInputField& field, bool isEditable);

    std::shared_ptr<facebook::react::UIManager> uiManager_;
    std::shared_ptr<LinuxMountingManager> mountingManager_;
    std::unordered_map<facebook::react::Tag, TextInputField> fields_;
    TextInputFocusSink* textInputFocusSink_{nullptr};
    facebook::react::Tag focusedTag_{0};
    double blinkMilliseconds_{0.0};
    bool isCaretVisible_{true};
    bool isSelectingByPointer_{false};
};

} // namespace react_native_linux
