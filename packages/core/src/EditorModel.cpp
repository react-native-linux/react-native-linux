#include "EditorModel.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace react_native_linux {

namespace {

constexpr char kSecureBullet[] = "\xE2\x80\xA2";
constexpr unsigned char kContinuationMask = 0xC0;
constexpr unsigned char kContinuationValue = 0x80;
constexpr unsigned char kFourByteLeadValue = 0xF0;
constexpr size_t kSurrogatePairLength = 2;
constexpr size_t kSingleCodeUnitLength = 1;

bool isContinuationByte(char byte) {
    return (static_cast<unsigned char>(byte) & kContinuationMask) == kContinuationValue;
}

bool isSupplementaryLeadByte(char byte) {
    return static_cast<unsigned char>(byte) >= kFourByteLeadValue;
}

bool isWhitespaceByte(char byte) {
    return byte == ' ' || (byte >= '\t' && byte <= '\r');
}

size_t countBoundariesBelow(const std::vector<size_t>& boundaries, size_t byteOffset) {
    return static_cast<size_t>(std::lower_bound(boundaries.begin(), boundaries.end(), byteOffset) -
                               boundaries.begin());
}

size_t boundaryBefore(const std::vector<size_t>& boundaries, size_t byteOffset, size_t fallback) {
    const auto position = std::lower_bound(boundaries.begin(), boundaries.end(), byteOffset);

    if (position == boundaries.begin()) {
        return fallback;
    }

    return *(position - 1);
}

size_t boundaryAfter(const std::vector<size_t>& boundaries, size_t byteOffset, size_t fallback) {
    const auto position = std::upper_bound(boundaries.begin(), boundaries.end(), byteOffset);

    if (position == boundaries.end()) {
        return fallback;
    }

    return *position;
}

/**
 * The longest prefix of `text` that fits in `codeUnits` UTF-16 code units, cut on a code-point boundary.
 */
std::string truncateToUtf16Length(const std::string& text, size_t codeUnits) {
    size_t consumed = 0;

    for (size_t index = 0; index < text.size(); ++index) {
        if (isContinuationByte(text[index])) {
            continue;
        }

        const size_t width = isSupplementaryLeadByte(text[index]) ? kSurrogatePairLength : kSingleCodeUnitLength;

        if (consumed + width > codeUnits) {
            return text.substr(0, index);
        }

        consumed += width;
    }

    return text;
}

} // namespace

TextSegments segmentUtf8CodePoints(const std::string& text) {
    TextSegments segments;

    segments.graphemeStarts.push_back(0);
    segments.wordStarts.push_back(0);

    for (size_t index = 1; index < text.size(); ++index) {
        if (isContinuationByte(text[index])) {
            continue;
        }

        segments.graphemeStarts.push_back(index);

        if (isWhitespaceByte(text[index]) != isWhitespaceByte(text[index - 1])) {
            segments.wordStarts.push_back(index);
        }
    }

    if (!text.empty()) {
        segments.graphemeStarts.push_back(text.size());
        segments.wordStarts.push_back(text.size());
    }

    return segments;
}

size_t utf16LengthOfUtf8(const std::string& text, size_t byteOffset) {
    const size_t limit = std::min(byteOffset, text.size());
    size_t codeUnits = 0;

    for (size_t index = 0; index < limit; ++index) {
        if (isContinuationByte(text[index])) {
            continue;
        }

        codeUnits += isSupplementaryLeadByte(text[index]) ? kSurrogatePairLength : kSingleCodeUnitLength;
    }

    return codeUnits;
}

size_t utf8OffsetForUtf16Index(const std::string& text, size_t utf16Index) {
    size_t consumed = 0;

    for (size_t index = 0; index < text.size(); ++index) {
        if (isContinuationByte(text[index])) {
            continue;
        }

        if (consumed >= utf16Index) {
            return index;
        }

        consumed += isSupplementaryLeadByte(text[index]) ? kSurrogatePairLength : kSingleCodeUnitLength;
    }

    return text.size();
}

void EditorModel::setSegmenter(TextSegmenter segmenter) {
    segmenter_ = std::move(segmenter);
    resegment();
    clampSelection();
}

void EditorModel::setMaximumLength(int maximumLength) noexcept { maximumLength_ = maximumLength; }

void EditorModel::setMultiline(bool isMultiline) noexcept { isMultiline_ = isMultiline; }

