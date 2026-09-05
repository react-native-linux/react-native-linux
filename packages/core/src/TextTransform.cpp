#include "TextTransform.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace react_native_linux {

namespace {

constexpr unsigned char kLatin1SupplementLeadByte = 0xC3;
constexpr int kLatin1SupplementBase = 0xC0;
constexpr unsigned char kContinuationByteBits = 0x3F;
constexpr unsigned char kContinuationBytePrefix = 0x80;
constexpr int kAsciiCaseOffset = 0x20;
constexpr int kMultiplicationSign = 0xD7;
constexpr int kDivisionSign = 0xF7;

bool isAsciiWhitespace(char byte) {
    switch (byte) {
        case ' ':
        case '\t':
        case '\n':
        case '\v':
        case '\f':
        case '\r':
            return true;
        default:
            return false;
    }
}

/**
 * The byte width of the UTF-8 sequence starting at `leadByte`, from the leading bits alone. A byte that cannot
 * start a sequence — a stray continuation byte in malformed input — is treated as its own one-byte unit so the
 * scan always makes progress.
 */
size_t utf8SequenceLength(unsigned char leadByte) {
    if ((leadByte & 0x80U) == 0x00U) {
        return 1;
    }

    if ((leadByte & 0xE0U) == 0xC0U) {
        return 2;
    }

    if ((leadByte & 0xF0U) == 0xE0U) {
        return 3;
    }

    if ((leadByte & 0xF8U) == 0xF0U) {
        return 4;
    }

    return 1;
}

/**
 * `utf8SequenceLength` clamped to what is actually left in `text`, so a truncated multi-byte sequence at the end
 * of a malformed string is never read past its bounds.
 */
size_t sequenceLengthAt(const std::string& text, size_t index) {
    return std::min(utf8SequenceLength(static_cast<unsigned char>(text[index])), text.size() - index);
}

/**
 * A Latin-1 Supplement letter's other case, `offset` points away on the codepoint line (negative to go from
 * lowercase to uppercase, positive the other way), re-encoded as the same two-byte UTF-8 shape.
 */
void appendShiftedLatin1Letter(std::string& output, unsigned char continuationByte, int offset) {
    output.push_back(static_cast<char>(kLatin1SupplementLeadByte));
    output.push_back(
        static_cast<char>(kContinuationBytePrefix | ((continuationByte + offset) & kContinuationByteBits)));
}

/**
 * Appends the one character at `text[index]`, `length` bytes wide, case-mapped when it is ASCII or Latin-1
 * Supplement and left alone otherwise.
 */
void appendCased(std::string& output, const std::string& text, size_t index, size_t length, bool toUpper) {
    if (length == 1) {
        const char byte = text[index];

        if (toUpper && byte >= 'a' && byte <= 'z') {
            output.push_back(static_cast<char>(byte - kAsciiCaseOffset));

            return;
        }

        if (!toUpper && byte >= 'A' && byte <= 'Z') {
            output.push_back(static_cast<char>(byte + kAsciiCaseOffset));

            return;
        }

        output.push_back(byte);

        return;
    }

    if (length == 2 && static_cast<unsigned char>(text[index]) == kLatin1SupplementLeadByte) {
        const unsigned char second = static_cast<unsigned char>(text[index + 1]);
        // `codepoint` is `kLatin1SupplementBase + (0-0x3F)`, so it is always in [0xC0, 0xFF]; there is no
        // separate lower bound to check on either range below.
        const int codepoint = kLatin1SupplementBase + (second & kContinuationByteBits);
        const bool isUppercaseLetter = codepoint <= 0xDE && codepoint != kMultiplicationSign;
        const bool isLowercaseLetter = codepoint >= 0xE0 && codepoint <= 0xFE && codepoint != kDivisionSign;

        if (toUpper && isLowercaseLetter) {
            appendShiftedLatin1Letter(output, second, -kAsciiCaseOffset);

            return;
        }

        if (!toUpper && isUppercaseLetter) {
            appendShiftedLatin1Letter(output, second, kAsciiCaseOffset);

            return;
        }
    }

    output.append(text, index, length);
}

std::string mapCase(const std::string& text, bool toUpper) {
    std::string result;

    result.reserve(text.size());

    size_t index = 0;

    while (index < text.size()) {
        const size_t length = sequenceLengthAt(text, index);

        appendCased(result, text, index, length, toUpper);
        index += length;
    }

    return result;
}

std::string capitalize(const std::string& text) {
    std::string result;

    result.reserve(text.size());

    bool atWordStart = true;
    size_t index = 0;

    while (index < text.size()) {
        const size_t length = sequenceLengthAt(text, index);

        if (length == 1 && isAsciiWhitespace(text[index])) {
            result.push_back(text[index]);
            atWordStart = true;
            index += length;

            continue;
        }

        if (atWordStart) {
            appendCased(result, text, index, length, true);
            atWordStart = false;
        } else {
            // react/react-native#34117: React Native lowercases the rest of the word, unlike CSS, which leaves
            // an already-uppercase remainder alone.
            appendCased(result, text, index, length, false);
        }

        index += length;
    }

    return result;
}

} // namespace

std::string applyTextTransform(const std::string& utf8Text, facebook::react::TextTransform transform) {
    switch (transform) { // COV_EXCL: every TextTransform value has a case below, so the implicit no-match branch cannot execute
        case facebook::react::TextTransform::Uppercase:
            return mapCase(utf8Text, true);
        case facebook::react::TextTransform::Lowercase:
            return mapCase(utf8Text, false);
        case facebook::react::TextTransform::Capitalize:
            return capitalize(utf8Text);
        case facebook::react::TextTransform::None:
        case facebook::react::TextTransform::Unset:
            return utf8Text;
    }

    return utf8Text; // COV_EXCL: every TextTransform value has a case above, so this fallback cannot execute
}

} // namespace react_native_linux
