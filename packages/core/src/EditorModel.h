#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace react_native_linux {

/**
 * Where a text run may be cut, as byte offsets into the UTF-8 string it was computed from.
 *
 * Both lists are ascending, both start at zero and both end at the string's length, so a caret is always on a
 * boundary and a motion is a step through a list rather than arithmetic over bytes. `graphemeStarts` is what one
 * press of an arrow key moves by — a user-perceived character, which is neither a byte nor a code point — and
 * `wordStarts` is what Ctrl and an arrow key move by.
 *
 * ICU supplies both in the real build, through SkUnicode inside the text pipeline. `segmentUtf8CodePoints` is the
 * fallback and a deliberate subset: every grapheme boundary is also a code-point boundary, so a caret driven by
 * it never lands inside a UTF-8 sequence — it only fails to treat a combining sequence or an emoji ZWJ sequence
 * as one step. That is the same Skia-off degradation the text pipeline already has, rather than a second Unicode
 * implementation to keep in sync.
 */
struct TextSegments {
    std::vector<size_t> graphemeStarts;
    std::vector<size_t> wordStarts;
};

using TextSegmenter = std::function<TextSegments(const std::string&)>;

/**
 * Grapheme starts as code-point starts, and word starts as the transitions between whitespace and everything
 * else. The fallback described on `TextSegments`.
 */
TextSegments segmentUtf8CodePoints(const std::string& text);

/**
 * The number of UTF-16 code units in the first `byteOffset` bytes of a UTF-8 string.
 *
 * SkParagraph's `getRectsForRange` and `getGlyphPositionAtCoordinate` speak UTF-16 indices while everything on
 * this side of the boundary speaks UTF-8 bytes, so this is the conversion the caret geometry goes through. It is
 * here rather than beside the paragraph because it is arithmetic, and arithmetic belongs where the coverage gate
 * can see it.
 */
size_t utf16LengthOfUtf8(const std::string& text, size_t byteOffset);

/**
 * The inverse: the byte offset `utf16Index` UTF-16 code units into a UTF-8 string. An index past the end of the
 * string is the end of the string, which is what a click past the last glyph produces.
 */
size_t utf8OffsetForUtf16Index(const std::string& text, size_t utf16Index);

enum class CaretMotion : uint8_t { Left, Right, WordLeft, WordRight, LineStart, LineEnd };

/**
 * The caret and the selection as one pair of byte offsets: the anchor is where the selection started and the
 * caret is where it currently ends, so shift and an arrow key move the caret and leave the anchor where it was.
 * They are equal when nothing is selected.
 */
struct EditorSelection {
    size_t anchorByte{0};
    size_t caretByte{0};
};

/**
 * The text buffer of one `<TextInput>`: the string, the selection, the composition range, and every rule that
 * decides how a keystroke changes them.
 *
 * This class is the part of a text field that can be arithmetically wrong, kept where the coverage gate can see
 * it — the same split `FocusModel` makes for focus and `TextInputV3State` makes for composition. It knows
 * nothing about React, Fabric, Skia, Wayland or events: `TextInputController` owns all of that and calls this for
 * every decision that has an answer independent of them. react-native-macos#2955, #2066, #2303, #480, #486, #432
 * and #2270 are all failures of this object in their platform's terms, which is why it is a class and not a set
 * of fields on the controller.
 *
 * Byte offsets are the whole vocabulary, and every one of them is on a grapheme boundary by construction: the
 * public mutators clamp what they are given to the segmentation, so a caret that would split a UTF-8 sequence or
 * a combining mark cannot be produced from outside. Grapheme and word boundaries come from the injected
 * `TextSegmenter`; without one, the code-point fallback above is used.
 *
 * `mostRecentEventCount` is the reconciliation counter React Native's controlled-value contract is built on. It
 * is incremented by every edit this object makes on the user's behalf and never by a props update, and
 * `reconcileProps` applies a `value` from JavaScript only when the count JavaScript echoed back matches the
 * current one — which is exactly what says that JavaScript has seen every edit so far and that its value is
 * therefore not stale. See *TextInput* in docs/cpp-toolchain.md.
 *
 * Threading contract: the frame thread owns this object, like everything else the input dispatcher reaches.
 */
class EditorModel final {
public:
    void setSegmenter(TextSegmenter segmenter);

    /**
     * The `maxLength` prop, counted in UTF-16 code units because that is what iOS and Android count. A value at
     * or below zero is no limit, which is also how upstream's default of `INT_MAX` behaves.
     */
    void setMaximumLength(int maximumLength) noexcept;

    /**
     * Whether a newline is text or a submission. A single-line field never contains one: pasted and committed
     * newlines collapse to spaces rather than being inserted, which is react-native-macos#2303.
     */
    void setMultiline(bool isMultiline) noexcept;