void EditorModel::setSecure(bool isSecure) noexcept { isSecure_ = isSecure; }

const std::string& EditorModel::text() const noexcept { return text_; }

std::string EditorModel::displayText() const {
    if (!isSecure_) {
        return text_;
    }

    std::string masked;

    for (size_t grapheme = 0; grapheme + 1 < segments_.graphemeStarts.size(); ++grapheme) {
        masked += kSecureBullet;
    }

    return masked;
}

size_t EditorModel::displayOffsetForByte(size_t byteOffset) const {
    if (!isSecure_) {
        return clampToGrapheme(byteOffset);
    }

    return countBoundariesBelow(segments_.graphemeStarts, clampToGrapheme(byteOffset)) * (sizeof(kSecureBullet) - 1);
}

size_t EditorModel::byteForDisplayOffset(size_t displayOffset) const {
    if (!isSecure_) {
        return clampToGrapheme(displayOffset);
    }

    const size_t grapheme = displayOffset / (sizeof(kSecureBullet) - 1);

    if (grapheme + 1 >= segments_.graphemeStarts.size()) {
        return text_.size();
    }

    return segments_.graphemeStarts[grapheme];
}

EditorSelection EditorModel::selection() const noexcept { return selection_; }

size_t EditorModel::selectionBeginByte() const noexcept {
    return std::min(selection_.anchorByte, selection_.caretByte);
}

size_t EditorModel::selectionEndByte() const noexcept {
    return std::max(selection_.anchorByte, selection_.caretByte);
}

bool EditorModel::hasSelection() const noexcept { return selection_.anchorByte != selection_.caretByte; }

std::string EditorModel::selectedText() const {
    return text_.substr(selectionBeginByte(), selectionEndByte() - selectionBeginByte());
}

size_t EditorModel::compositionBeginByte() const noexcept { return compositionBeginByte_; }

size_t EditorModel::compositionEndByte() const noexcept { return compositionBeginByte_ + compositionLength_; }

bool EditorModel::isComposing() const noexcept { return isComposing_; }

int EditorModel::mostRecentEventCount() const noexcept { return mostRecentEventCount_; }

bool EditorModel::setText(std::string text) {
    if (text == text_ && !isComposing_) {
        return false;
    }

    text_ = std::move(text);
    compositionBeginByte_ = 0;
    compositionLength_ = 0;
    isComposing_ = false;
    resegment();
    clampSelection();

    return true;
}

bool EditorModel::insertText(const std::string& insertion) {
    const size_t begin = isComposing_ ? compositionBeginByte_ : selectionBeginByte();
    const size_t end = isComposing_ ? compositionEndByte() : selectionEndByte();
    const std::string normalized = normalizeInsertion(insertion);
    const std::string fitted = truncateToUtf16Length(normalized, remainingCapacity(begin, end));

    if (begin == end && fitted.empty()) {
        return false;
    }

    return replaceRange(begin, end, fitted);
}

bool EditorModel::deleteBackward() {
    if (hasSelection()) {
        return replaceRange(selectionBeginByte(), selectionEndByte(), {});
    }

    if (selection_.caretByte == 0) {
        return false;
    }

    return replaceRange(previousGrapheme(selection_.caretByte), selection_.caretByte, {});
}

bool EditorModel::deleteForward() {
    if (hasSelection()) {
        return replaceRange(selectionBeginByte(), selectionEndByte(), {});
    }

    if (selection_.caretByte == text_.size()) {
        return false;
    }

    return replaceRange(selection_.caretByte, nextGrapheme(selection_.caretByte), {});
}

bool EditorModel::moveCaret(CaretMotion motion, bool isExtending) {
    const bool isHorizontalStep = motion == CaretMotion::Left || motion == CaretMotion::Right;

    // An arrow key with a selection collapses it to the edge it points at instead of stepping from the caret,
    // which is what every desktop toolkit does and what makes a misaimed Shift+Left recoverable.
    if (!isExtending && isHorizontalStep && hasSelection()) {
        collapseCaretTo(motion == CaretMotion::Left ? selectionBeginByte() : selectionEndByte());

        return true;
    }

    const size_t target = motionTarget(motion);

    if (isExtending) {
        if (target == selection_.caretByte) {
            return false;
        }

        selection_.caretByte = target;

        return true;
    }

    if (target == selection_.caretByte && !hasSelection()) {
        return false;
    }

    collapseCaretTo(target);

    return true;
}

bool EditorModel::setSelectionRange(size_t anchorByte, size_t caretByte) {
    const EditorSelection clamped{.anchorByte = clampToGrapheme(anchorByte), .caretByte = clampToGrapheme(caretByte)};

    if (clamped.anchorByte == selection_.anchorByte && clamped.caretByte == selection_.caretByte) {
        return false;
    }

    selection_ = clamped;

    return true;
}

bool EditorModel::selectAll() { return setSelectionRange(0, text_.size()); }

bool EditorModel::applyPreedit(const std::string& preedit, int32_t cursorBegin, int32_t cursorEnd) {
    if (!isComposing_ && preedit.empty()) {
        return false;
    }

    const size_t begin = isComposing_ ? compositionBeginByte_ : selectionBeginByte();
    const size_t end = isComposing_ ? compositionEndByte() : selectionEndByte();

    text_.replace(begin, end - begin, preedit);
    resegment();

    compositionBeginByte_ = begin;
    compositionLength_ = preedit.size();
    isComposing_ = !preedit.empty();

    // The protocol's cursor pair is a range inside the pre-edit rather than a single position: equal values are
    // a cursor and different ones are a selection the input method wants shown. `-1, -1` is a hidden cursor, and
    // the end of the composing run is where every toolkit puts the caret then.
    const size_t anchorOffset = cursorBegin < 0 ? preedit.size()
                                                : std::min(static_cast<size_t>(cursorBegin), preedit.size());
    const size_t caretOffset = cursorEnd < 0 ? preedit.size()
                                             : std::min(static_cast<size_t>(cursorEnd), preedit.size());

    selection_ = EditorSelection{.anchorByte = clampToGrapheme(begin + anchorOffset),
                                 .caretByte = clampToGrapheme(begin + caretOffset)};

    return true;
}

bool EditorModel::deleteSurrounding(uint32_t beforeLength, uint32_t afterLength) {
    const bool hadComposition = isComposing_;

    if (hadComposition) {
        text_.erase(compositionBeginByte_, compositionLength_);
        resegment();
        collapseCaretTo(compositionBeginByte_);
        compositionLength_ = 0;
        isComposing_ = false;
    }

    const size_t caret = selection_.caretByte;
    const size_t before = static_cast<size_t>(beforeLength);
    const size_t begin = clampToGrapheme(caret > before ? caret - before : 0);
    const size_t requestedEnd = std::min(caret + static_cast<size_t>(afterLength), text_.size());
    const size_t clampedEnd = clampToGrapheme(requestedEnd);
    const size_t end = clampedEnd < requestedEnd ? nextGrapheme(clampedEnd) : clampedEnd;

    if (begin == end) {
        if (!hadComposition) {
            return false;
        }

        ++mostRecentEventCount_;

        return true;
    }

    return replaceRange(begin, end, {});
}

bool EditorModel::reconcileProps(const std::string& propsText, int propsMostRecentEventCount) {
    if (propsMostRecentEventCount != mostRecentEventCount_) {
        return false;
    }

    return setText(propsText);
}

void EditorModel::resegment() {
    segments_ = segmenter_ ? segmenter_(text_) : segmentUtf8CodePoints(text_);

    // The contract on `TextSegments` is what every offset in this class relies on, and the production segmenter
    // is ICU behind a Skia archive rather than code the coverage gate can see. Restoring the two ends costs two
    // comparisons and removes the only way an out-of-range offset could reach the buffer.
    if (segments_.graphemeStarts.empty() || segments_.graphemeStarts.front() != 0) {
        segments_.graphemeStarts.insert(segments_.graphemeStarts.begin(), 0);
    }

    if (segments_.graphemeStarts.back() != text_.size()) {
        segments_.graphemeStarts.push_back(text_.size());
    }

    if (segments_.wordStarts.empty() || segments_.wordStarts.front() != 0) {
        segments_.wordStarts.insert(segments_.wordStarts.begin(), 0);
    }

    if (segments_.wordStarts.back() != text_.size()) {
        segments_.wordStarts.push_back(text_.size());
    }
}

size_t EditorModel::clampToGrapheme(size_t byteOffset) const {
    if (byteOffset >= text_.size()) {
        return text_.size();
    }

    return boundaryBefore(segments_.graphemeStarts, byteOffset + 1, 0);
}

size_t EditorModel::previousGrapheme(size_t byteOffset) const {
    return boundaryBefore(segments_.graphemeStarts, byteOffset, 0);
}