    /**
     * Whether `displayText` masks the buffer. The buffer itself is never masked, because it is what the change
     * events carry to JavaScript; the masking happens on the way to the paragraph, which is the only thing that
     * makes react-native-macos#423 impossible rather than unlikely.
     */
    void setSecure(bool isSecure) noexcept;

    const std::string& text() const noexcept;
    std::string displayText() const;

    /**
     * The two conversions between the buffer and what is drawn. They are the identity for an ordinary field and
     * a grapheme-for-bullet mapping for a `secureTextEntry` one, which is the whole of masking: the caret and
     * the selection are computed against the buffer and then moved into the string the paragraph actually
     * contains, so no unmasked character is ever measured or drawn.
     */
    size_t displayOffsetForByte(size_t byteOffset) const;
    size_t byteForDisplayOffset(size_t displayOffset) const;

    EditorSelection selection() const noexcept;
    size_t selectionBeginByte() const noexcept;
    size_t selectionEndByte() const noexcept;
    bool hasSelection() const noexcept;
    std::string selectedText() const;

    size_t compositionBeginByte() const noexcept;
    size_t compositionEndByte() const noexcept;
    bool isComposing() const noexcept;

    int mostRecentEventCount() const noexcept;

    /**
     * Replaces the whole buffer without touching the event count, which is what a `defaultValue` or a first
     * mount does. Everything a caller can get wrong — a selection past the end, a composition that no longer
     * exists — is clamped here rather than by the caller.
     */
    bool setText(std::string text);

    /**
     * Replaces the selection with `insertion`, or the composing run when one is open — which is what makes this
     * the commit path as well as the typing path. An input method's `commit_string` is an insertion at the caret
     * that ends the composition, and giving it a method of its own would be the same function under a second
     * name; the reason a key press must not also take this path during a composition is in *IME* in
     * docs/cpp-toolchain.md.
     */
    bool insertText(const std::string& insertion);
    bool deleteBackward();
    bool deleteForward();
    bool moveCaret(CaretMotion motion, bool isExtending);
    bool setSelectionRange(size_t anchorByte, size_t caretByte);
    bool selectAll();

    /**
     * The composing run as `zwp_text_input_v3` last described it. An empty string ends the composition, and the
     * cursor pair is a byte range inside the pre-edit with `-1, -1` meaning the input method wants no cursor
     * shown — in which case the caret goes to the end of the run, which is where every toolkit puts it.
     *
     * A pre-edit is not an edit: it never moves the event count, because the text JavaScript owns has not
     * changed until the commit arrives.
     */
    bool applyPreedit(const std::string& preedit, int32_t cursorBegin, int32_t cursorEnd);

    /**
     * `delete_surrounding_text`, in the protocol's own units: bytes before and after the caret. Both ends are
     * snapped outwards to a grapheme boundary, because deleting half of a UTF-8 sequence is not a thing this
     * buffer is allowed to contain.
     */
    bool deleteSurrounding(uint32_t beforeLength, uint32_t afterLength);

    /**
     * The controlled-value rule. Returns whether the buffer was replaced.
     *
     * An update whose echoed count is not the current one describes a buffer that has already moved on, and is
     * dropped — that is the whole of the contract, and applying it anyway is react-native-macos#2066 in the
     * other direction: the caret jumping back to where JavaScript last saw it while the user is still typing.
     */
    bool reconcileProps(const std::string& propsText, int propsMostRecentEventCount);

private:
    void resegment();
    size_t clampToGrapheme(size_t byteOffset) const;
    size_t previousGrapheme(size_t byteOffset) const;
    size_t nextGrapheme(size_t byteOffset) const;
    size_t previousWord(size_t byteOffset) const;
    size_t nextWord(size_t byteOffset) const;
    size_t lineStart(size_t byteOffset) const;
    size_t lineEnd(size_t byteOffset) const;
    size_t motionTarget(CaretMotion motion) const;
    size_t remainingCapacity(size_t beginByte, size_t endByte) const;
    std::string normalizeInsertion(const std::string& insertion) const;
    bool replaceRange(size_t beginByte, size_t endByte, const std::string& replacement);
    void collapseCaretTo(size_t byteOffset);
    void clampSelection();

    TextSegmenter segmenter_;
    TextSegments segments_{.graphemeStarts = {0}, .wordStarts = {0}};
    std::string text_;
    EditorSelection selection_;
    size_t compositionBeginByte_{0};
    size_t compositionLength_{0};
    int maximumLength_{0};
    int mostRecentEventCount_{0};
    bool isMultiline_{false};
    bool isSecure_{false};
    bool isComposing_{false};
};

} // namespace react_native_linux