size_t EditorModel::nextGrapheme(size_t byteOffset) const {
    return boundaryAfter(segments_.graphemeStarts, byteOffset, text_.size());
}

/**
 * The start of the word before the caret: the nearest boundary that begins a run of non-whitespace. Stepping to
 * the whitespace run first is what a boundary list alone would do, and it is not what Ctrl+Left means.
 */
size_t EditorModel::previousWord(size_t byteOffset) const {
    for (size_t index = segments_.wordStarts.size(); index > 0; --index) {
        const size_t boundary = segments_.wordStarts[index - 1];

        if (boundary < byteOffset && !isWhitespaceByte(text_[boundary])) {
            return boundary;
        }
    }

    return 0;
}

/**
 * The end of the word after the caret: the nearest boundary that is the end of the text or is followed by
 * whitespace. The mirror of `previousWord`, and the same convention GTK and every terminal use.
 */
size_t EditorModel::nextWord(size_t byteOffset) const {
    for (const size_t boundary : segments_.wordStarts) {
        if (boundary > byteOffset && (boundary == text_.size() || isWhitespaceByte(text_[boundary]))) {
            return boundary;
        }
    }

    return text_.size();
}

size_t EditorModel::lineStart(size_t byteOffset) const {
    if (byteOffset == 0) {
        return 0;
    }

    const size_t newline = text_.rfind('\n', byteOffset - 1);

    return newline == std::string::npos ? 0 : newline + 1;
}

size_t EditorModel::lineEnd(size_t byteOffset) const {
    const size_t newline = text_.find('\n', byteOffset);

    return newline == std::string::npos ? text_.size() : newline;
}

size_t EditorModel::motionTarget(CaretMotion motion) const {
    const size_t caret = selection_.caretByte;

    if (motion == CaretMotion::Left) {
        return previousGrapheme(caret);
    }

    if (motion == CaretMotion::Right) {
        return nextGrapheme(caret);
    }

    if (motion == CaretMotion::WordLeft) {
        return previousWord(caret);
    }

    if (motion == CaretMotion::WordRight) {
        return nextWord(caret);
    }

    if (motion == CaretMotion::LineStart) {
        return lineStart(caret);
    }

    return lineEnd(caret);
}

size_t EditorModel::remainingCapacity(size_t beginByte, size_t endByte) const {
    if (maximumLength_ <= 0) {
        return std::string::npos;
    }

    const size_t replaced = utf16LengthOfUtf8(text_, endByte) - utf16LengthOfUtf8(text_, beginByte);
    const size_t occupied = utf16LengthOfUtf8(text_, text_.size()) - replaced;
    const size_t limit = static_cast<size_t>(maximumLength_);

    return occupied >= limit ? 0 : limit - occupied;
}

/**
 * What a single-line field does with a newline: it becomes a space rather than a line break, because a
 * single-line paragraph would otherwise carry a break it can neither show nor scroll to.
 * react-native-macos#2303 is the bug filed when a paste inserted them literally.
 */
std::string EditorModel::normalizeInsertion(const std::string& insertion) const {
    if (isMultiline_) {
        return insertion;
    }

    std::string normalized;

    normalized.reserve(insertion.size());

    for (size_t index = 0; index < insertion.size(); ++index) {
        if (insertion[index] == '\r') {
            normalized += ' ';

            if (index + 1 < insertion.size() && insertion[index + 1] == '\n') {
                ++index;
            }

            continue;
        }

        normalized += insertion[index] == '\n' ? ' ' : insertion[index];
    }

    return normalized;
}

bool EditorModel::replaceRange(size_t beginByte, size_t endByte, const std::string& replacement) {
    text_.replace(beginByte, endByte - beginByte, replacement);
    compositionBeginByte_ = 0;
    compositionLength_ = 0;
    isComposing_ = false;
    resegment();
    collapseCaretTo(beginByte + replacement.size());
    ++mostRecentEventCount_;

    return true;
}

void EditorModel::collapseCaretTo(size_t byteOffset) {
    const size_t clamped = clampToGrapheme(byteOffset);

    selection_ = EditorSelection{.anchorByte = clamped, .caretByte = clamped};
}

void EditorModel::clampSelection() {
    selection_ = EditorSelection{.anchorByte = clampToGrapheme(selection_.anchorByte),
                                 .caretByte = clampToGrapheme(selection_.caretByte)};
}

} // namespace react_native_linux
